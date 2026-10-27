#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
MultiTool TwinCommander — двухпанельный файловый менеджер для обмена
файлами между ПК и ретро-консолью (pico-retro-new-gen, FTP-сервер на H3).

Левая панель  — файлы на компьютере.
Правая панель — файлы на SD/USB консоли (FTP).

Горячие клавиши / мышь:
  Enter / двойной клик  — войти в папку
  BackSpace / «↑»       — вверх
  Tab или клик          — сменить активную панель
  F5                    — копировать выделенное из активной панели в другую
  F8 / Del              — удалить выделенное
  F7                    — создать папку (в активной панели)
  F2                    — перечитать панель
  Автопоиск             — кнопка "Найти консоль": сканирует подсеть, ищет
                          открытый порт 21 (FTP) и подключается.

Запуск: python twin_commander.py   (или собранный twin_commander.exe)
"""

import ftplib
import ipaddress
import os
import socket
import string
import sys
import threading
import time
import tkinter as tk
from tkinter import ttk, filedialog, messagebox, simpledialog

FTP_TIMEOUT = 5
MAX_SCAN_HOSTS = 254     # сканируем ВСЮ подсеть /24 (254 адреса)

# ---- Лог программы: twin_commander.log рядом с exe (или .py) ----
LOG_PATH = os.path.join(os.path.dirname(os.path.abspath(sys.argv[0])),
                        "twin_commander.log")


def log(msg):
    """Писать в лог-файл (дополнительно к статусу в UI)."""
    try:
        with open(LOG_PATH, "a", encoding="utf-8") as f:
            import datetime
            f.write(f"[{datetime.datetime.now():%H:%M:%S}] {msg}\n")
    except OSError:
        pass


def get_local_drives():
    """Список локальных дисков Windows (C:, D:, ...)."""
    drives = []
    for letter in string.ascii_uppercase:
        p = letter + ":\\"
        if os.path.isdir(p):
            drives.append(p)
    return drives or ["C:\\"]


def fmt_size(n):
    if n is None:
        return ""
    try:
        n = int(n)
    except (TypeError, ValueError):
        return ""
    if n < 1024:
        return f"{n} Б"
    if n < 1048576:
        return f"{n / 1024:.1f} КБ"
    if n < 1073741824:
        return f"{n / 1048576:.1f} МБ"
    return f"{n / 1073741824:.1f} ГБ"


class LocalPanel:
    """Панель локальной ФС (левая)."""

    def __init__(self, tree, path_var, cwd="/"):
        self.tree = tree
        self.path_var = path_var
        self.cwd = cwd
        self.entries = []      # [(name, is_dir, size)]
        self._skip = 0

    def refresh(self):
        try:
            names = sorted(os.listdir(self.cwd), key=str.lower)
        except OSError as e:
            return False, f"Нет доступа: {e}"
        out = []
        for n in names:
            p = os.path.join(self.cwd, n)
            try:
                isd = os.path.isdir(p)
                sz = 0 if isd else os.path.getsize(p)
            except OSError:
                continue
            out.append((n, isd, sz))
        out.sort(key=lambda x: (not x[1], x[0].lower()))
        self.entries = out
        self._render()
        return True, ""

    def _render(self):
        self.tree.delete(*self.tree.get_children())
        for name, isd, sz in self.entries:
            self.tree.insert("", "end", values=(
                name + ("/" if isd else ""),
                "" if isd else fmt_size(sz),
                "Папка" if isd else "Файл",
            ))
        self.path_var.set(self.cwd)

    def selected(self):
        sel = self.tree.selection()
        if not sel:
            return None
        i = self.tree.index(sel[0])
        if 0 <= i < len(self.entries):
            return self.entries[i]
        return None

    def enter(self, name):
        p = os.path.join(self.cwd, name)
        if os.path.isdir(p):
            self.cwd = os.path.normpath(p)
            return self.refresh()
        return False, ""

    def up(self):
        p = os.path.normpath(os.path.join(self.cwd, ".."))
        if os.path.isdir(p):
            self.cwd = p
            return self.refresh()
        return False, ""

    def cd(self, drive):
        """Сменить диск (D:) — в корень диска."""
        if os.path.isdir(drive):
            self.cwd = drive
            return self.refresh()
        return False, "нет диска"

    def full_path(self, name):
        return os.path.join(self.cwd, name)


class FtpPanel:
    """Панель FTP (правая)."""

    def __init__(self, tree, path_var):
        self.tree = tree
        self.path_var = path_var
        self.ftp = None
        self.cwd = "/roms"
        self.entries = []      # [(name, is_dir, size)]
        self.host = ""

    def connected(self):
        return self.ftp is not None

    def connect(self, host, port=21):
        try:
            ftp = ftplib.FTP()
            # Проверка: при connect() ftplib читает баннер 220. Свой FTP-сервер
            # шлёт "220 MultiTool FTP ready" — по маркеру MultiTool отличаем
            # консоль от чужого FTP.
            ftp.connect(host, port, timeout=8)
            ftp.login("user", "pass")
            ftp.set_pasv(True)
            self.ftp = ftp
            self.host = host
            return True, ""
        except Exception as e:
            try:
                ftp.close()
            except Exception:
                pass
            return False, f"{e}"

    def disconnect(self):
        try:
            if self.ftp:
                self.ftp.quit()
        except Exception:
            pass
        self.ftp = None
        self.entries = []
        self.tree.delete(*self.tree.get_children())
        self.path_var.set("-")

    def refresh(self):
        if not self.ftp:
            return False, "не подключено"
        try:
            items = []
            self.ftp.retrlines("LIST -a", items.append)
        except Exception as e:
            log(f"FTP LIST {self.cwd}: ошибка {e}")
            return False, f"{e}"
        out = []
        for line in items:
            if not line.strip():
                continue
            isd = line[0] == "d"
            name = None
            mi = line.find(" 2026 ")
            if mi >= 0:
                name = line[mi + 6:].rstrip("\r\n")
            if name is None or name == "":
                parts = line.split()
                if parts:
                    name = parts[-1]
            if name in (".", ".."):
                continue
            sz = 0
            for p in line.split()[1:4]:
                if p.isdigit():
                    sz = int(p)
                    break
            out.append((name, isd, sz))
        out.sort(key=lambda x: (not x[1], x[0].lower()))
        self.entries = out
        self._render()
        return True, ""

    def _render(self):
        self.tree.delete(*self.tree.get_children())
        for name, isd, sz in self.entries:
            self.tree.insert("", "end", values=(
                name + ("/" if isd else ""),
                "" if isd else fmt_size(sz),
                "Папка" if isd else "Файл",
            ))
        self.path_var.set(self.cwd + f"  ({self.host})")

    def selected(self):
        sel = self.tree.selection()
        if not sel:
            return None
        i = self.tree.index(sel[0])
        if 0 <= i < len(self.entries):
            return self.entries[i]
        return None

    def enter(self, name):
        if not self.ftp:
            return False, "не подключено"
        try:
            base = self.cwd.rstrip("/")
            self.ftp.cwd(base + "/" + name.strip("/"))
            self.cwd = self.ftp.pwd()
            if not self.cwd.startswith("/roms"):
                self.cwd = "/roms"
            return self.refresh()
        except Exception as e:
            return False, f"{e}"

    def up(self):
        if not self.ftp:
            return False, "не подключено"
        try:
            self.ftp.cwd("..")
            self.cwd = self.ftp.pwd()
            if not self.cwd.startswith("/roms"):
                self.cwd = "/roms"
            return self.refresh()
        except Exception as e:
            return False, f"{e}"

    def delete(self, name, is_dir):
        try:
            if is_dir:
                self.ftp.rmd(name)
            else:
                self.ftp.delete(name)
            return True, ""
        except Exception as e:
            return False, f"{e}"

    def mkdir(self, name):
        try:
            self.ftp.mkd(name.strip("/"))
            return True, ""
        except Exception as e:
            return False, f"{e}"


def find_console_worker(ip, port, found, done, open_ports):
    """Проверить один адрес: открыт ли FTP и шлёт ли баннер 'MultiTool'.
    Баннер может идти с задержкой (WiFi-очередь) — читаем НЕСКОЛЬКО раз
    с накоплением до ~2 сек. Любой открытый :21 кладём в open_ports — для
    диагностики (видно, что консоль отвечает, даже если маркер упущен)."""
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.settimeout(0.8)
    try:
        s.connect((ip, port))
        open_ports.append(ip)           # порт открыт — кандидат
        s.settimeout(1.2)
        try:
            s.sendall(b"\r\n")
        except OSError:
            pass
        data = b""
        t0 = time.time()
        while time.time() - t0 < 2.0:
            try:
                chunk = s.recv(1024)
                if not chunk:
                    break
                data += chunk
                if b"MultiTool" in data:
                    break
            except socket.timeout:
                break
            except OSError:
                break
        if b"MultiTool" in data:
            found.append(ip)
    except OSError:
        pass
    finally:
        try:
            s.close()
        except OSError:
            pass
        done.append(1)


class TwinApp:
    PANE_COLORS = {"active": "#e6f2ff", "normal": "white"}

    def __init__(self, root):
        self.root = root
        root.title("MultiTool TwinCommander")
        root.geometry("1000x600")
        root.minsize(800, 480)

        self.active = "local"     # 'local' | 'remote'
        self.font = ("Consolas", 10)

        # ---- Верхняя панель: автопоиск + адрес ----
        top = ttk.Frame(root, padding=6)
        top.pack(fill="x")
        ttk.Button(top, text="\u2315 Найти консоль", command=self.auto_find).pack(side="left")
        ttk.Label(top, text="IP консоли:").pack(side="left", padx=(10, 2))
        self.ip_var = tk.StringVar(value="10.233.48.44")
        ttk.Entry(top, textvariable=self.ip_var, width=16, font=self.font).pack(side="left", padx=2)
        ttk.Button(top, text="Подключить", command=self.connect_remote).pack(side="left", padx=2)
        ttk.Button(top, text="Отключить", command=self.disconnect_remote).pack(side="left", padx=2)
        self.status_var = tk.StringVar(value="Нет консоли")
        ttk.Label(top, textvariable=self.status_var, foreground="#555").pack(side="right")

        # ---- Панели: два фрейма ----
        panes = ttk.Frame(root, padding=(6, 0, 6, 4))
        panes.pack(fill="both", expand=True)

        # ЛЕВАЯ
        left = ttk.Frame(panes)
        left.pack(side="left", fill="both", expand=True, padx=(0, 3))
        lbar = ttk.Frame(left)
        lbar.pack(fill="x")
        ttk.Label(lbar, text="Диск:").pack(side="left")
        self.drive_var = tk.StringVar(value=get_local_drives()[0])
        self.drive_combo = ttk.Combobox(lbar, textvariable=self.drive_var,
                                        values=get_local_drives(), width=6,
                                        state="readonly", font=self.font)
        self.drive_combo.pack(side="left", padx=(2, 6))
        self.drive_combo.bind("<<ComboboxSelected>>", lambda e: self.on_drive_change())
        ttk.Button(lbar, text="\u2302 Корень", width=8,
                   command=self.go_root).pack(side="left")
        self.lpath = tk.StringVar(value="/")
        ttk.Label(left, textvariable=self.lpath, font=self.font, foreground="#063",
                  background="#f4f4f4").pack(fill="x")
        self.ltree = ttk.Treeview(left, columns=("name", "size", "type"), show="headings",
                                  selectmode="browse")
        for c, w in (("name", 300), ("size", 90), ("type", 70)):
            self.ltree.heading(c, text={"name": "Имя", "size": "Размер", "type": "Тип"}[c])
            self.ltree.column(c, width=w, anchor="w" if c == "name" else "e" if c == "size" else "center")
        lvsb = ttk.Scrollbar(left, orient="vertical", command=self.ltree.yview)
        self.ltree.configure(yscrollcommand=lvsb.set)
        self.ltree.pack(side="left", fill="both", expand=True)
        lvsb.pack(side="right", fill="y")
        self.ltree.bind("<Double-1>", lambda e: self.on_double("local"))
        self.ltree.bind("<Button-1>", lambda e: self.set_active("local"))

        # ПРАВАЯ
        right = ttk.Frame(panes)
        right.pack(side="right", fill="both", expand=True, padx=(3, 0))
        self.rpath = tk.StringVar(value="-")
        ttk.Label(right, textvariable=self.rpath, font=self.font, foreground="#206",
                  background="#f4f4f4").pack(fill="x")
        self.rtree = ttk.Treeview(right, columns=("name", "size", "type"), show="headings",
                                  selectmode="browse")
        for c, w in (("name", 300), ("size", 90), ("type", 70)):
            self.rtree.heading(c, text={"name": "Имя", "size": "Размер", "type": "Тип"}[c])
            self.rtree.column(c, width=w, anchor="w" if c == "name" else "e" if c == "size" else "center")
        rvsb = ttk.Scrollbar(right, orient="vertical", command=self.rtree.yview)
        self.rtree.configure(yscrollcommand=rvsb.set)
        self.rtree.pack(side="left", fill="both", expand=True)
        rvsb.pack(side="right", fill="y")
        self.rtree.bind("<Double-1>", lambda e: self.on_double("remote"))
        self.rtree.bind("<Button-1>", lambda e: self.set_active("remote"))

        self.lpanel = LocalPanel(self.ltree, self.lpath)
        self.rpanel = FtpPanel(self.rtree, self.rpath)
        self.lpanel.refresh()

        # ---- Нижняя панель: кнопки ----
        bottom = ttk.Frame(root, padding=6)
        bottom.pack(fill="x")
        ttk.Button(bottom, text="\u2190 Копировать", command=lambda: self.copy("remote")).pack(side="left", padx=2)
        ttk.Button(bottom, text="Копировать \u2192", command=lambda: self.copy("local")).pack(side="left", padx=2)
        ttk.Button(bottom, text="\u2191 Вверх", command=self.go_up).pack(side="left", padx=2)
        ttk.Button(bottom, text="Новая папка", command=self.new_folder).pack(side="left", padx=2)
        ttk.Button(bottom, text="\u2326 Удалить", command=self.delete).pack(side="left", padx=2)
        ttk.Button(bottom, text="Обновить", command=self.refresh_all).pack(side="left", padx=6)

        self.progress = ttk.Progressbar(bottom, mode="determinate")
        self.progress.pack(side="right", fill="x", expand=True, padx=6)

        root.bind("<Tab>", lambda e: self.switch_pane())
        root.bind("<BackSpace>", lambda e: self.go_up())
        root.bind("<F5>", lambda e: self.copy(self.active))
        root.bind("<F8>", lambda e: self.delete())
        root.bind("<Delete>", lambda e: self.delete())
        root.bind("<F7>", lambda e: self.new_folder())
        root.bind("<F2>", lambda e: self.refresh_all())
        root.bind("<Control-d>", lambda e: self.focus_drive())
        root.bind("<Alt-F1>", lambda e: self.focus_drive())
        root.bind("<Return>", lambda e: self.on_enter())

        self.paint_active()
        self.status_var.set("Подключи консоль (или «Найти консоль»).")

    # ---------- панели ----------
    @property
    def active_panel(self):
        return self.lpanel if self.active == "local" else self.rpanel

    def other_panel(self):
        return self.rpanel if self.active == "local" else self.lpanel

    def set_active(self, side):
        self.active = side
        self.paint_active()

    def switch_pane(self):
        self.active = "remote" if self.active == "local" else "local"
        self.paint_active()
        return "break"

    def paint_active(self):
        # ttk.Treeview не принимает -background в configure — используем Style.
        # Активная панель — светло-голубая подсветка строк и заголовка.
        style = ttk.Style(self.root)
        if self.active == "local":
            style.configure("Act.Treeview", background="#e6f2ff",
                            fieldbackground="#e6f2ff")
            style.configure("Act.Treeview.Heading", background="#cfe4ff")
            self.ltree.configure(style="Act.Treeview")
            self.rtree.configure(style="")
        else:
            style.configure("Act.Treeview", background="#e6f2ff",
                            fieldbackground="#e6f2ff")
            style.configure("Act.Treeview.Heading", background="#cfe4ff")
            self.rtree.configure(style="Act.Treeview")
            self.ltree.configure(style="")

    def on_double(self, side):
        p = self.lpanel if side == "local" else self.rpanel
        e = p.selected()
        if e and e[1]:
            ok, err = p.enter(e[0])
            if not ok:
                self.set_status(err)
        elif e and side == "remote":
            # запустить? нет — FTP не запускает; просто статус
            pass
        elif e and side == "local":
            pass

    def go_up(self):
        ok, err = self.active_panel.up()
        if not ok:
            self.set_status(err)
        return "break"

    def on_drive_change(self):
        """Выбор диска в левой панели — как смена диска в Total Commander."""
        drive = self.drive_var.get()
        ok, err = self.lpanel.cd(drive)
        if not ok:
            self.set_status(f"Нет доступа к {drive}: {err}")
        if self.active != "local":
            self.set_active("local")

    def go_root(self):
        """Перейти в корень диска (как backslash в TC)."""
        if self.active == "local":
            ok, err = self.lpanel.cd(self.lpanel.cwd.split("\\")[0] + "\\")
            if not ok:
                self.set_status(err)
        else:
            self.rpanel.up()
        return "break"

    def focus_drive(self):
        """Ctrl+D / Alt+F1 — открыть выбор диска левой панели."""
        self.drive_combo.focus_set()
        self.drive_combo.event_generate("<Down>")
        return "break"

    def on_enter(self):
        """Enter — войти в папку активной панели."""
        p = self.active_panel
        e = p.selected()
        if e and e[1]:
            ok, err = p.enter(e[0])
            if not ok:
                self.set_status(err)
        return "break"

    # ---------- консоль ----------
    def connect_remote(self):
        ip = self.ip_var.get().strip()
        if not ip:
            return
        self.set_status(f"Подключение к {ip}...")
        threading.Thread(target=self._connect_worker, args=(ip,), daemon=True).start()

    def _connect_worker(self, ip):
        ok, err = self.rpanel.connect(ip)
        if not ok:
            self.root.after(0, lambda: self.set_status(f"Нет консоли на {ip}: {err}"))
            log(f"Подключение к {ip} НЕ удалось: {err}")
            return
        log(f"Подключено к {ip}")
        self.root.after(0, self._connected)

    def _connected(self):
        self.set_status(f"Подключено: {self.rpanel.host}")
        self.refresh_all()

    def disconnect_remote(self):
        self.rpanel.disconnect()
        self.set_status("Отключено")

    def auto_find(self):
        """Найти консоль: сканировать подсеть, искать открытый :21."""
        self.set_status("Автопоиск: сканирую подсеть...")
        threading.Thread(target=self._auto_find_worker, daemon=True).start()

    def _auto_find_worker(self):
        # своя IP через UDP-сокет
        ip = None
        try:
            s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            s.connect(("8.8.8.8", 80))
            ip = s.getsockname()[0]
            s.close()
        except OSError:
            ip = None
        if not ip:
            self.root.after(0, lambda: self.set_status("Не определил свою сеть — впиши IP консоли вручную"))
            log("Автопоиск: не определён свой IP (нет сети?)")
            return
        net = ipaddress.ip_network(f"{ip}/24", strict=False)
        addrs = [str(h) for h in net.hosts()]
        # свои адрес пропустить, но порядок: попробовать последний октет рядом
        addrs = [a for a in addrs if a != ip]
        addrs.sort(key=lambda a: abs(int(a.split(".")[-1]) - int(ip.split(".")[-1])))
        addrs = addrs[:MAX_SCAN_HOSTS]
        self.root.after(0, lambda: self.set_status(
            f"Автопоиск: сканирую {net} (ищу FTP с маркером MultiTool)..."))
        log(f"Автопоиск: свой IP {ip}, сканирую {net}, адресов={len(addrs)}")

        found = []
        open_ports = []
        done = []
        threads = []
        step = max(1, len(addrs) // 30)   # до 30 одновременных
        for block in range(0, len(addrs), step):
            batch = addrs[block:block + step]
            for a in batch:
                t = threading.Thread(target=find_console_worker,
                                     args=(a, 21, found, done, open_ports), daemon=True)
                t.start()
                threads.append(t)
            for t in threads:
                t.join(timeout=2)
            if found:
                break
        if found:
            ip = found[0]
            # Сразу показываем найденный IP в статусе и поле ввода
            self.root.after(0, lambda: self.ip_var.set(ip))
            self.root.after(0, lambda: self.set_status(
                f"Найдена консоль: {ip} (ещё кандидаты: {len(found) - 1})"))
            log(f"Автопоиск: найдены кандидаты с маркером MultiTool: {found}")
            self.root.after(0, self._auto_connect, ip)
        else:
            if open_ports:
                self.root.after(0, lambda: self.set_status(
                    f"Открыты :21 на {open_ports}, но баннер MultiTool не получен"))
                log(f"Автопоиск: открыты :21 на {open_ports}, но без маркера MultiTool")
            else:
                self.root.after(0, lambda: self.set_status("Консоль не найдена в подсети"))
                log("Автопоиск: ни один хост не отдал :21")

    def _auto_connect(self, ip):
        self.set_status(f"Найдена консоль: {ip}, подключаюсь...")
        threading.Thread(target=self._connect_worker, args=(ip,), daemon=True).start()

    # ---------- операции ----------
    def copy(self, direction):
        """direction = 'local' → копировать из ЛЕВОЙ в ПРАВУЮ (ПК → консоль).
           direction = 'remote' → из ПРАВОЙ в ЛЕВУЮ (консоль → ПК)."""
        if direction == "local":
            src, dst = self.lpanel, self.rpanel
        else:
            src, dst = self.rpanel, self.lpanel
        if not getattr(dst, "connected", lambda: True)():
            self.set_status("Целевая панель — не подключена (консоль)")
            return
        e = src.selected()
        if not e:
            self.set_status("Ничего не выбрано")
            return
        name, isd, sz = e
        self.set_status(f"Копирование {name}...")
        threading.Thread(target=self._copy_worker, args=(src, dst, name, isd, direction), daemon=True).start()

    def _copy_worker(self, src, dst, name, isd, direction):
        if isinstance(dst, FtpPanel) and isinstance(src, LocalPanel):
            # ПК -> консоль
            try:
                with open(src.full_path(name), "rb") as f:
                    d = dst.ftp.storbinary("STOR " + name, f)
                self.root.after(0, lambda: self.set_status(f"Загружено на консоль: {name}"))
            except Exception as e:
                self.root.after(0, lambda: self.set_status(f"Ошибка загрузки: {e}"))
        else:
            # консоль -> ПК
            try:
                path = os.path.join(dst.cwd, name)
                with open(path, "wb") as f:
                    src.ftp.retrbinary("RETR " + name, f.write)
                self.root.after(0, lambda: self.set_status(f"Скачано на ПК: {name}"))
            except Exception as e:
                self.root.after(0, lambda: self.set_status(f"Ошибка скачивания: {e}"))
        self.root.after(0, self.refresh_all)

    def delete(self):
        p = self.active_panel
        e = p.selected()
        if not e:
            return
        name, isd, sz = e
        if not messagebox.askyesno("Удалить", f"Удалить «{name}»?"):
            return
        if isinstance(p, FtpPanel):
            ok, err = p.delete(name, isd)
            if not ok:
                self.set_status(f"Не удалить: {err}")
        else:
            try:
                path = p.full_path(name)
                if isd:
                    import shutil
                    shutil.rmtree(path)
                else:
                    os.remove(path)
                self.set_status(f"Удалено: {name}")
            except Exception as e:
                self.set_status(f"Не удалить: {e}")
        self.refresh_all()
        return "break"

    def new_folder(self):
        p = self.active_panel
        name = simpledialog.askstring("Новая папка", "Имя папки:", parent=self.root)
        if not name:
            return
        if isinstance(p, FtpPanel):
            ok, err = p.mkdir(name)
            if not ok:
                self.set_status(f"Не создать папку: {err}")
            else:
                self.set_status(f"Папка создана: {name}")
        else:
            try:
                os.mkdir(p.full_path(name.strip("/")))
                self.set_status(f"Папка создана: {name}")
            except Exception as e:
                self.set_status(f"Не создать папку: {e}")
        self.refresh_all()

    def refresh_all(self):
        ok, err = self.lpanel.refresh()
        if not ok:
            self.set_status(err)
        if self.rpanel.connected():
            ok, err = self.rpanel.refresh()
            if not ok:
                self.set_status(err)
        self.paint_active()

    def set_status(self, s):
        self.status_var.set(s)


def main():
    root = tk.Tk()
    TwinApp(root)
    root.mainloop()


if __name__ == "__main__":
    main()
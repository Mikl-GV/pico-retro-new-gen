#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
MultiTool FTP Client — простая программа для обмена файлами с консолью
(pico-retro-new-gen, FTP-сервер на H3).

Окно: поле IP/порт, кнопка "Подключить", список файлов/папок (двойной клик
по папке — вход), кнопки:
  - "Скачать"     — сохранить выделенный файл с консоли на ПК
  - "Загрузить"   — залить файл с ПК на консоль (в текущую папку)
  - "Новая папка" — создать папку в текущей
  - "Удалить"     — удалить выделенный файл/папку
  - "Вверх"       — перейти в родительскую папку

Работает по обычному FTP (ftplib), сервер — net_ftp.c на консоли.
Запуск:  python ftp_client.py   (или собранный ftp_client.exe)
"""

import ftplib
import os
import sys
import threading
import tkinter as tk
from tkinter import ttk, filedialog, messagebox


class FtpClientApp:
    def __init__(self, root):
        self.root = root
        self.root.title("MultiTool FTP — передача файлов")
        self.root.geometry("820x520")
        self.root.minsize(640, 400)

        self.ftp = None
        self.cwd = "/roms"
        self.entries = []          # [(name, is_dir, size)]
        self.font = ("Consolas", 10)

        # ---- Верхняя панель: адрес ----
        top = ttk.Frame(root, padding=6)
        top.pack(fill="x")
        ttk.Label(top, text="IP:").pack(side="left")
        self.ip_var = tk.StringVar(value="10.233.48.182")
        e_ip = ttk.Entry(top, textvariable=self.ip_var, width=16, font=self.font)
        e_ip.pack(side="left", padx=(4, 8))
        ttk.Label(top, text="Порт:").pack(side="left")
        self.port_var = tk.StringVar(value="21")
        e_port = ttk.Entry(top, textvariable=self.port_var, width=5, font=self.font)
        e_port.pack(side="left", padx=(4, 8))
        self.btn_conn = ttk.Button(top, text="Подключить", command=self.toggle_connect)
        self.btn_conn.pack(side="left")
        self.status_var = tk.StringVar(value="Не подключено")
        ttk.Label(top, textvariable=self.status_var, foreground="#555").pack(side="right")

        # ---- Панель пути ----
        bar = ttk.Frame(root, padding=(6, 0, 6, 4))
        bar.pack(fill="x")
        ttk.Button(bar, text="\u2191 Вверх", command=self.go_up, width=10).pack(side="left")
        self.path_var = tk.StringVar(value="/roms")
        ttk.Label(bar, textvariable=self.path_var, font=self.font,
                  foreground="#063").pack(side="left", padx=8)

        # ---- Список файлов ----
        mid = ttk.Frame(root)
        mid.pack(fill="both", expand=True, padx=6, pady=2)
        cols = ("name", "size", "type")
        self.tree = ttk.Treeview(mid, columns=cols, show="headings", selectmode="browse")
        self.tree.heading("name", text="Имя")
        self.tree.heading("size", text="Размер")
        self.tree.heading("type", text="Тип")
        self.tree.column("name", width=520)
        self.tree.column("size", width=110, anchor="e")
        self.tree.column("type", width=90, anchor="center")
        vsb = ttk.Scrollbar(mid, orient="vertical", command=self.tree.yview)
        self.tree.configure(yscrollcommand=vsb.set)
        self.tree.pack(side="left", fill="both", expand=True)
        vsb.pack(side="right", fill="y")
        self.tree.bind("<Double-1>", self.on_double_click)

        # ---- Нижняя панель: операции ----
        bottom = ttk.Frame(root, padding=6)
        bottom.pack(fill="x")
        ttk.Button(bottom, text="Скачать", command=self.download).pack(side="left", padx=2)
        ttk.Button(bottom, text="Загрузить", command=self.upload).pack(side="left", padx=2)
        ttk.Button(bottom, text="Новая папка", command=self.mkdir).pack(side="left", padx=2)
        ttk.Button(bottom, text="Удалить", command=self.delete).pack(side="left", padx=2)
        ttk.Button(bottom, text="Обновить", command=self.refresh).pack(side="left", padx=6)

        self.progress = ttk.Progressbar(bottom, mode="determinate")
        self.progress.pack(side="right", fill="x", expand=True, padx=6)

    # ---------- подключение ----------
    def toggle_connect(self):
        if self.ftp:
            self.disconnect()
            return
        ip = self.ip_var.get().strip()
        port = int(self.port_var.get().strip() or "21")
        self.set_status("Подключение...")
        threading.Thread(target=self._connect_worker, args=(ip, port), daemon=True).start()

    def _connect_worker(self, ip, port):
        try:
            ftp = ftplib.FTP()
            ftp.connect(ip, port, timeout=8)
            ftp.login("user", "pass")
            ftp.set_pasv(True)
            # проверить рабочесть: попробовать прислать PWD
            try:
                ftp.sendcmd("PWD")
            except Exception:
                pass
            self.ftp = ftp
            self.root.after(0, self._connected_ok)
        except Exception as e:
            self.root.after(0, lambda: self.set_status(f"Ошибка: {e}"))

    def _connected_ok(self):
        self.btn_conn.config(text="Отключить")
        self.set_status("Подключено")
        self.refresh()

    def disconnect(self):
        try:
            if self.ftp:
                self.ftp.quit()
        except Exception:
            pass
        self.ftp = None
        self.btn_conn.config(text="Подключить")
        self.set_status("Не подключено")
        self.tree.delete(*self.tree.get_children())

    # ---------- навигация ----------
    def refresh(self):
        if not self.ftp:
            return
        threading.Thread(target=self._refresh_worker, daemon=True).start()

    def _refresh_worker(self):
        try:
            items = []
            self.ftp.retrlines("LIST -a", items.append)
            parsed = []
            for line in items:
                if not line.strip():
                    continue
                is_dir = line[0] == "d"
                # наш сервер: "d rw------- 0 Jan  1  2026 NAME"
                # имя = всё после маркера " 2026 " (года в дате листинга)
                name = None
                mark = " 2026 "
                mi = line.find(mark)
                if mi >= 0:
                    name = line[mi + len(mark):].rstrip("\r\n")
                if name is None or name == "":
                    # fallback: последнее поле
                    parts = line.split()
                    if parts:
                        name = parts[-1]
                if name in (".", ".."):
                    continue
                size = 0
                # размер — первое число среди первых полей
                for p in line.split()[1:4]:
                    if p.isdigit():
                        size = int(p)
                        break
                parsed.append((name, is_dir, size))
            parsed.sort(key=lambda x: (not x[1], x[0].lower()))
            self.root.after(0, self._render, parsed)
        except Exception as e:
            self.root.after(0, lambda: self.set_status(f"Ошибка списка: {e}"))

    def _render(self, parsed):
        self.tree.delete(*self.tree.get_children())
        self.entries = parsed
        for name, is_dir, size in parsed:
            tag = "dir" if is_dir else "file"
            self.tree.insert("", "end", values=(
                name + ("/" if is_dir else ""),
                "" if is_dir else self.fmt_size(size),
                "Папка" if is_dir else "Файл",
            ), tags=(tag,))
        self.path_var.set(self.cwd)

    @staticmethod
    def fmt_size(n):
        if n < 1024:
            return f"{n} Б"
        if n < 1024 * 1024:
            return f"{n / 1024:.1f} КБ"
        return f"{n / 1048576:.1f} МБ"

    def selected(self):
        sel = self.tree.selection()
        if not sel:
            return None
        idx = self.tree.index(sel[0])
        if 0 <= idx < len(self.entries):
            return self.entries[idx]
        return None

    def on_double_click(self, _evt):
        e = self.selected()
        if e and e[1]:
            self.enter_dir(e[0])

    def enter_dir(self, name):
        if not self.ftp:
            return
        try:
            self.ftp.cwd("/" + name.strip("/") if self.cwd in ("", "/") else self.cwd.rstrip("/") + "/" + name.strip("/"))
            self.cwd = self.ftp.pwd()
            self.refresh()
        except Exception as e:
            self.set_status(f"Не могу войти в папку: {e}")

    def go_up(self):
        if not self.ftp:
            return
        try:
            self.ftp.cwd("..")
            self.cwd = self.ftp.pwd()
            if not self.cwd.startswith("/roms"):
                self.cwd = "/roms"
            self.refresh()
        except Exception as e:
            self.set_status(f"Не могу подняться: {e}")

    # ---------- операции ----------
    def download(self):
        e = self.selected()
        if not e:
            return
        if e[1]:
            messagebox.showinfo("MultiTool", "Выберите файл (в папки — двойной клик).")
            return
        path = filedialog.asksaveasfilename(initialfile=e[0], title="Сохранить файл")
        if not path:
            return
        self.set_status(f"Скачивание: {e[0]}...")
        threading.Thread(target=self._download_worker, args=(e[0], path), daemon=True).start()

    def _download_worker(self, remote, local):
        try:
            with open(local, "wb") as f:
                self.ftp.retrbinary("RETR " + remote, f.write)
            self.root.after(0, lambda: self.set_status(f"Скачано: {remote}"))
        except Exception as e:
            self.root.after(0, lambda: self.set_status(f"Ошибка скачивания: {e}"))

    def upload(self):
        if not self.ftp:
            return
        path = filedialog.askopenfilename(title="Выбрать файл для загрузки")
        if not path:
            return
        name = os.path.basename(path)
        self.set_status(f"Загрузка: {name}...")
        threading.Thread(target=self._upload_worker, args=(path, name), daemon=True).start()

    def _upload_worker(self, local, remote):
        try:
            with open(local, "rb") as f:
                self.ftp.storbinary("STOR " + remote, f)
            self.root.after(0, self._upload_done, remote)
        except Exception as e:
            self.root.after(0, lambda: self.set_status(f"Ошибка загрузки: {e}"))

    def _upload_done(self, remote):
        self.set_status(f"Загружено: {remote}")
        self.refresh()

    def mkdir(self):
        if not self.ftp:
            return
        name = tk.simpledialog.askstring("Новая папка", "Имя папки:")
        if not name:
            return
        try:
            self.ftp.mkd(name.strip("/"))
            self.refresh()
        except Exception as e:
            self.set_status(f"Не создать папку: {e}")

    def delete(self):
        e = self.selected()
        if not e:
            return
        if not messagebox.askyesno("MultiTool", f"Удалить «{e[0]}»?"):
            return
        try:
            if e[1]:
                self.ftp.rmd(e[0])
            else:
                self.ftp.delete(e[0])
            self.refresh()
            self.set_status(f"Удалено: {e[0]}")
        except Exception as e:
            self.set_status(f"Не удалить: {e}")

    # ---------- утилиты ----------
    def set_status(self, s):
        self.status_var.set(s)


def main():
    root = tk.Tk()
    FtpClientApp(root)
    root.mainloop()


if __name__ == "__main__":
    main()
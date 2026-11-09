# МАППИНГ КНОПОК (r731)

Справочник соответствий **кнопка → устройство → действие** по всем эмуляторам.
Источник — код: `h3_bare/cores/remap.c` (клавиатура, дефолт), `h3_bare/cores/btn_pad.c`
(плата 8 кнопок, I2C 0x27), `h3_bare/cores/sega_pad.c` (джойстик 6-btn, I2C 0x20)
и host-слои (`*_host.*`, `system_*.cpp`).

Править по желанию → потом привести код в соответствие.

---

## 1. Устройства

| Устройство | Шина | Адрес | Кнопки | Примечание |
|---|---|---|---|---|
| **Sega-джойстик 6-btn** | I2C (bit-bang PG9/PG8) | **0x20** | D-Pad, A B C X Y Z, Start, Mode | Протокол 6-btn: крестовина + B/C, фаза 6 → Z/Y/X/Mode. НЕ ЛЕЗТЬ (sega_pad.c) |
| **Плата 8 кнопок (денди)** | I2C (bit-bang PG9/PG8) | **0x27** | Up/Left/Right/Down, A, B, Start, Select | Read-only скан (btn_pad.c). Биты см. ниже |
| **Клавиатура USB** | USB | — | HID | Ремап через Settings → Keyboard remap (/retro.cfg) |

> Два I2C-устройства на разных адресах не конфликтуют: `pad_scan_combined()`
> сканирует джой (0x20) и плату (0x27) раздельно.

---

## 2. Кнопочная плата (8 кнопок, I2C 0x27) — как «обычная денди»

| Кнопка | Линия (активный 0) | Бит маски |
|---|---|---|
| Up | B0 | 0x0001 |
| Down | B3 | 0x0002 |
| Left | B1 | 0x0004 |
| Right | B2 | 0x0008 |
| A | B4 | 0x0010 |
| B | B5 | 0x0020 |
| Start | B6 | 0x0080 |
| **Select (Coin)** | B7 | **0x0800** (r731: раньше было 0x0100=X, Select не работал) |

Биты совпадают с джойстиком 6-btn (UP/DOWN/LEFT/RIGHT/A/B/START + Mode=Select),
поэтому в играх плата работает как 8-кнопочный «денди-пад».

---

## 3. Sega-джойстик 6-btn (I2C 0x20) — назначение по системам

| Система | A | B | C | X | Y | Z | Start | Mode |
|---|---|---|---|---|---|---|---|---|
| **Mega Drive** | A | B | C | X | Y | Z | Start | Mode |
| **SMS / GG** | Btn1 | Btn2 | — | — | — | — | Start (=Pause/NMI) | — |
| **SNES** | A | B | L | X | Y | R | Start | Select |
| **NES / Famicom** | A | B | — | — | — | — | Start | Select |
| **Game Boy / GBC** | A | B | — | — | — | — | Start | Select |
| **GBA** | A | B | L | R | — | — | Start | Select |
| **Lynx** | A | B | — | Opt1 | Opt2 | — | Pause | — |
| **NGP / NGPC** | A | B | — | — | — | — | Start | Option(Select) |
| **Atari 2600** | Fire | Fire | — | — | — | — | Reset | Select |
| **Atari 5200** | Fire | **Pause** | — | — | — | — | Start | # |
| **Atari 7800** | B1 | B2 | — | — | — | Pause | Start | Select |
| **Vectrex** | 1 | 2 | — | 3 | 4 | — | — | — |
| **ColecoVision** | L | R | — | — | — | — | Start(8) | # |
| **PCE / TurboGrafx** | I | II | — | Select | — | — | Run | — |
| **CPS1 / аркады** | Attack | Jump | Fire3 | Coin | Kick1 | Kick2 | Start | — |

---

## 4. Клавиатура USB — дефолтные раскладки (меняются в Settings → Keyboard remap)

| Система | A | B | C | X | Y | Z | Start | Select/Mode | Доп. |
|---|---|---|---|---|---|---|---|---|---|
| **MD** | Z | X | C | A | S | D | Enter | Q=Mode | — |
| **SMS** | Z | X | — | — | — | — | Enter | — | S=Pause |
| **GG** | Z | X | — | — | — | — | Enter | — | S=Pause |
| **SNES** | A | Z | — | S | X | — | Enter | Space | L=Q, R=W |
| **NES** | Z | X | — | — | — | — | Enter | S | — |
| **GB** | X | Z | — | — | — | — | Enter | S | — |
| **GBA** | Z | X | — | — | — | — | Enter | S | L=Q, R=W |
| **Lynx** | Z | X | — | — | — | — | Enter(Opt2) | S(Opt1) | P=Pause |
| **NGP** | Z | X | — | — | — | — | Enter | S | — |
| **A2600** | Z=Fire | — | — | — | — | — | Enter=Reset | S=Select | Q=Diff |
| **A5200** | Z=Fire | — | — | — | — | — | Enter=Start | S=Key3 | P=Pause, X=Fire2 |
| **A7800** | Z=B1 | X=B2 | — | — | — | — | Enter | S | P=Pause |
| **Vectrex** | Z=1 | X=2 | C=3 | V=4 | — | — | — | — | — |
| **Coleco** | Z=L | X=R | — | — | — | — | Enter=8 | S=# | — |
| **PCE** | Z=I | X=II | — | — | — | — | Enter=Run | S=Sel | — |
| **CPS1** | Z=Att | X=Jump | C=F3 | — | — | — | Enter | S=Coin | 5 / Numpad5 = Coin |

Коды HID: Z=29 X=27 C=6 A=4 S=22 D=7 Q=20 W=26 V=25 P=19 Enter=40 Space=44
стрелки: Up=82 Down=81 Left=80 Right=79.

---

## 5. Меню (HDMI) — назначение кнопок

| Кнопка | Действие в меню |
|---|---|
| D-Pad / стрелки | навигация (+автоповтор) |
| A (0x0010) | Enter (открыть) |
| Start (0x0080) | Enter (открыть) |
| B (0x0020) | ESC (назад) |
| **Select / Mode (0x0800)** | «S» (в браузере — читы) |
| Клавиатура | как обычно |

---

## 6. Выход из эмулятора

| Устройство | Комбинация | Задержка |
|---|---|---|
| Клавиатура | ESC (удержание) | ~0.9 с (или ESC×3) |
| Плата / джой | **Start + Select(/Mode)** (0x80 + 0x800) | ~0.9–1 с удержания |

> Внимание: одиночный Start НЕ выходит (в играх это игровая кнопка).
> Выход — только Start+Select одновременно и удержание.

---

## 7. Известные «странности» (на заметку, можно поправить)

1. **A5200**: на плате/джое **B = Pause** (не Fire2). Клавиатурный Fire2 = X. Если хочется B=Fire2 — поменять в `system_a5200_h3.cpp`.
2. **NES/A2600/Vectrex/Coleco/PCE/SMS/GG**: кнопки C/Y/Z на джое в этих системах не задействованы.
3. **Lynx**: Mode (0x800) на джое не задействован; Opt1/Opt2 — на X/Y.
4. **GB**: клавиатура A=X/B=Z, а на джое/плате A=A/B=B — поведение A/B на разных устройствах различается.
5. **CPS1**: Mode (0x800, плата) = Coin; на джое Coin — X.
# Управление (Controls)

Единый маппинг клавиатуры для всех эмуляторов.  
USB-клавиатура (Boot HID, Logitech 046D:C52B и аналоги).

## Меню (глобальное)

| Действие | Клавиша |
|----------|---------|
| Вверх / Вниз | ↑ / ↓ (удержание = автоповтор) |
| Открыть систему / запустить ROM | Enter |
| Назад / выйти из эмулятора | ESC |
| Settings | активировать из меню → Enter |

## Settings

| Кнопка | Действие |
|--------|----------|
| 1 | Создать папки ROM на SD |
| 2 | Input Test (тест кнопок) |
| 3 | Video Mode / Throttle: 60, 50, 45, 40, 35, 30 Hz (←/→ или Enter — циклически) |
| 4 | Atari 2600 Difficulty: Novice / Expert |
| 5 | ROM partition info (справка по разметке SD) |

## Эмуляторы (игровой маппинг)

| Клавиша | NES | A2600 (MCUME) | A5200 | A7800 | SMS | Game Boy | Lynx | NGP | Mega Drive | SNES |
|---------|:---:|:-------------:|:-----:|:-----:|:---:|:--------:|:----:|:---:|:----------:|:----:|
| ↑ ↓ ← → | D-Pad | D-Pad | D-Pad | D-Pad | D-Pad | D-Pad | D-Pad | D-Pad | D-Pad | D-Pad |
| Z | A (огонь) | Fire | Fire | B1 (A) | Button 1 | B | A | **A** | **A** | **B** |
| X | B | — | Pause | B2 (B) | Button 2 | A | B | **B** | **B** | **Y** |
| C | — | — | — | — | — | — | — | — | **C** | — |
| A | — | — | — | — | — | — | — | — | **X** | **A** |
| S | Select | Select | Start | Select | Pause | Select | Option 1 | **Select** | **Y** | **X** |
| D | — | — | — | — | — | — | — | — | **Z** | — |
| Q | — | — | — | — | — | — | — | — | Mode | **L** |
| W | — | — | — | — | — | — | — | — | — | **R** |
| Space | — | — | — | — | — | — | — | — | — | Select |
| Enter | Start | Game Reset | Key 3 | Start | **Start** | Start | Option 2 | **Start** | Start | Start |
| ESC (удерж. ~1с) | Выход¹ | Выход | Выход | Выход | Выход | Выход | Выход | Выход | Выход | Выход |
 
¹ NES/Dendy: одиночный ESC → клавиша SuborKB (Break в Basic), выход только по удержанию ~1с.

Удерживание ESC ~1 сек — выход в меню.  
В A2600 также работает сложность (Settings → 4).  
A2600: Z=Fire, S=Select, Enter=Game Reset (запуск игры заново).  
Mega Drive (6-кнопочный геймпад): Z=A, X=B, C=C, A=X, S=Y, D=Z, Q=Mode, Enter=Start.  
SMS/GG: Z=Button1, X=Button2, S=Pause (кнопка на корпусе), Enter=Start.  
A5200: Z=Fire, X=Pause, S=Start.
Lynx: Z=A, X=B, S=Option 1, Enter=Option 2, стрелки — D-Pad.
NGP (Neo Geo Pocket / Color): Z=A, X=B, S=Select, Enter=Start, стрелки — D-Pad.
NES/Dendy: одиночный ESC = клавиша SuborKB (Esc/Break), выход в меню — только по удержанию ~1с.
SNES: Z=B, X=Y, A=A, S=X, Q=L, W=R, Space=Select, Enter=Start.

## Atari Portfolio (полная клавиатура)

Portfolio — полноценный компьютер DIP DOS, **игровой маппинг отсутствует**.  
Управление — как на настоящей клавиатуре:

| Клавиша | Действие в Portfolio |
|---------|---------------------|
| A-Z, 0-9 | Ввод текста |
| Shift + буква | Заглавные / символы |
| Enter | Enter / выполнение команды |
| Backspace | Удалить символ слева |
| Стрелки | Курсор (в DOS) / навигация по VK |
| Insert | Открыть/закрыть экранную клавиатуру (VK) |
| ESC (удержание ~1 сек) | Выход в меню |
| F1-F12 | Не используются |

Команды DIP DOS:
- `PIN` + Enter — анимация «взлома» (Terminator 2 style)
- `APPS` + Enter — меню встроенных приложений (калькулятор, редактор, таблицы, контакты, дневник)
- `HELP` + Enter — справка по DIP DOS
- `EXIT` + Enter — выход в меню (альтернатива удержанию ESC)

### Экранная клавиатура (VK)

Insert → открыть VK. Стрелки + Enter → выбор символа.  
Esc → закрыть VK. Shift/Caps → переключение регистра.

## Подсказка на экране (Portfolio)

Внизу экрана всегда отображается:
```
PIN APPS HELP EXIT  INS:VK  ESC(hold):menu
```
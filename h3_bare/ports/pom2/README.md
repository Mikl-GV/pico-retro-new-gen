# POM2 — ядро Apple II для MultiTool

Срез ядра [POM2](https://github.com/habib256/pom2) (Apple ][, ][+, //e, //c),
портируется на bare-metal H3 вместо обвязки вокруг ядра эмулятора «Агат».

## Источник

- Репозиторий: `https://github.com/habib256/pom2`
- Коммит: `40aeb8ed379ee27d1629b7a06518aee9cc3b3023` (2026-11-16)
- Лицензия: **GPL-3.0**, полный текст — `COPYING`. Файлы в `src/` сохраняют
  авторские шапки; прошивка, включающая этот каталог, является производной
  работой и распространяется на условиях GPL-3.0.

## Что здесь есть

Только машинная часть: `src/` — 99 файлов (15 `.cpp` + 84 `.h`, 116 290 строк),
транзитивное замыкание заголовков от:

| Файл | Назначение |
| --- | --- |
| `M6502.cpp`, `M6502_Undoc.cpp` | CPU 6502/65C02, недокументированные опкоды |
| `Memory.cpp`, `MemoryLanguageCard.cpp` | адресное пространство, языковая карта |
| `Apple2Display.cpp`, `Apple2Display_Beam.cpp`, `AppleWinNtsc.cpp` | видео: текст, HGR/DHGR, NTSC |
| `DiskIICard.cpp`, `DiskIICard_Cycles.cpp`, `DiskIICard_Sequencer.cpp` | контроллер дисковода |
| `DiskImage.cpp` | образы `.dsk/.do/.po/.nib/.woz/.2mg` |
| `Keyboard.cpp`, `SlotBus.cpp`, `SystemProfile.cpp`, `SlotCardFactory.cpp` | ввод, слоты, профили |

Вне среза намеренно: ImGui-фронтенд, `MainWindow*`, сеть и транспорты
(`SuperSerialTcpTransport`, FujiNet, TNFS), принтеры, CP/M-карты, снапшоты
сессий, AI-сервер.

## Сборка

```
make pom2-port        # → build/libpom2port.a (15 объектов, ~752 КБ)
```

Цель **не входит** в `OBJ` и в прошивку пока не линкуется: подключение к слою
Apple II — отдельный шаг (`docs/HANDOVER.md`). Так сборка прошивки остаётся
зелёной, а адреса USB-символов на стенде не сдвигаются.

Флаги — `POM2_PORT_CXXFLAGS` в `Makefile`: `-std=gnu++17`, `-fno-exceptions`,
`-fno-rtti`, `-fno-threadsafe-statics`, `-mfloat-abi=softfp`.
**`-ffreestanding` здесь запрещён**: он выставляет `__STDC_HOSTED__=0`,
libstdc++ уходит в freestanding-режим и перестаёт объявлять
`__throw_length_error`/`__throw_bad_alloc` — сборка падает на первом
`std::vector::resize`.

## Локальные отклонения от апстрима

1. `src/MediaAutosave.h` — убран единственный `try/catch` в `start()`:
   проект собирается с `-fno-exceptions`. Отказ исполнителя обрабатывается
   через `refuse()` по пустому результату.
2. `shim/mutex` — собственная реализация `std::mutex`, `std::recursive_mutex`,
   `std::lock_guard`, `std::unique_lock`, `std::once_flag`, `std::call_once`.
   В libstdc++ для `arm-none-eabi` `std::mutex` и `std::thread` отсутствуют
   (библиотека собрана без gthreads), а ядро POM2 однопоточное на core0 —
   блокировки не нужны, но интерфейс должен компилироваться.

`std::filesystem` в libstdc++ есть (636 символов) и линкуется, но на железе
файловые вызовы возвращают ошибку: реальное чтение ROM и образов идёт через
SD-слой MultiTool, а не через `<filesystem>`.

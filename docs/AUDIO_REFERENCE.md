# AUDIO REFERENCE — как делать I2S-звук на Allwinner H3 (пособие)

Собрано по рабочей реализации + даташиту H3. Используется для нашего
`h3_bare/cores/i2s.c` (I2S0, 48 кГц, CPU2 доливает TX FIFO из ring).

## 1. Главный референс (рабочий CPU→FIFO на H3)
- `uli/allwinner-bare-metal` — https://github.com/uli/allwinner-bare-metal/blob/jh/audio_i2s.c
  (+ `audio.h`, `ccu.h`). Тот же SoC, CPU (не DMA) наливает TX FIFO по опросу
  `FIFO_STA[23:16]`. Подтверждает: `TX_DRQ` для CPU-долива НЕ нужен.

### Рабочая инициализация (эталонные значения)
```c
I2S_TX0CHSEL = OFFSET_1|CHSEL_2|CHEN_0|CHEN_1;          // 0x00001031
I2S_TX0CHMAP = CH0_SAMPLE(0)|CH1_SAMPLE(1);
I2S_CLKD     = MCLKDIV(1)|BCLKDIV(5)|MCLKO_EN;          // BCLK=/8, MCLKO_EN=бит8 (H3)
I2S_FAT1     = 0;
I2S_FAT0     = SW(3)|SR(3)|LRCK_PERIOD(31);             // 0x1F33
I2S_TXCHCFG  = TX_SLOT_NUM(1)|RX_SLOT_NUM(1);
I2S_FCTL     = TXTL(0x40)|RXTL(0x0f)|TXIM(1)|RXOM(1)|FRX;  // FTX в цикле НЕ трогать
I2S_TXCNT    = 0;
I2S_INT      = TXEI_EN;                                 // бит4; TX_DRQ(бит7) не включать
I2S_CTL      = BCLK_OUT|LRCK_OUT|MODE_SEL(1)|RXEN|TXEN|GEN|SDO0_EN; // 0x00060117
```
Клоки: `PLL_AUDIO_CTRL = 0x80035514` (EN + N=86,M=21,P=4 → 24.571 МГц), ждать
бит28 (LOCK); `I2S_PCM_CLK = 0x80030000` (gate+mux); `BUS_SOFT_RST3` assert→deassert;
`BUS_CLK_GATING2`. `mod_clk` (I2SPCM0_CLK, 0x0B0) должна быть ≈24.576 МГц.

### Рабочий долив
```c
while (((I2S_FSTA >> 16) & 0xff) > 2) {   // [23:16] = сколько СВОБОДНЫХ слов
    I2S_TXFIFO = l;                        // 0x20
    I2S_TXFIFO = r;
}
```

## 2. Регистры (даташит H3, стр. 534/536)
- `I2S0_BASE = 0x01C22000`; `CTL 0x00`, `FMT0 0x04`, `FMT1 0x08`, `ISTA 0x0C`,
  `RXFIFO 0x10`, `FCTL 0x14`, `FSTA 0x18`, `INT 0x1C`, `TXFIFO 0x20`,
  `CLKD 0x24`, `TXCNT 0x28`, `CHCFG 0x30`, `TX0CHSEL 0x34`, `TX0CHMAP 0x44`.
- `FSTA (0x18)`, default `0x10800000`: `[23:16]=TXE_CNT` «Empty Space Word Counter»
  (при пустом = **128** слов = глубина FIFO), `[28]=TXE` (есть место ≥1 слово).
- `FCTL (0x14)`: `FLUSH_TX=бит25`, `FLUSH_RX=бит24` (**R/WAC — самоочищаются**),
  `TXTL=[18:12]` (деф. 0x40), `TXIM=бит2`.
- `INT (0x1C)`: `TX_DRQ_EN=бит7`, `TXEI_EN=бит4`.
- `CLKD (0x24)`: `BCLKDIV=[7:4]`, `MCLKDIV=[3:0]`, `MCLKO_EN` = **бит8** (H3);
  `0` в BCLKDIV/MCLKDIV = **Reserved → нет BCLK → FIFO не сливается**.
- `FMT0 (0x04)`: `SR=[6:4]` (3=16-бит), `SW=[2:0]` (7=32-бит слот),
  `LRCK_PERIOD=[17:8]` (пишется как `period-1`).
- `PLL_AUDIO (CCU 0x008)`, стр.93: `PLL_AUDIO = (24MHz*N)/(M*P)`;
  поля `N=[14:8]`(фактор+1), `M=[4:0]`(фактор+1), `P=[19:16]`(фактор+1),
  `EN=бит31`, `LOCK=бит28`. **24.576 МГц:** `0x80035514` (N=86,M=21,P=4 →24.571).

## 3. Чек-лист bring-up (по симптому «FIFO 128 и не сливается»)
1. Прочитать `FSTA` → `0x10800000`. Если `0` — блок не тактируется/гейт выключен.
2. Записать 4 слова в `TXFIFO` → перечитать `FSTA`: уменьшилось `TXE_CNT` → записи
   доходят (искать дренаж); не изменилось → неверный оффсет/база/гейт
   (ловушка вендорского BSP: `TXFIFO=0x0C` вместо `0x20` для H3).
3. Тактирование: PLL_AUDIO lock; `I2SPCM0_CLK`≈24.576 МГц; `BCLKDIV≠0`,`MCLKDIV≠0`,
   `MCLKO_EN=бит8`; проверить реальную частоту (осциллограф BCLK/LRCK).
4. Включение: `CTL=GEN|TXEN|BCLK_OUT|LRCK_OUT|SDO0_EN`; `0x34` CHEN+CHSEL=2;
   `0x30` TX_SLOT_NUM=1; `FMT0[17:8]=31`.
5. Дренаж: залить 64 слова, перестать писать, за 1 мс `TXE_CNT` должен вырасти
   ~на 96 (≈1 слово/10.4 мкс при 48k). Не выросло — нет такта/включения.
6. Порядок: формат/каналы → flush → TXEN → GEN последним; `FCTL` в цикле не трогать.
7. `TX_DRQ` для CPU-пути не нужен; `TXIM` — только формат данных.

## 4. Вторичные референсы
- U-Boot `drivers/sound/rockchip_i2s.c` — «CPU опрашивает FIFO и наливает» на
  другом железе: `if ((fifolr&0x3f)<0x20) write(txdr)`. Читает УРОВЕНЬ, у нас —
  СВОБОДНОЕ место (не путать семантику).
- `rsta2/circle` `lib/sound/i2ssoundbasedevice.cpp` — паттерн
  producer→очередь чанков→потребитель-устройство; init: выключить→`TXCLR|RXCLR`
  +задержка→каналы→MODE→STBY→EN.
- Linux `sun4i-i2s.c` — `FLUSH_TX=BIT(25)`, `TX_DRQ_EN=BIT(7)`, H3-квирки
  (`MCLKO_EN`=бит8, `LRCK_PERIOD[17:8]`, `mod_clk=24576000`).
- Вендорский BSP `friendlyarm/h3_lichee` `sunxi-i2s0.c` — эталонный порядок
  включения для H3 (и ловушка `TXFIFO=0x0C`).

## 5. Наши найденные ошибки и фиксы
- **PLL_AUDIO (r914, КОРЕНЬ ~73% дропа во ВСЕХ системах):** стояло `N=14,M=14`
  при сохранённом `P=4` → `24*14/(14*4)=6 МГц` (в 4 раза ниже!) → I2S шёл ~12 кГц
  вместо 48 → продюсер переполнял кольцо на ~75%. Фикс: `PLL_AUDIO_CTRL=0x80035514`
  (24.571 МГц). Проверка `PLL_AUDIO=(24MHz*N)/(M*P)`.
- **Громкость (r914):** масштабировалась только PA10 (on/off) → «10%» звучало как
  максимум. Фикс: масштаб сэмплов в `i2s_push_sample` по `g_volume_pct`.
- **CU2 не спит (r914):** при пустом кольце пишем ТИШИНУ в FIFO (~до половины) —
  нет underrun-щелчков/«фантомов» при входе/выходе.
- **FLUSH (r913):** не держать `FLUSH` в `FCTL` — он R/WAC; в цикле долива не трогать.

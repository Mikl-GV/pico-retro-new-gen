// pom2_h3.cpp — платформенный слой POM2 для bare-metal H3 (core0).
// Ядро POM2 работает без ОС: ROM и образ диска приходят байтами от SD-слоя,
// кадр снимается с Apple2Display и отдаётся RGBA (uint32_t) через
// pom2_h3_pixels(); хост пишет его напрямую в HDMI-фреймбуфер
// (emu_show_rgba_int). Эмуляция идёт в вызывающем потоке: нитей, мьютексов
// и таймеров нет.
#include "pom2_h3.h"

#include <cstdio>
#include <cstring>
#include <memory>
#include <string>

#include "Apple2Display.h"
#include "CpuClock.h"
#include "DiskIICard.h"
#include "DiskImage.h"
#include "M6502.h"
#include "Memory.h"
#include "SlotBus.h"
#include "SpeakerDevice.h"

// newlib для arm-none-eabi не предоставляет fsync. POM2 зовёт его при
// атомарной записи образа (autosave); на bare-metal образы только читаются,
// поэтому сообщаем об успешной синхронизации.
extern "C" int fsync(int fd)
{
    (void)fd;
    return 0;
}

namespace {

Memory        *g_mem = nullptr;
M6502         *g_cpu = nullptr;
Apple2Display *g_display = nullptr;
DiskIICard    *g_disk = nullptr;
bool           g_up = false;

// Динамик Apple II — 1-битный триггер $C030; ядро POM2 синтезирует его само
// (SpeakerDevice), слой лишь владеет объектом и переводит float в PCM16.
SpeakerDevice g_speaker;
float         g_audio_f[1024];
uint64_t      g_audio_cycle = 0;
constexpr int kAudioHz      = 48000;

inline int16_t to_pcm16(float v)
{
    if (v < -1.0f) v = -1.0f;
    if (v >  1.0f) v =  1.0f;
    return (int16_t)(v * 32767.0f);
}

} // namespace

extern "C" int pom2_h3_boot(const uint8_t *rom, unsigned rom_size, int iie,
                            const uint8_t *disk, unsigned disk_size,
                            const char *disk_name)
{
    pom2_h3_shutdown();

    if (!rom || rom_size == 0) return -1;

    g_mem = new Memory();
    g_cpu = new M6502(g_mem);
    g_display = new Apple2Display();

    // Режим //e выставляется ДО загрузки ПЗУ: загрузчик раскладывает дамп
    // на внутреннее ПЗУ $C100-$CFFF только в этом режиме.
    if (iie) g_mem->setIIEMode(true);

    Memory::AppleIIRomImage image;
    image.bytes.assign(rom, rom + rom_size);
    // loadAppleIIRom возвращает 1 при успехе и 0 при отказе (не «код ошибки»).
    // Любой отказ ниже освобождает уже созданное: newlib-куча без free, иначе
    // объекты прошлого запуска остаются висеть до сброса кучи.
    if (!g_mem->loadAppleIIRom(image)) { pom2_h3_shutdown(); return -2; }

    // ][, ][+ и нерасширенный //e — не 65C02. Признак //e: $FBC0 = $EA.
    if (!iie || g_mem->peekCpuView(0xFBC0) == 0xEA)
        g_cpu->setCpuMode(M6502::CpuMode::NMOS);

    g_display->setAuxMemory(g_mem->auxData());

    // Динамик: объект в статике слоя, Memory держит только указатель.
    g_speaker.reset();
    g_speaker.setSampleRate((uint32_t)kAudioHz);
    g_speaker.setCpuClock((double)POM2_CPU_CLOCK_HZ);
    g_mem->setSpeakerDevice(&g_speaker);
    g_audio_cycle = g_mem->getCycleCounter();

    if (disk && disk_size) {
        g_disk = new DiskIICard();
        DiskImage img;
        // loadMediaSnapshot — сессионный снимок дорожек, НЕ образ диска:
        // обычный .dsk/.woz разбирает loadBuffer, имя даёт порядок секторов.
        if (!img.loadBuffer(disk, (std::size_t)disk_size,
                            disk_name ? disk_name : "")) {
            pom2_h3_shutdown(); return -3;
        }
        if (!g_disk->installDisk(0, std::move(img))) { pom2_h3_shutdown(); return -4; }
        g_mem->slotBus().plug(6, std::unique_ptr<SlotPeripheral>(g_disk));
    } else {
        g_disk = nullptr;
    }

    g_cpu->hardReset();
    g_cpu->start();
    g_up = true;
    return 0;
}

extern "C" int pom2_h3_frame(void)
{
    if (!g_up) return -1;

    // Memory публикует кадр сам на границе видео-кадра (advanceCycles →
    // advanceCyclesVideo при cycleCounter >= vblNextEventCycle_). Поэтому
    // достаточно догнать CPU до ближайшего VBL и перерисовать дисплей.
    const uint64_t now = g_mem->getCycleCounter();
    uint64_t target = g_mem->nextVblCpuCycle(now);
    if (target <= now) target = now + 65u * 262u;

    while (g_mem->getCycleCounter() < target)
        g_cpu->step();

    g_display->render(*g_mem);
    return 0;
}

extern "C" const uint32_t *pom2_h3_pixels(int *w, int *h)
{
    if (!g_up) {
        if (w) *w = 0;
        if (h) *h = 0;
        return nullptr;
    }
    if (w) *w = g_display->width();
    if (h) *h = g_display->height();
    return g_display->pixels();
}

extern "C" unsigned pom2_h3_display_flags(void)
{
    if (!g_up) return 0;
    const Memory::DisplayState s = g_mem->getDisplayState();
    unsigned f = 0;
    if (s.textMode)    f |= 1u << 0;
    if (s.mixedMode)   f |= 1u << 1;
    if (s.page2)       f |= 1u << 2;
    if (s.hiRes)       f |= 1u << 3;
    if (s.eightyCol)   f |= 1u << 4;
    if (s.an3)         f |= 1u << 5;
    if (s.altChar)     f |= 1u << 6;
    if (s.dhgr)        f |= 1u << 7;
    if (s.eightyStore) f |= 1u << 8;
    if (g_display->showingCardPicture()) f |= 1u << 9;
    return f;
}

extern "C" unsigned pom2_h3_machine_flags(void)
{
    if (!g_up) return 0;
    return (g_mem->isIIE() ? 1u : 0u) |
           (static_cast<unsigned>(g_mem->iieModeFlags()) << 8);
}

extern "C" unsigned pom2_h3_cpu_pc(void)
{
    if (!g_up || !g_cpu) return 0;
    return static_cast<unsigned>(g_cpu->getProgramCounter());
}

extern "C" void pom2_h3_key(int ch, int down)
{
    if (!g_up || ch <= 0 || ch > 0x7F) return;
    // В POM2 «удержание» и «символ» — разное: setKeyHeld даёт физическое
    // состояние (авто-повтор и сохранение кода после $C010), queueKey —
    // сам символ со стробом. Порядок важен: held выставляем ПЕРВЫМ, иначе
    // Keyboard пометит символ как инъекцию без удержания (one-shot).
    // Токен = код Apple II (bitset<512>, 0..0x7F помещается).
    g_mem->setKeyHeld((uint16_t)ch, down != 0);
    if (down) g_mem->queueKey((uint8_t)ch);
}

extern "C" int pom2_h3_audio(int16_t *pairs, unsigned max_pairs)
{
    if (!g_up || !pairs || max_pairs == 0) return 0;

    // Сколько сэмплов набежало с прошлого кадра — считаем от тактов CPU:
    // фиксированное число на кадр при 50/60 Гц систематически уводит звук.
    const uint64_t now = g_mem->getCycleCounter();
    const uint64_t d = now - g_audio_cycle;
    int frames = (int)((d * (uint64_t)kAudioHz) / (uint64_t)POM2_CPU_CLOCK_HZ);
    if (frames > (int)max_pairs) frames = (int)max_pairs;
    if (frames <= 0) return 0;

    g_speaker.fillAudioBuffer(g_audio_f, frames);
    for (int i = 0; i < frames; i++) {
        const int16_t s = to_pcm16(g_audio_f[i]);
        pairs[2 * i]     = s;
        pairs[2 * i + 1] = s;
    }
    g_audio_cycle += ((uint64_t)frames * (uint64_t)POM2_CPU_CLOCK_HZ) / (uint64_t)kAudioHz;
    return frames;
}

extern "C" void pom2_h3_paddles(const uint8_t *paddles, int btn0, int btn1)
{
    if (!g_up || !paddles) return;
    // Паддлы — аналог: значение задаёт время разряда в $C064-$C067/$C070.
    // Цифровой пад отдаёт крайние значения (0/255), центр — 128.
    for (int i = 0; i < 4; i++) g_mem->setPaddle(i, paddles[i]);
    g_mem->setPaddleButton(0, btn0 != 0);
    g_mem->setPaddleButton(1, btn1 != 0);
}

extern "C" int pom2_h3_insert_disk(int drive, const uint8_t *data,
                                   unsigned len, const char *name)
{
    if (!g_up || !g_disk || !data || len == 0) return -1;
    if (drive < 0 || drive > 1) return -1;   // у Disk II два привода
    DiskImage img;
    if (!img.loadBuffer(data, (std::size_t)len, name ? name : "")) {
        std::printf("pom2: image rejected: %s\n", img.getLastError().c_str());
        return -2;
    }
    if (!g_disk->installDisk(drive, std::move(img))) return -3;
    return 0;
}

extern "C" void pom2_h3_shutdown(void)
{
    if (g_cpu) { g_cpu->stop(); }

    g_disk = nullptr;               // владелец — SlotBus, он ниже
    // Cold start: pom2_h3_boot() вызывает shutdown() первым же действием,
    // чтобы освободить прошлую сессию, когда g_mem ещё никогда не был
    // создан (статические нули). Без guard это было чтение метода через
    // нулевой указатель — UB на bare-metal (мусор из SRAM, hard-fault).
    if (g_mem) {
        g_mem->slotBus().plug(6, nullptr);
        g_mem->setSpeakerDevice(nullptr);
    }
    g_speaker.reset();

    delete g_display; g_display = nullptr;
    delete g_cpu;     g_cpu = nullptr;
    delete g_mem;     g_mem = nullptr;
    g_up = false;
}

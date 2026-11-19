// ep_host.cpp — Enterprise 64/128 host-слой для H3 bare-metal.
//
// Обходит libretro-обвязку core/ (thread/pthread) и инстанцирует движок
// Ep128::Ep128VM напрямую, синхронно из host-цикла (без VMThread).
// Собственные VideoDisplay/AudioOutput подклассы пишут в framebuffer и I2S.
//
// HTTP-модель 64/128: задаётся через ep_set_model() (см. main.c):
//   - 64 : resetMemoryConfiguration(64) + ROM exos20/basic20
//   - 128: resetMemoryConfiguration(128) + ROM exos21/basic21
// Все ROM берутся из roms/roms.hpp (вшиты в прошивку).
//
// Видео: логический экран 768x576, RLE drawLine (см. decodeLine) -> RGB565
// палитра -> шкалирование в EMU_FB (320x240). Кадр финализируется на vsync.
// Звук: AudioOutput отдаёт стерео 44100, ресемплится rs44fs_stereo -> i2s_push_block.
// Ввод: USB-клавиатура -> EP keycode (0x00..0x7F), Sega-пад -> джойстик.

#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include "ep128emu.hpp"
#include "display.hpp"
#include "soundio.hpp"
#include "ep128vm.hpp"
#include "snd_conv.hpp"

extern "C" {
#include "emu.h"
#include "emulog.h"
#include "usb_kbd.h"
#include "sega_pad.h"
#include "btn_pad.h"
#include "fb_text.h"
#include "i2s.h"
#include "resampler44.h"
#include "h3_hs_timer.h"
#include "fat.h"
}

#define EP_LOGICAL_W   768
#define EP_LOGICAL_H   576
#define EP_FRAME_LINES 288    // r1354: строк Nick в кадре (768x288, не 312)
#define EP_SAMPLE_RATE 44100
#define FB_ADDR  0x5F900000u   // HDMI FB (XRGB8888, 1024×600)
#define FB_W     1024
#define FB_H     600

// ---------------------------------------------------------------------------
// r1518: DE2 GSU / cache (C-линковка; без extern "C" C++ манглит имена и
// сборка падает на undefined reference — как в cps1_host.cpp r1512).
// ---------------------------------------------------------------------------
extern "C" {
int h3_de2_ui_scaler_rect(uint32_t addr, uint32_t src_w, uint32_t src_h,
                          uint32_t bpp, uint32_t dx, uint32_t dy,
                          uint32_t dw, uint32_t dh);
void h3_de2_ui_restore(void);
void h3_de2_set_bkcolor(uint32_t argb);
void usb_cache_clean(uint32_t addr, uint32_t size);
}

// ---------------------------------------------------------------------------
// r1518: аппаратный DE2 GSU (вместо прямого писателя в FB_ADDR).
// Кадр Nick — 768×288 (строки RLE-декодируются в linebuf, палитра RGB565).
// В drawLine кадр теперь накапливается в ep_rgb32 (XRGB8888, упакован 768×288
// — строки по 768 слов, DE2 PITCH = INSIZE, урок r1495), а ep_render_frame
// (в цикле emu_run_enterprise) просит DE2 GSU растянуть его на всю высоту
// FB_H=600 с сохранением пропорций 4:3 (dst_w = 600×4/3 = 800, dx = 112 —
// та же геометрия, что была у прямого писателя). Размер фиксированный
// (768×288) — перекалибровка при смене w/h не нужна (урок r1506 не применим).
// Сброс флага при входе (урок r1496). Fallback — старый прямой писатель.
// ---------------------------------------------------------------------------
static uint32_t ep_rgb32[EP_LOGICAL_W * EP_FRAME_LINES] __attribute__((aligned(4), section(".noinit_fbn")));
static int g_ep_hw_scaler_active = 0;

// r1518: рендер кадра Enterprise на DE2 GSU. ep_rgb32 заполнен drawLine за
// кадр (768×288 XRGB8888). DE2 GSU растягивает на всю высоту FB_H=600 с
// сохранением пропорций 4:3 (dst_w = 600×4/3 = 800, dx = (1024-800)/2 = 112;
// бордюр — через h3_de2_set_bkcolor, поля заполняет микшер).
static void ep_render_frame(void)
{
  if (!g_ep_hw_scaler_active) {
    int dh = FB_H;                        // 600
    int dw = (FB_H * 4) / 3;              // 800
    if (dw > FB_W) dw = FB_W;
    if (dw <= 0) return;
    int dx = (FB_W - dw) / 2;             // 112 по центру
    h3_de2_set_bkcolor(0x00060812u);      // тёмно-синий бордюр (как поля EP)
    if (h3_de2_ui_scaler_rect((uint32_t)(uintptr_t)ep_rgb32,
                              (uint32_t)EP_LOGICAL_W, (uint32_t)EP_FRAME_LINES, 32,
                              (uint32_t)dx, 0, (uint32_t)dw, (uint32_t)dh) == 0) {
      g_ep_hw_scaler_active = 1;
      printf("EP: DE2 GSU scaler %dx%d->%dx%d\n", EP_LOGICAL_W, EP_FRAME_LINES, dw, dh);
    } else {
      printf("EP: GSU scaler failed, fallback direct FB\n");
    }
  }
  usb_cache_clean((uint32_t)(uintptr_t)ep_rgb32,
                  (uint32_t)((size_t)EP_LOGICAL_W * EP_FRAME_LINES * 4u));
}

// ---------------------------------------------------------------------------
// Палитра Nick (R2G2B2, 64 цвета) -> 256-entry RGB565, повтор по 4.
// Enterprise видеоконтроллер Nick выдаёт 2-бита на канал; цвет 0..63.
// ---------------------------------------------------------------------------
static uint16_t g_pal16[256];
static bool     g_pal_init = false;

static void ep_build_palette(void)
{
  // Точная формула Nick (Nick::convertPixelToRGB, +8 бит на пиксель):
  //   r = ((v<<2)&4 | (v>>2)&2 | (v>>6)&1) / 7
  //   g = ((v<<1)&4 | (v>>3)&2 | (v>>7)&1) / 7
  //   b = ((v>>1)&2 | (v>>5)&1) / 3
  for (int i = 0; i < 256; i++) {
    int v = i;
    int r = ((v << 2) & 4) | ((v >> 2) & 2) | ((v >> 6) & 1);
    int g = ((v << 1) & 4) | ((v >> 3) & 2) | ((v >> 7) & 1);
    int b = ((v >> 1) & 2) | ((v >> 5) & 1);
    uint16_t r5 = (uint16_t)((r * 255 / 7) >> 3);
    uint16_t g6 = (uint16_t)((g * 255 / 7) >> 2);
    uint16_t b5 = (uint16_t)((b * 255 / 3) >> 3);
    g_pal16[i] = (uint16_t)((r5 << 11) | (g6 << 5) | b5);
  }
  g_pal_init = true;
}

inline static uint16_t ep_pal(uint8_t idx) { return g_pal16[idx & 0xFF]; }

// ---------------------------------------------------------------------------
// VideoDisplay: строки Nick (768) декодируются и пишутся НАПРЯМУЮ в HDMI FB
// (XRGB8888, doublescan по вертикали), как frodo_host.c. Промежуточного кадра
// нет, поля заливаются только при смене геометрии — мерцания нет.
// ---------------------------------------------------------------------------
static volatile uint32_t g_ep_lines_drawn = 0;
static volatile uint32_t g_ep_frames_done = 0;

// Геометрия вывода (как в frodo_host.c): картинка на всю высоту, ширина 4:3.
static int g_ep_dw = 0, g_ep_dh = 0, g_ep_x0 = 0, g_ep_y0 = 0;

// Таблица исходных колонок (16.16) для горизонтального масштабирования.
static int g_ep_sx[FB_W];

// Заполнить поля бордюром (при смене геометрии либо по запросу).
static void ep_fill_border(uint32_t border)
{
  uint32_t* dst = (uint32_t*)FB_ADDR;
  for (int y = 0; y < FB_H; y++) {
    uint32_t* row = dst + (size_t)y * FB_W;
    int inside = (y >= g_ep_y0 && y < g_ep_y0 + g_ep_dh);
    if (!inside) {
      for (int x = 0; x < FB_W; x++) row[x] = border;
    } else {
      for (int x = 0; x < g_ep_x0; x++) row[x] = border;
      for (int x = g_ep_x0 + g_ep_dw; x < FB_W; x++) row[x] = border;
    }
  }
}

class EpDisplay : public Ep128Emu::VideoDisplay {
 public:
  unsigned char linebuf[EP_LOGICAL_W];
  int           curLine;
  int           lineNo;       // счётчик Nick-строк текущего кадра (0..)
  bool          frameDone;

  EpDisplay();
  virtual ~EpDisplay();
  virtual void setDisplayParameters(const Ep128Emu::VideoDisplay::DisplayParameters& dp);
  virtual const Ep128Emu::VideoDisplay::DisplayParameters& getDisplayParameters() const;
  virtual void drawLine(const uint8_t* buf, size_t nBytes);
  virtual void vsyncStateChange(bool newState, unsigned int currentSlot_);

 private:
  void decodeLine(const unsigned char* inBuf, size_t nBytes);
  Ep128Emu::VideoDisplay::DisplayParameters dp_;
};

EpDisplay::EpDisplay()
  : curLine(0), lineNo(0), frameDone(false)
{
  if (!g_pal_init) ep_build_palette();
  // Кадр Nick — 768x288 (display.hpp: «full horizontal resolution, no
  // interlace (768x288)»), т.е. 288 вызовов drawLine на кадр. Выводим его
  // 4:3 на 1024x600 удвоением строк: 768x576, поля по 128 px слева/справа
  // и по 12 строк сверху/снизу.
  g_ep_dw = EP_LOGICAL_W;                   // 768
  g_ep_dh = EP_FRAME_LINES * 2;             // 576
  g_ep_x0 = (FB_W - g_ep_dw) / 2;           // 128
  g_ep_y0 = (FB_H - g_ep_dh) / 2;           // 12
  ep_fill_border(0x00060812u);
}

EpDisplay::~EpDisplay()
{
}

void EpDisplay::setDisplayParameters(const Ep128Emu::VideoDisplay::DisplayParameters& dp)
{
  dp_ = dp;
}

const Ep128Emu::VideoDisplay::DisplayParameters& EpDisplay::getDisplayParameters() const
{
  return dp_;
}

// RLE-декод 768 пикселей одной строки (формат Nick, см. display.hpp / эталон)
void EpDisplay::decodeLine(const unsigned char* inBuf, size_t nBytes)
{
  const unsigned char* bufp = inBuf;
  unsigned char* outBuf = linebuf;
  unsigned char* endp = outBuf + EP_LOGICAL_W;
  (void)nBytes;

  do {
    switch (bufp[0]) {
    case 0x00:
      do {
        outBuf[15]=outBuf[14]=outBuf[13]=outBuf[12]=outBuf[11]=outBuf[10]=
        outBuf[9]=outBuf[8]=outBuf[7]=outBuf[6]=outBuf[5]=outBuf[4]=
        outBuf[3]=outBuf[2]=outBuf[1]=outBuf[0]=0x00;
        outBuf += 16; bufp += 1;
        if (outBuf >= endp) break;
      } while (bufp[0] == 0x00);
      break;
    case 0x01:
      do {
        outBuf[15]=outBuf[14]=outBuf[13]=outBuf[12]=outBuf[11]=outBuf[10]=
        outBuf[9]=outBuf[8]=outBuf[7]=outBuf[6]=outBuf[5]=outBuf[4]=
        outBuf[3]=outBuf[2]=outBuf[1]=outBuf[0]=bufp[1];
        outBuf += 16; bufp += 2;
        if (outBuf >= endp) break;
      } while (bufp[0] == 0x01);
      break;
    case 0x02:
      do {
        outBuf[7]=outBuf[6]=outBuf[5]=outBuf[4]=outBuf[3]=outBuf[2]=outBuf[1]=
        outBuf[0]=bufp[1];
        outBuf[15]=outBuf[14]=outBuf[13]=outBuf[12]=outBuf[11]=outBuf[10]=
        outBuf[9]=outBuf[8]=bufp[2];
        outBuf += 16; bufp += 3;
        if (outBuf >= endp) break;
      } while (bufp[0] == 0x02);
      break;
    case 0x03:
      do {
        unsigned char c0=bufp[1], c1=bufp[2], b=bufp[3];
        outBuf[1]=outBuf[0]=((b&128)?c1:c0);
        outBuf[3]=outBuf[2]=((b&64)?c1:c0);
        outBuf[5]=outBuf[4]=((b&32)?c1:c0);
        outBuf[7]=outBuf[6]=((b&16)?c1:c0);
        outBuf[9]=outBuf[8]=((b&8)?c1:c0);
        outBuf[11]=outBuf[10]=((b&4)?c1:c0);
        outBuf[13]=outBuf[12]=((b&2)?c1:c0);
        outBuf[15]=outBuf[14]=((b&1)?c1:c0);
        outBuf += 16; bufp += 4;
        if (outBuf >= endp) break;
      } while (bufp[0] == 0x03);
      break;
    case 0x04:
      do {
        outBuf[3]=outBuf[2]=outBuf[1]=outBuf[0]=bufp[1];
        outBuf[7]=outBuf[6]=outBuf[5]=outBuf[4]=bufp[2];
        outBuf[11]=outBuf[10]=outBuf[9]=outBuf[8]=bufp[3];
        outBuf[15]=outBuf[14]=outBuf[13]=outBuf[12]=bufp[4];
        outBuf += 16; bufp += 5;
        if (outBuf >= endp) break;
      } while (bufp[0] == 0x04);
      break;
    case 0x06:
      do {
        unsigned char c0=bufp[1], c1=bufp[2], b=bufp[3];
        outBuf[0]=(b&128)?c1:c0; outBuf[1]=(b&64)?c1:c0;
        outBuf[2]=(b&32)?c1:c0;  outBuf[3]=(b&16)?c1:c0;
        outBuf[4]=(b&8)?c1:c0;   outBuf[5]=(b&4)?c1:c0;
        outBuf[6]=(b&2)?c1:c0;   outBuf[7]=(b&1)?c1:c0;
        c0=bufp[4]; c1=bufp[5]; b=bufp[6];
        outBuf[8]=(b&128)?c1:c0; outBuf[9]=(b&64)?c1:c0;
        outBuf[10]=(b&32)?c1:c0; outBuf[11]=(b&16)?c1:c0;
        outBuf[12]=(b&8)?c1:c0;  outBuf[13]=(b&4)?c1:c0;
        outBuf[14]=(b&2)?c1:c0;  outBuf[15]=(b&1)?c1:c0;
        outBuf += 16; bufp += 7;
        if (outBuf >= endp) break;
      } while (bufp[0] == 0x06);
      break;
    case 0x08:
      do {
        outBuf[1]=outBuf[0]=bufp[1]; outBuf[3]=outBuf[2]=bufp[2];
        outBuf[5]=outBuf[4]=bufp[3]; outBuf[7]=outBuf[6]=bufp[4];
        outBuf[9]=outBuf[8]=bufp[5]; outBuf[11]=outBuf[10]=bufp[6];
        outBuf[13]=outBuf[12]=bufp[7]; outBuf[15]=outBuf[14]=bufp[8];
        outBuf += 16; bufp += 9;
        if (outBuf >= endp) break;
      } while (bufp[0] == 0x08);
      break;
    default:
      do { *(outBuf++) = 0x00; } while (outBuf < endp);
      break;
    }
  } while (outBuf < endp);
}

void EpDisplay::drawLine(const uint8_t* buf, size_t nBytes)
{
  // Nick отдаёт 312 строк/кадр (288 видимых + бордюры). Каждая строка
  // пишется напрямую в HDMI FB двумя экранными строками (doublescan),
  // центрированно по вертикали. Поля залиты заранее — мерцания нет.
  if (g_ep_dw <= 0 || g_ep_dh <= 0) return;
  decodeLine(buf, nBytes);

  static int      sx[FB_W];
  static unsigned sx_src = 0;
  static int      sx_dst = 0;
  if (EP_LOGICAL_W != sx_src || g_ep_dw != sx_dst) {
    uint32_t step = ((uint32_t)EP_LOGICAL_W << 16) / (uint32_t)g_ep_dw;
    uint32_t acc  = step >> 1;
    for (int dx = 0; dx < g_ep_dw; dx++) {
      sx[dx] = (int)(acc >> 16);
      acc += step;
      if (acc >= ((uint32_t)EP_LOGICAL_W << 16)) acc -= (uint32_t)EP_LOGICAL_W << 16;
    }
    sx_src = EP_LOGICAL_W; sx_dst = g_ep_dw;
  }

  // Вертикаль: каждая из 288 строк Nick занимает ровно две экранные строки
  // (576 строк кадра). Раньше делили на 312 и получали нецелый шаг 1.92 —
  // нижние ~7% экрана никогда не перерисовывались и картинка «съезжала».
  if (lineNo >= EP_FRAME_LINES) { lineNo++; curLine++; return; }
  int src_y = g_ep_y0 + lineNo * 2;
  if (src_y < 0 || src_y >= g_ep_y0 + g_ep_dh) { lineNo++; curLine++; return; }

  // r1518: при активном DE2 GSU строки Nick копятся в ep_rgb32 (упакован
  // 768×288) — программный прямой писатель в FB не нужен (и наложился бы
  // поверх аппаратного). Сброс флага в начале кадра (vsyncStateChange) —
  // после любого кадра буфер заполняется заново (fallback — старый путь).
  uint32_t* dst_row = NULL;
  if (g_ep_hw_scaler_active) {
    dst_row = ep_rgb32 + (size_t)lineNo * EP_LOGICAL_W;
  } else {
    dst_row = (uint32_t*)FB_ADDR + (size_t)src_y * FB_W + g_ep_x0;
  }
  for (int dx = 0; dx < g_ep_dw; dx++) {
    uint16_t p = ep_pal(linebuf[sx[dx]]);
    dst_row[dx] = (((p >> 11) & 0x1Fu) << 19) | (((p >> 5) & 0x3Fu) << 10) |
                  ((p & 0x1Fu) << 3);
  }
  if (!g_ep_hw_scaler_active && src_y + 1 < g_ep_y0 + g_ep_dh) {
    uint32_t* drow2 = dst_row + FB_W;
    for (int dx = 0; dx < g_ep_dw; dx++) drow2[dx] = dst_row[dx];
  }
  lineNo++;
  curLine++;
  g_ep_lines_drawn++;
}

void EpDisplay::vsyncStateChange(bool newState, unsigned int currentSlot_)
{
  (void)currentSlot_;
  if (newState) {
    curLine = 0; lineNo = 0;   // начало нового кадра
    frameDone = true;          // кадр выведен -> host заканчивает кадр
  }
}

// ---------------------------------------------------------------------------
// AudioOutput: стерео 44100, ресемпл -> I2S
// ---------------------------------------------------------------------------
class EpAudio : public Ep128Emu::AudioOutput {
 public:
  EpAudio()  { setParameters(0, (float)EP_SAMPLE_RATE); }
  virtual ~EpAudio() {}
  virtual void sendAudioData(const int16_t* buf, size_t nFrames);
#ifdef EP128EMU_LIBRETRO_CORE
  virtual void forwardAudioData(int16_t* /*buf_out*/, size_t* /*nFrames*/, int /*expectedFrames*/) {}
#endif
  virtual void closeDevice() {}
  virtual std::vector<std::string> getDeviceList() { return std::vector<std::string>(); }
};

void EpAudio::sendAudioData(const int16_t* buf, size_t nFrames)
{
  if (!buf || nFrames == 0) return;
  static uint32_t rs_phase = 0;
  static int16_t  rs_hist[RS44_HIST * 2] = {0, 0, 0, 0, 0, 0};
  static int16_t  out[2048 * 2];
  size_t n = nFrames;
  while (n > 0) {
    size_t chunk = (n > 2048) ? 2048 : n;
    uint32_t o = rs44fs_stereo(&rs_phase, rs_hist, buf, (uint32_t)chunk, out, 2048);
    if (o) i2s_push_block(out, o);
    buf += chunk * 2;
    n -= chunk;
  }
}

// ---------------------------------------------------------------------------
// Клавиатура: HID scancode -> EP keycode (см. эталон libretro_to_ep128emu_kbmap)
// ---------------------------------------------------------------------------
static int8_t g_kbd[256];    // EP keycode pressed map is handled directly

// r1354: клавиша, которую в этом кадре держит автозагрузка (EP keycode, -1 =
// нет). Вливается в общий g_kbd, иначе ep_update_input затирал бы её каждый
// кадр и EXOS не успевал увидеть нажатие.
static int g_ep_auto_held = -1;

static void ep_update_input(Ep128::Ep128VM& vm)
{
  memset(g_kbd, 0, sizeof(g_kbd));

  uint8_t keys[8];
  int n = usb_kbd_get_raw(keys, 8);
  for (int i = 0; i < n; i++) {
    uint8_t sc = keys[i];
    int epkey = -1;
    switch (sc) {
      case 0x04: epkey=0x0E; break; // a
      case 0x05: epkey=0x02; break; // b
      case 0x06: epkey=0x03; break; // c
      case 0x07: epkey=0x0B; break; // d
      case 0x08: epkey=0x15; break; // e
      case 0x09: epkey=0x0C; break; // f
      case 0x0A: epkey=0x0A; break; // g
      case 0x0B: epkey=0x08; break; // h
      case 0x0C: epkey=0x48; break; // i
      case 0x0D: epkey=0x30; break; // j
      case 0x0E: epkey=0x32; break; // k
      case 0x0F: epkey=0x34; break; // l
      case 0x10: epkey=0x40; break; // m
      case 0x11: epkey=0x00; break; // n
      case 0x12: epkey=0x4A; break; // o
      case 0x13: epkey=0x4C; break; // p
      case 0x14: epkey=0x11; break; // q
      case 0x15: epkey=0x13; break; // r
      case 0x16: epkey=0x0D; break; // s
      case 0x17: epkey=0x14; break; // t
      case 0x18: epkey=0x10; break; // u
      case 0x19: epkey=0x04; break; // v
      case 0x1A: epkey=0x16; break; // w
      case 0x1B: epkey=0x05; break; // x
      case 0x1C: epkey=0x12; break; // y
      case 0x1D: epkey=0x06; break; // z
      case 0x1E: epkey=0x19; break; // 1
      case 0x1F: epkey=0x1E; break; // 2
      case 0x20: epkey=0x1D; break; // 3
      case 0x21: epkey=0x1B; break; // 4
      case 0x22: epkey=0x1C; break; // 5
      case 0x23: epkey=0x1A; break; // 6
      case 0x24: epkey=0x18; break; // 7
      case 0x25: epkey=0x28; break; // 8
      case 0x26: epkey=0x2A; break; // 9
      case 0x27: epkey=0x2C; break; // 0
      case 0x28: epkey=0x3E; break; // enter
      case 0x29: epkey=0x1F; break; // esc
      case 0x2A: epkey=0x2E; break; // backspace
      case 0x2B: epkey=0x17; break; // tab
      case 0x2C: epkey=0x46; break; // space
      case 0x2D: epkey=0x2B; break; // -
      case 0x2E: epkey=0x2D; break; // =
      case 0x2F: epkey=0x4B; break; // [
      case 0x30: epkey=0x4D; break; // ]
      case 0x31: epkey=0x36; break; // backslash
      case 0x33: epkey=0x33; break; // ;
      case 0x34: epkey=0x35; break; // '
      case 0x36: epkey=0x42; break; // ,
      case 0x37: epkey=0x44; break; // .
      case 0x38: epkey=0x43; break; // /
      case 0x39: epkey=0x09; break; // capslock
      case 0x3A: epkey=0x27; break; // F1
      case 0x3B: epkey=0x26; break; // F2
      case 0x3C: epkey=0x22; break; // F3
      case 0x3D: epkey=0x20; break; // F4
      case 0x3E: epkey=0x24; break; // F5
      case 0x3F: epkey=0x23; break; // F6
      case 0x40: epkey=0x25; break; // F7
      case 0x41: epkey=0x21; break; // F8
      case 0x42: epkey=0x3C; break; // F9
      case 0x43: epkey=0x38; break; // F10 (STOP)
      case 0x49: epkey=0x47; break; // insert
      case 0x4A: epkey=0x01; break; // home -> EP backslash
      case 0x4C: epkey=0x41; break; // delete
      case 0x4F: epkey=0x3A; break; // right
      case 0x50: epkey=0x3D; break; // left
      case 0x51: epkey=0x39; break; // down
      case 0x52: epkey=0x3B; break; // up
      case 0x62: epkey=0x3B; break; // up (dup)
      default: break;
    }
    if (epkey >= 0) g_kbd[epkey & 0x7F] = 1;
  }

  // mods
  uint8_t mods = usb_kbd_get_mods();
  if (mods & 0x02) g_kbd[0x07] = 1; // LShift
  if (mods & 0x20) g_kbd[0x45] = 1; // RShift
  if (mods & 0x01) g_kbd[0x0F] = 1; // LCtrl
  if (mods & 0x10) g_kbd[0x0F] = 1; // RCtrl
  if (mods & 0x04) g_kbd[0x3F] = 1; // LAlt
  if (mods & 0x40) g_kbd[0x3F] = 1; // RAlt

  // Sega pad -> джойстик. На EP джойстики читаются через матрицу клавиатуры
  // (dave.cpp: строки 14/15 для EXT1/EXT2, «внутренний» = курсор+пробел):
  //   внутренний: Up/Down/Left/Right = 0x3B/0x39/0x3D/0x3A, Fire = 0x46;
  //   EXT1 (Kempston в ZX-портах): 0x73/0x72/0x71/0x70 + fire 0x74.
  // Пад один, поэтому жмём ОБА набора — иначе игры, читающие только внешний
  // порт (порты ZX), оставались без управления. Справочник ep128emu:
  // core.hpp joystickCodesInt/joystickCodesExt1.
  uint16_t sp = pad_scan_combined();
  if (sp & 0x0001) { g_kbd[0x3B] = 1; g_kbd[0x70] = 1; }   // Up
  if (sp & 0x0002) { g_kbd[0x39] = 1; g_kbd[0x71] = 1; }   // Down
  if (sp & 0x0004) { g_kbd[0x3D] = 1; g_kbd[0x72] = 1; }   // Left
  if (sp & 0x0008) { g_kbd[0x3A] = 1; g_kbd[0x73] = 1; }   // Right
  if (sp & 0x0010) { g_kbd[0x46] = 1; g_kbd[0x74] = 1; }   // A = Fire (все)
  if (sp & 0x0080) g_kbd[0x3E] = 1; // Start = Enter

  // r1354: автозагрузка (имя файла + F1 START) вливается в тот же кадр.
  if (g_ep_auto_held >= 0) g_kbd[g_ep_auto_held] = 1;

  // применяем
  for (int k = 0; k < 128; k++) {
    vm.setKeyboardState(k, g_kbd[k] ? true : false);
  }
}

// ---------------------------------------------------------------------------
// Автозагрузка (r1356): документированный «turn-key»-механизм Enterprise.
// EXDOS Manual, приложение E: файл с именем START автоматически загружается и
// запускается командой START, которая даётся ИЗ РЕДАКТОРА BASIC (F1 или набор
// START + Enter). Поэтому образ игры регистрируется в fs_layer под именами
// 'start'/'start.com', а автозагрузка просто набирает "START" + Enter.
// Раньше (r1354..r1355) набирался псевдоним имени файла: после включения образ
// оказывается в редакторе BASIC, где имя файла — не команда, и BASIC отвечал
// «Not understood», игра не стартовала (подтверждено логом с железа).
// ---------------------------------------------------------------------------
static int g_ep128 = 0;        // 1 = Enterprise 128, 0 = Enterprise 64

#define EP_AUTO_STEP_FRAMES 20    // шаг между символами (кадры, как эталон)
#define EP_AUTO_HOLD_FRAMES 10    // удержание символа
#define EP_AUTO_WAIT_COUNT  3     // паузы после включения: EXOS успевает дать BASIC
#define EP_AUTO_MAX_STEPS   48
#define EP_AUTO_DELAY       0xFF  // маркер «пауза»: клавиша не нажата

static uint8_t g_ep_auto_seq[EP_AUTO_MAX_STEPS];
static int     g_ep_auto_len = 0;
static int     g_ep_auto_frame = 0;

// EP keycode для символа, который умеет печатать автозагрузка (a-z 0-9 . - CR).
static int ep_auto_key_for(char c)
{
  if (c >= 'a' && c <= 'z') {
    // DAVE (dave.hpp): A=0x0E, B=0x02 ... Z=0x06 (латинская раскладка EP).
    static const uint8_t t[26] = {
      0x0E, 0x02, 0x03, 0x0B, 0x15, 0x0C, 0x0A, 0x08, 0x48, 0x30, 0x32, 0x34, 0x40,
      0x00, 0x4A, 0x4C, 0x11, 0x13, 0x0D, 0x14, 0x10, 0x04, 0x16, 0x05, 0x12, 0x06
    };
    return t[c - 'a'];
  }
  if (c >= '0' && c <= '9') {
    static const uint8_t t[10] = {
      0x2C, 0x19, 0x1E, 0x1D, 0x1B, 0x1C, 0x1A, 0x18, 0x28, 0x2A
    };
    return t[c - '0'];
  }
  if (c == '.')  return 0x44;
  if (c == '-')  return 0x2B;
  if (c == ' ')  return 0x46;
  if (c == '\r') return 0x3E;
  return -1;
}

// Нормализация имени ровно как VirtualMachine::openFileInWorkingDirectory():
// нижний регистр, допустимы a-z 0-9 . + - _ , остальные символы -> '_'.
// fs_layer ищет доп. файлы по таким ключам.
static void ep_make_key(const char* name, char* out, int outsz)
{
  int i = 0;
  if (!name) name = "";
  for (; *name && i < outsz - 1; name++) {
    char c = *name;
    if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
    if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
          c == '.' || c == '+' || c == '-' || c == '_'))
      c = '_';
    out[i++] = c;
  }
  out[i] = 0;
}

// Короткое имя для набора на клавиатуре EP: [a-z0-9]{1..8} + опционально
// .[a-z0-9]{1..3}. Псевдоним не зависит от длинного имени файла на SD.
static void ep_make_alias(const char* name, char* out, int outsz)
{
  const char* base = name ? name : "";
  for (const char* p = base; *p; p++) {
    if (*p == '/' || *p == '\\') base = p + 1;
  }
  const char* dot = 0;
  for (const char* p = base; *p; p++) {
    if (*p == '.') dot = p;
  }
  const char* stem_end = dot ? dot : base + strlen(base);
  int n = 0;
  for (const char* p = base; p < stem_end && n < 8 && n < outsz - 6; p++) {
    char c = *p;
    if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
    if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) out[n++] = c;
  }
  if (n == 0) {
    static const char d[] = "game";
    for (int i = 0; d[i] && i < outsz - 1; i++) out[n++] = d[i];
  }
  if (dot) {
    int e = 0;
    for (const char* p = dot + 1; *p && e < 3; p++) {
      char c = *p;
      if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
      if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
        if (!e) out[n++] = '.';
        out[n++] = c;
        e++;
      }
    }
  }
  out[n] = 0;
}

static int ep_file_is_ep_like(const uint8_t* rom, uint32_t size)
{
  if (!rom || size < 2) return 0;
  // r1355: EXOS-файл: rom[0] — 0x00 (заголовок незащищённого файла),
  // rom[1] — 0x04 (BASIC), 0x05 (программа), 0x06 (IS-BASIC).
  // До r1355 индексы были перепутаны (rom[1]==0 && rom[0] в {4,5,6}) и проверку
  // не проходил ни один настоящий EP-файл — автозапуск не включался.
  if (rom[0] != 0x00) return 0;
  return (rom[1] == 0x04 || rom[1] == 0x05 || rom[1] == 0x06);
}

static void ep_auto_start_build(const char* alias)
{
  // alias — псевдоним основного файла; сам файл зарегистрирован ещё и как
  // 'start' (см. emu_run_ep), поэтому команде START достаточно набрать имя.
  static const char cmd[] = "START";
  g_ep_auto_len = 0;
  g_ep_auto_frame = 0;
  g_ep_auto_held = -1;
  // Пробел в начале — очищает строку редактора (как в эталонной последовательности).
  g_ep_auto_seq[g_ep_auto_len++] = ' ';
  for (int i = 0; i < EP_AUTO_WAIT_COUNT; i++) {
    if (g_ep_auto_len < EP_AUTO_MAX_STEPS - 1)
      g_ep_auto_seq[g_ep_auto_len++] = EP_AUTO_DELAY;   // задержка (без клавиши)
  }
  for (int i = 0; cmd[i] && g_ep_auto_len < EP_AUTO_MAX_STEPS - 2; i++) {
    char c = cmd[i];
    if (c >= 'A' && c <= 'Z') c = (char)(c + 32);   // ep_auto_key_for ждёт нижний регистр
    g_ep_auto_seq[g_ep_auto_len++] = (uint8_t)c;
  }
  g_ep_auto_seq[g_ep_auto_len++] = '\r';
  printf("EP: autostart 'START' (file alias '%s')\n", alias ? alias : "?");
}

// Вызывается раз в кадр ДО ep_update_input: выставляет g_ep_auto_held.
static void ep_auto_start_tick(void)
{
  if (g_ep_auto_len <= 0) return;
  int boot = g_ep128 ? 400 : 300;   // кадры загрузки EXOS (core.cpp:226-234)
  int t = g_ep_auto_frame - boot;
  g_ep_auto_frame++;
  if (t < 0) return;
  int pos = t / EP_AUTO_STEP_FRAMES;
  int ph  = t % EP_AUTO_STEP_FRAMES;
  if (pos >= g_ep_auto_len) {
    g_ep_auto_len = 0;
    g_ep_auto_held = -1;
    printf("EP: autostart sequence done (frame %d)\n", g_ep_auto_frame);
    return;
  }
  if (ph == 0) {
    uint8_t k = g_ep_auto_seq[pos];
    g_ep_auto_held = (k == EP_AUTO_DELAY) ? -1 : ep_auto_key_for((char)k);
    if (k != EP_AUTO_DELAY && g_ep_auto_held < 0)
      printf("EP: autostart: no keycode for 0x%02X\n", (unsigned)k);
  } else if (ph == EP_AUTO_HOLD_FRAMES) {
    g_ep_auto_held = -1;
  }
}

// ---------------------------------------------------------------------------
// Точка входа
// ---------------------------------------------------------------------------

extern "C" void ep_set_model(int model64)
{
  // main.c: m=0 -> «Enterprise 64», m=1 -> «Enterprise 128»
  g_ep128 = model64 ? 1 : 0;
}

namespace Ep128Emu {
  uint64_t ep128emu_get_usec(void) { return h3_hs_timer_lo_us(); }
  void ep128emu_wait_usec(unsigned int usec) { h3_hs_timer_delay(usec); }
}

// ---------------------------------------------------------------------------
// FileIO: EXOS открывает выбранный файл по имени (механизм эталона EP128_FILE).
// Буфер из load_rom регистрируется в fs_layer, ядро делает stat/fopen ->
// наш fs_layer отдаёт содержимое по имени, которое набрано в EXOS.
// Игра из нескольких файлов (.com + .prg/.scr/...) грузится целиком: файлы
// каталога игры читаются с SD в ROM_BUF сразу и регистрируются доп. слотами.
// ---------------------------------------------------------------------------
extern "C" void fs_register(const char* path, const void* data, uint32_t size);
extern "C" int  fs_register_extra(const char* name, const void* data, uint32_t size);
extern "C" void fs_register_bios(const char* path, const void* data, uint32_t size);

/* r1369: hot-swap дискеты Enterprise по F12. Образ диска (FAT .img) кладём во
   второй файловый слот fs_layer (как Kickstart у Amiga) под уникальным именем
   и монтируем в флоппи-Drive A: через setDiskImageFile — ядро откроет его
   обычным fopen, который наши fs_layer-заглушки отдадут из буфера. */
#define EP_DISK_IMAGE "epfloppy.img"
static Ep128::Ep128VM* g_ep_vm = 0;
static char           g_ep_disk_name[FAT_NAME_LEN];

static int ep_disk_insert(int drive, const char* path, const uint8_t* data,
                          uint32_t size, const char* name)
{
  if (drive != 0 || !g_ep_vm || !data || size == 0 || !name) return -1;
  size_t n = strlen(name);
  if (n >= sizeof(g_ep_disk_name)) n = sizeof(g_ep_disk_name) - 1;
  memcpy(g_ep_disk_name, name, n);
  g_ep_disk_name[n] = 0;
  fs_register_bios(EP_DISK_IMAGE, data, size);
  try {
    g_ep_vm->setDiskImageFile(0, EP_DISK_IMAGE, -1, -1, -1);
  } catch (...) {
    printf("EP: disk '%s' insert failed\n", g_ep_disk_name);
    return -1;
  }
  printf("EP: disk inserted '%s' (%u Б)\n", g_ep_disk_name, (unsigned)size);
  return 0;
}

static const emu_disk_ops_t g_ops_ep = {
  1, "img;dsk", 1, ep_disk_insert
};

#define EP_ROM_BASE 0x50000000u
#define EP_ROM_MAX  (24u * 1024u * 1024u)   // ROM_BUF (см. docs/MEMORY.md)
#define EP_KEY_MAX  40                      // как FS_EXTRA_NAME в fs_layer.c

static std::string g_ep_file_name;           // имя, которое отдаёт колбэк
static char        g_ep_dir[FAT_NAME_LEN + 24];   // каталог выбранного файла

extern "C" void ep_set_rom_dir(const char* dir)
{
  if (!dir) { g_ep_dir[0] = 0; return; }
  size_t n = strlen(dir);
  if (n >= sizeof(g_ep_dir)) n = sizeof(g_ep_dir) - 1;
  memcpy(g_ep_dir, dir, n);
  g_ep_dir[n] = 0;
}

static void ep_file_name_cb(void * /*userData*/, std::string& fileName)
{
  fileName = g_ep_file_name;   // EXOS-имя открываемого файла
}

// Подгрузить остальные файлы каталога игры и отдать их fs_layer по
// нормализованным именам. Вызывается только для вложенных каталогов: в корне
// /roms/enterprise лежат сотни чужих файлов (дискеты и т.п.), их не тянем.
static void ep_load_dir_files(const char* entry_name, uint32_t entry_size)
{
  if (!g_ep_dir[0] || strcmp(g_ep_dir, "/roms/enterprise") == 0) return;

  fat_entry_t* list = fat_scratch();
  int n = fat_list(g_ep_dir, list, FAT_MAX_ENTRIES);
  if (n <= 0) {
    printf("EP: dir '%s' list failed (%d)\n", g_ep_dir, n);
    return;
  }
  uint32_t off = (entry_size + 63u) & ~63u;   // строка кэша между файлами
  int loaded = 0, skipped = 0;
  for (int i = 0; i < n; i++) {
    if (list[i].attr & 0x10) continue;                 // каталог
    if (list[i].size == 0) continue;
    // сам выбранный файл уже в ROM_BUF (сравнение без регистра)
    int same = 1;
    for (const char* a = entry_name, *b = list[i].name; *a || *b; a++, b++) {
      char ca = *a, cb = *b;
      if (ca >= 'A' && ca <= 'Z') ca = (char)(ca + 32);
      if (cb >= 'A' && cb <= 'Z') cb = (char)(cb + 32);
      if (ca != cb) { same = 0; break; }
      if (!*a) break;
    }
    if (same) continue;
    if (list[i].size > EP_ROM_MAX - off) { skipped++; continue; }
    uint8_t* dst = (uint8_t*)EP_ROM_BASE + off;
    if (fat_read_file(&list[i], 0, dst, list[i].size) != (int)list[i].size) {
      printf("EP: dir file '%s' read failed\n", list[i].name);
      skipped++;
      continue;
    }
    char key[EP_KEY_MAX];
    ep_make_key(list[i].name, key, (int)sizeof(key));
    if (fs_register_extra(key, dst, list[i].size) != 0) { skipped++; continue; }
    off = (off + list[i].size + 63u) & ~63u;
    loaded++;
  }
  printf("EP: dir '%s' extra files=%d skipped=%d used=%u KB\n",
         g_ep_dir, loaded, skipped, (unsigned)(off >> 10));
}


extern "C" void emu_run_ep(const uint8_t* rom, uint32_t size, const char* rom_name)
{
  emu_prepare();
  emulog_begin("EP");
  emulog_field("EP", "rom", "%s, %u B", rom_name ? rom_name : "BASIC", (unsigned)size);
  printf("EP: %s (model %s, %u bytes)\n", rom_name ? rom_name : "BASIC",
         g_ep128 ? "128" : "64", (unsigned)size);

  // r1354: состояние прошлой сессии (в т.ч. недоигранная последовательность
  // автозагрузки) не должно протекать в новую — иначе в EXOS полетят клавиши.
  g_ep_auto_len = 0;
  g_ep_auto_frame = 0;
  g_ep_auto_held = -1;

  // Встроенные ROM (roms.hpp) — статическая таблица (builtin_rom_tab), без
  // динамической аллокации: emu_prepare() свободно сбрасывает кучу.
  emu_prepare();
  snd_manifest("enterprise", "dave");

  EpAudio audioOut;
  EpDisplay disp;

  Ep128::Ep128VM vm(disp, audioOut);

  // память + ROM (вшитые). g_ep128=0 -> Enterprise 64 (EXOS 2.0/BASIC 1.0);
  // g_ep128=1 -> Enterprise 128 (EXOS 2.1/BASIC 1.1).
  vm.resetMemoryConfiguration(g_ep128 ? 128 : 64);
  if (g_ep128) {
    vm.loadROMSegment(0x00, "_default_exos21.rom", 0);
    vm.loadROMSegment(0x01, "_default_exos21.rom", 16384);
    vm.loadROMSegment(0x05, "_default_basic21.rom", 0);
  } else {
    vm.loadROMSegment(0x00, "_default_exos20.rom", 0);
    vm.loadROMSegment(0x01, "_default_exos20.rom", 16384);
    vm.loadROMSegment(0x04, "_default_basic20.rom", 0);
  }

  vm.setEnableAudioOutput(true);
  vm.setAudioOutputHighQuality(true);
  vm.setEnableDisplay(true);
  vm.setCPUFrequency(4000000);
  vm.setVideoFrequency(889846);
  vm.setSoundClockFrequency(500000);
  vm.reset(true);
  g_ep_vm = &vm;   /* r1369: F12 hot-swap; ставим ДО стартового монтирования .img */

  // Автозагрузка выбранного файла (r1356: команда START из BASIC, см. блок
  // ep_auto_start_build). Только для настоящих EP-файлов (заголовок
  // 00 04/00 05/00 06) — у .dtf/.img/посторонних форматов автозапуск не
  // применяем, пользователь остаётся в EXOS/BASIC.
  int ep_auto_enabled = (rom && size && ep_file_is_ep_like(rom, size)) ? 1 : 0;
  char ep_alias[16] = {0};
  if (rom && size) {
    ep_make_alias(rom_name, ep_alias, (int)sizeof(ep_alias));
    g_ep_file_name = ep_alias;

    vm.setEnableFileIO(true);
    vm.setWorkingDirectory("/");
    vm.setFileNameCallback(&ep_file_name_cb, (void*)0);
    // EPFILEIO-драйвер (вшит) — сегмент 0x10, как у эталона (core.cpp:284)
    vm.loadROMSegment(0x10, "_default_epfileio.rom", 0);

    // Основной файл — под коротким именем 8.3 (его набирают вручную из BASIC
    // и по нему загрузчики переоткрывают сами себя), плюс под нормализованным
    // длинным именем с SD.
    fs_register(ep_alias, rom, size);
    char entry_key[EP_KEY_MAX];
    ep_make_key(rom_name ? rom_name : ep_alias, entry_key, (int)sizeof(entry_key));
    if (strcmp(entry_key, ep_alias) != 0)
      fs_register_extra(entry_key, rom, size);
    // r1356: «turn-key»-запуск Enterprise — файл START подхватывает команда
    // START из BASIC (EXDOS Manual, прил. E). Копии нет: тот же буфер, два имени.
    if (ep_auto_enabled) {
      fs_register_extra("start", rom, size);
      fs_register_extra("start.com", rom, size);
    }
    ep_load_dir_files(rom_name ? rom_name : ep_alias, size);

    printf("EP: fileio '%s' (alias '%s', %u bytes) autostart=%d\n",
           rom_name ? rom_name : "rom", ep_alias, (unsigned)size, ep_auto_enabled);
  }
  if (ep_auto_enabled) ep_auto_start_build(ep_alias);

  // r1369: образ диска (.img/.dsk) при запуске монтируем в дисковод A
  // (F12 работает так же). vm.reset(true) уже выполнен, диски сброшены —
  // вставка сейчас корректна. Автозапуск START к дискам не применяем:
  // диск загружается из EXOS по команде пользователя / через F12.
  {
    const char* nm = rom_name ? rom_name : ep_alias;
    int is_img = 0;
    const char* dot = strrchr(nm, '.');
    if (dot && dot[1]) {
      char e[4] = { 0 };
      int i;
      for (i = 0; i < 3 && dot[1 + i]; i++) {
        char c = dot[1 + i];
        if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
        e[i] = c;
      }
      is_img = (strcmp(e, "img") == 0 || strcmp(e, "dsk") == 0);
    }
    if (is_img && rom && size)
      ep_disk_insert(0, 0, rom, size, rom_name ? rom_name : ep_alias);
  }

  // Прямой писатель во FB_ADDR: двойная буферизация меню (опция) несовместима
  // — fb_flush() переключал бы DE2 на back-буфер, а мы пишем во фронт.
  fb_flip_set(0);

  emulog_field("EP", "video", "672x576");
  emulog_field("EP", "machine", "Enterprise %s (Z80N)", g_ep128 ? "128" : "64");
  emulog_field("EP", "init", "OK");
  emu_set_border_color(0x00060812);   // тёмно-синий (как CPC/Frodo) — поля меню не видны
  emu_set_period_us(20000);   // PAL 50 Гц (20 мс на кадр)
  // r1518: сброс флага аппаратного скейлера ПРИ КАЖДОМ входе (урок r1496):
  // static переживает сессию, без сброса повторный запуск = чёрный экран.
  g_ep_hw_scaler_active = 0;
  emu_throttle_reset();
  emu_esc_hold_reset();
  emu_disk_register(&g_ops_ep);

  /* main loop: ~20ms на кадр (50 fps) */
  static uint32_t ep_dbg_last = 0;
  for (;;) {
    emu_disk_poll();           /* r1369: F12 — до ввода/run, иначе уйдёт в EXOS */
    ep_auto_start_tick();        // r1354: последовательность START (до ввода)
    ep_update_input(vm);
    vm.run(20000);               // 20 ms эмуляции

    // r1356: по-секундный дамп frames/lines убран — это спам в UART (владелец).
    // Оставлена только диагностика реальной проблемы: за секунду не пришло ни
    // одного кадра (зависание/нет vsync).
    uint32_t now = h3_hs_timer_lo_us();
    if (now - ep_dbg_last >= 1000000) {
      if (g_ep_frames_done == 0)
        printf("EP: no video frames in 1000 ms (hang?)\n");
      ep_dbg_last = now;
      g_ep_frames_done = 0; g_ep_lines_drawn = 0;
    }
    if (disp.frameDone) {
      // r1518: при активном DE2 GSU кадр уже накоплен drawLine в ep_rgb32
      // (XRGB8888 768×288) — просим GSU его растянуть (программный путь со
      // строками FB не нужен). Иначе — старый прямой писатель (поля/copy
      // уже сделаны в drawLine и ep_fill_border).
      disp.frameDone = false;
      // r1519: ep_render_frame вызывается БЕЗУСЛОВНО каждый кадр — именно он
      // при первом кадре настраивает DE2 GSU (ставит g_ep_hw_scaler_active).
      // Раньше его звали ВНУТРИ if (активен) — он никогда не вызывался, GSU
      // не активировался, ядро вечно сидело на старом прямом писателе.
      ep_render_frame();
      // r1518-fallback: программный прямой путь — только пока GSU не
      // активирован (строки Nick уже записаны drawLine в FB, полs
      // залиты ep_fill_border).
      if (!g_ep_hw_scaler_active) {
        fb_dirty_all();
        fb_frame_scaled();
        fb_flush();
      }
      g_ep_frames_done++;
    }
    emu_throttle();
    if (emu_esc_hold()) break;
  }

  /* r1518: вернуть DE2 слой на буфер меню (иначе экран держит последний
     кадр Enterprise и меню не видно) + бордюр обратно чёрный, как у 32X r1493.
     ВАЖНО: до fb_clear — иначе DE2 продолжит читать освобождённый ep_rgb32. */
  {
    extern void h3_de2_ui_restore(void);
    extern void h3_de2_set_bkcolor(uint32_t argb);

    h3_de2_ui_restore();
    h3_de2_set_bkcolor(0xFF000000u);
  }
  emu_audio_stop();
  fb_clear(); fb_flush();
  g_ep_dir[0] = 0;               // каталог игры не наследуется следующей сессией
  emulog_end("EP");
}
// Мост к libretro-common для ядра Stella (a2600).
// KidVid.hxx (плеер сэмплов) вызывает rfgetc; остальные rf* — rfopen,
// rfclose, rfseek, rftell, rfread, rfwrite, rfgets — уже определены
// libretro-common проекта (ядра fuse/cap32/gpgx), поэтому здесь ТОЛЬКО
// недостающая rfgetc: иначе межобъектный дубль (linkcheck, r1525).
#include "streams/file_stream.h"

// EOF: KidVid в сборке H3 не активен (нет SD-доступа к сэмплам).
int rfgetc(RFILE* stream)
{
    (void)stream;
    return -1;
}

#ifndef MSX_RENAME_H
#define MSX_RENAME_H
/* msx_rename.h — префикс общих символов fMSX, чтобы не конфликтовать
 * с другими ядрами в одной прошивке (FCEUmm RAM, Snes9x CPU/LoadROM,
 * GPGX rf*, GBA time/localtime, libretro_compat sscanf/strlcpy и т.д.).
 * Подключается через -include ТОЛЬКО к файлам сборки MSX (Makefile),
 * поэтому другие ядра этих переименований не видят. */

/* глобалы/функции ядра fMSX */
#define CPU        msx_CPU
#define RAM        msx_RAM
#define LoadROM    msx_LoadROM

/* файловые стабы (конфликт с gpgx_missing.c) */
#define rfopen             msx_rfopen
#define rfclose            msx_rfclose
#define rfread             msx_rfread
#define rfwrite            msx_rfwrite
#define rfseek             msx_rfseek
#define rftell             msx_rftell
#define rfgets             msx_rfgets
#define rfeof              msx_rfeof
#define rfgetc             msx_rfgetc
#define rfputc             msx_rfputc
#define filestream_rewind  msx_filestream_rewind

/* libc-дополнения (конфликт с fceumm_libretro_compat / gba_compat) */
#define sscanf             msx_sscanf
#define time               msx_time
#define localtime          msx_localtime
#define strlcpy            msx_strlcpy
#define fill_pathname_join msx_fill_pathname_join

#endif
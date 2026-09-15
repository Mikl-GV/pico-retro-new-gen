// handy.h — stub for H3 bare-metal
#ifndef HANDY_H3_H
#define HANDY_H3_H

#define HANDYVER "0.97"
#define ROM_FILE "lynxboot.img"

enum retro_log_level { RETRO_LOG_INFO, RETRO_LOG_ERROR, RETRO_LOG_WARN };

static inline void handy_log(enum retro_log_level level, const char *format, ...) {
    (void)level; (void)format;
}

#endif
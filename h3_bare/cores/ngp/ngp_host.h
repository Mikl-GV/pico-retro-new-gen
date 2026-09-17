#ifndef NGP_HOST_H
#define NGP_HOST_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int ngp_init_game(const uint8_t* rom, uint32_t size);
void ngp_run_frame(void);

#ifdef __cplusplus
}
#endif

#endif
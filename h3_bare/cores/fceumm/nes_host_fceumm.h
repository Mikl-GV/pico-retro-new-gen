#ifndef NES_HOST_FCEUMM_H
#define NES_HOST_FCEUMM_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int fceumm_init_game(const uint8_t* rom, uint32_t size);
void fceumm_run_frame(void);
void fceumm_set_suborkb_keys(const uint8_t* keys, int n);

#ifdef __cplusplus
}
#endif
#endif
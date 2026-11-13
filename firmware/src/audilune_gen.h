#ifndef AUDILUNE_GEN_H
#define AUDILUNE_GEN_H

#include <stdint.h>

/* Block 4: Step-level FX locks (OP1 - OP6) */
typedef enum {
    STEP_FX_NONE = 0,
    STEP_FX_STUTTER,
    STEP_FX_TAPE_STOP,
    STEP_FX_CRUSH,
    STEP_FX_REVERSE,
    STEP_FX_WASH,
    STEP_FX_OCT_SLAM
} audilune_step_fx_t;

/* Initializes the generative scale mask and RNG state */
void audilune_gen_init(void);

/* Scans non-generative tracks to build a live harmonic scale mask */
void audilune_gen_update_scale_mask(const uint8_t *active_notes, uint8_t count);

/* Clamps a drifting note into the active harmonic scale mask */
uint8_t audilune_gen_clamp_note(uint8_t raw_note);

/* Generates step triggers and notes based on the active Macro-Genre (0-5) */
uint8_t audilune_gen_eval_step(uint8_t track, uint8_t step, uint8_t genre, uint8_t *out_note, uint8_t *out_vel);

#endif /* AUDILUNE_GEN_H */

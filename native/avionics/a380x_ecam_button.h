/* SPDX-License-Identifier: GPL-3.0-or-later
 * Reusable, SDK-independent momentary pushbutton motion. Not a page latch.
 * OBJ8 maps ratio 0..1 onto the inspected model's local pressing vector.
 */
#ifndef A380X_ECAM_BUTTON_H
#define A380X_ECAM_BUTTON_H
#include <math.h>
#include <string.h>

typedef struct {
    const char *id;
    const char *label;
    int page;                 /* -1 for action keys; selection stays in SD. */
    double press_seconds;
    double return_seconds;
} EcamButtonConfig;

typedef struct {
    float ratio, start_ratio;
    double elapsed;
    int phase;                /* 0 rest, 1 inward, 2 automatic return */
    unsigned activations;
} EcamButtonMotion;

static inline void ecam_button_reset(EcamButtonMotion *motion)
{ memset(motion,0,sizeof(*motion)); }

static inline void ecam_button_trigger(EcamButtonMotion *motion)
{
    /* A second click starts at the current physical position, never jumps. */
    if(!isfinite(motion->ratio))motion->ratio=0;
    motion->start_ratio=fminf(1,fmaxf(0,motion->ratio));
    motion->elapsed=0;motion->phase=1;++motion->activations;
}

static inline void ecam_button_update(EcamButtonMotion *motion,
                                      const EcamButtonConfig *config,double dt)
{
    if(!isfinite(dt)) { motion->ratio=0;motion->phase=0;motion->elapsed=0;return; }
    if(dt<=0 || !motion->phase)return;
    double inward=isfinite(config->press_seconds)&&config->press_seconds>0?config->press_seconds:.10;
    double outward=isfinite(config->return_seconds)&&config->return_seconds>0?config->return_seconds:.15;
    motion->elapsed+=dt;
    /* Carry excess time over the phase boundary: no frame-count dependence. */
    if(motion->phase==1 && motion->elapsed>=inward) {
        motion->elapsed-=inward;motion->phase=2;
    }
    if(motion->phase==2 && motion->elapsed>=outward) {
        motion->ratio=0;motion->elapsed=0;motion->phase=0;return;
    }
    double t=motion->elapsed/(motion->phase==1?inward:outward);
    float eased=(float)(t*t*(3-2*t));
    motion->ratio=motion->phase==1?motion->start_ratio+(1-motion->start_ratio)*eased:1-eased;
}
#endif

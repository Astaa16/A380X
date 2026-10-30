/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef A380X_XPKIT_SHARED_H
#define A380X_XPKIT_SHARED_H
#ifdef __cplusplus
#include <atomic>
#define XPKIT_ATOMIC(type) std::atomic<type>
#else
#include <stdatomic.h>
#define XPKIT_ATOMIC(type) _Atomic type
#endif
#include <stdint.h>
#include "a380x_mfd_types.h"
#define XPKIT_MAGIC 0x58333830u
#define XPKIT_VERSION 6u
#define XPKIT_VIEWS 7
#define XPKIT_SD_CAP 65536
/* v6: PFDs are captured at their full 768x1024 logical size (were 384x512). */
#define XPKIT_W 768
#define XPKIT_H 1024
#define XPKIT_BYTES (XPKIT_W * XPKIT_H * 4)
#define XPKIT_ND_W 768
#define XPKIT_ND_H 1024
#define XPKIT_ND_BYTES (XPKIT_ND_W * XPKIT_ND_H * 4)
#define XPKIT_JSON_CAP 262144
#define XPKIT_MFD_BYTES (MFD_W * MFD_H * 4)
typedef struct {
    XPKIT_ATOMIC(uint32_t) sequence;
    uint32_t powered;
    char uri[64];
    MfdDrawList list;
} A380XXpKitMfd;
typedef struct { XPKIT_ATOMIC(uint32_t) sequence; uint32_t size; char json[XPKIT_SD_CAP]; } A380XXpKitSd;
/* Read-only diagnostics: helper writes capture/apply fields; plugin writes upload/draw fields. */
typedef struct {
    XPKIT_ATOMIC(uint64_t) snapshot_count, snapshot_ns, apply_count, apply_ns, skipped_count;
    XPKIT_ATOMIC(uint64_t) upload_count, upload_bytes, draw_count;
} A380XXpKitNdProfile;
typedef struct {
    uint32_t magic, version;
    XPKIT_ATOMIC(uint32_t) state_seq;
    uint32_t state_size;
    double state_time;
    char state[XPKIT_JSON_CAP];
    XPKIT_ATOMIC(uint32_t) ready[XPKIT_VIEWS], frame_seq[XPKIT_VIEWS], errors;
    double frame_time[XPKIT_VIEWS];
    XPKIT_ATOMIC(int) zoom_delta[2];
    XPKIT_ATOMIC(uint32_t) nd_status[2][8];
    char error[512];
    unsigned char pixels[2][XPKIT_BYTES];
    unsigned char nd_pixels[2][XPKIT_ND_BYTES];
    unsigned char mfd_pixels[3][XPKIT_MFD_BYTES];
    A380XXpKitMfd mfd[2];
    A380XXpKitSd sd;
    A380XXpKitNdProfile nd_profile[2];
} A380XXpKitShared;
static inline int xpkit_view_width(int view) { return view < 2 ? XPKIT_W : view < 4 ? XPKIT_ND_W : MFD_W; }
static inline int xpkit_view_height(int view) { return view < 2 ? XPKIT_H : view < 4 ? XPKIT_ND_H : MFD_H; }
static inline size_t xpkit_view_bytes(int view) { return view < 2 ? XPKIT_BYTES : view < 4 ? XPKIT_ND_BYTES : XPKIT_MFD_BYTES; }
static inline unsigned char *xpkit_view_pixels(A380XXpKitShared *s, int view) {
    return view < 2 ? s->pixels[view] : view < 4 ? s->nd_pixels[view - 2] : s->mfd_pixels[view - 4];
}
#endif

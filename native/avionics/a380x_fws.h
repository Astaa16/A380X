/* SPDX-License-Identifier: GPL-3.0-or-later
 * Native Flight Warning System core: the FWS alert life cycle of the pinned FlyByWire A380X
 * (systems-host/CpiomC/FlightWarningSystem/FwsCore.ts update(), lines 5468-6155: MASTER CAUT/WARN
 * buttons, the abnormal-sensed + activated abnormal-non-sensed loop with presented / cleared /
 * recalled lists, deferred procedures, CRC / single-chime aural requests, master warning / caution
 * latching, memos, STS lists, ECAM SFAIL), glued to
 *   a380x_fws_signals.* (signals, alert conditions, memos, STS dictionaries) and
 *   a380x_fws_ecl.*     (ECP keys, normal / abnormal checklists, EWD composition),
 * and the EWD warning-display view (a380x_ewd_wd.h). XPLM-free; the X-Plane boundary is
 * a380x_fws_adapter.inc.
 */
#ifndef A380X_FWS_H
#define A380X_FWS_H
#include "a380x_fws_signals.h"
#include "a380x_fws_ecl.h"
#include "a380x_ewd_wd.h"

#define FWS_MEMO_LINES 10       /* ewdMessageLinesLeft / Right */
#define FWS_PFD_MEMO_LINES 3

typedef struct {
    FwsSignals sig;
    FwsSignalOutputs out;
    FwsEcl ecl;
    FwsEclCoreLists lists;
    FwsEclInput ecl_in;
    FwsStatusLists status;

    unsigned memo_left[FWS_MEMO_LINES], memo_right[FWS_MEMO_LINES], memo_pfd[FWS_PFD_MEMO_LINES];
    int memo_left_count, memo_right_count, memo_pfd_count;

    /* FwsCore members of the life cycle */
    unsigned char request_master_warning_from_faults, request_master_caution_from_faults;
    int non_cancellable_warning_count;
    unsigned char aural_crc_active, aural_single_chime;
    FwsEclKeyList aural_crc_keys;
    double single_chime_timer;               /* singleChimeMtrig (NXLogicTriggeredMonostableNode(2)) */
    unsigned char single_chime_previous, single_chime_memory;
    unsigned char master_warning, master_caution;
    signed char sfail;                       /* L:A32NX_ECAM_SFAIL, -1 none */
    unsigned char failure_active;            /* L:A32NX_ECAM_FAILURE_ACTIVE */
    unsigned char sd_status_request;         /* FWS_ECL_EVENT_SD_STATUS_PAGE seen this cycle */
    unsigned char sts_indication;            /* ecamEwdShowStsIndication */
    unsigned char master_warning_pb_last, master_caution_pb_last;
    unsigned char flaps_handle_last, baro_std_last, approach_pulse_seen;
    unsigned char powered, started;
    double clock;
} Fws;

/* Per-cycle inputs that are not part of FwsInputs. */
typedef struct {
    FwsEcpButtons ecp;                        /* L:A32NX_BTN_* levels (both ECP sides merged) */
    unsigned char sd_status_shown;            /* SD STATUS page is the current page */
    unsigned char baro_std;                   /* PFD baro reference STD (approach auto display) */
} FwsCycleInput;

void fws_init(Fws *f);
/* One FWS cycle. powered = 0 resets the FWS (FBW: unpowered CPIOM) and publishes nothing. */
void fws_update(Fws *f, const FwsInputs *in, const FwsCycleInput *cycle, int powered,
                double clock_s, float dt);
/* Lower EWD view for this frame (memos, limitations, ECL window, STS area). */
void fws_ewd_view(const Fws *f, EwdWdView *out);
#endif

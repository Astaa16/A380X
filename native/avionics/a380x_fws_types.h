/* SPDX-License-Identifier: GPL-3.0-or-later
 * Shared types of the native Flight Warning System (FWS) port and the EWD warning display.
 *
 * Mirrors the pinned FlyByWire A380X contracts so the port stays a translation:
 *  - ChecklistLineStyle / WdSpecialLine / WdLineData
 *      fbw-a380x/src/systems/instruments/src/MsfsAvionicsCommon/EcamMessages/index.ts
 *  - ChecklistState
 *      fbw-a380x/src/systems/instruments/src/MsfsAvionicsCommon/providers/FwsPublisher.ts
 * Text fields keep FBW's formatting escape codes (\x1b<Nm colours, \x1b4m underline, \x1bm reset,
 * ...); the EWD renderer interprets them as FBW's FormattedFwcText does.
 * XPLM-free.
 */
#ifndef A380X_FWS_TYPES_H
#define A380X_FWS_TYPES_H

#define FWS_WD_NUM_LINES 18      /* WD_NUM_LINES */
#define FWS_WD_LINE_CHARACTERS 41 /* WD_LINE_CHARACTERS: without the leading checkbox and space */
#define FWS_MAX_ITEMS 80          /* >= the longest FBW procedure (340900003: 71 items; checked by the table generator) */
#define FWS_TEXT_MAX 112          /* one formatted line, escape codes included */

/* ChecklistLineStyle, same order as the FBW enum. */
typedef enum {
    FWS_STYLE_STANDARD,
    FWS_STYLE_CYAN,
    FWS_STYLE_GREEN,
    FWS_STYLE_GREEN_TABLE,
    FWS_STYLE_AMBER,
    FWS_STYLE_WHITE,
    FWS_STYLE_RED,
    FWS_STYLE_HEADLINE,
    FWS_STYLE_SUB_HEADLINE,
    FWS_STYLE_CENTERED_SUB_HEADLINE,
    FWS_STYLE_SEPARATION_LINE,
    FWS_STYLE_EMPTY,
    FWS_STYLE_CHECKLIST_ITEM,
    FWS_STYLE_COMPLETED_CHECKLIST,
    FWS_STYLE_COMPLETED_DEFERRED_PROCEDURE,
    FWS_STYLE_DEFERRED_PROCEDURE,
    FWS_STYLE_OMISSION_DOTS,
    FWS_STYLE_LAND_ASAP,
    FWS_STYLE_LAND_ANSA,
    FWS_STYLE_CHECKLIST_CONDITION,
    FWS_STYLE_CHECKLIST_ITEM_INACTIVE,
    FWS_STYLE_COUNT
} FwsLineStyle;

/* WdSpecialLine (+ NONE for an absent specialLine). */
typedef enum {
    FWS_SPECIAL_NONE = -1,
    FWS_SPECIAL_CL_COMPLETE = 0,
    FWS_SPECIAL_RESET,
    FWS_SPECIAL_CLEAR,
    FWS_SPECIAL_EMPTY,
    FWS_SPECIAL_SEPARATION_LINE
} FwsSpecialLine;

/* Procedure kinds of ProcedureLinesGenerator (ProcedureType). */
typedef enum {
    FWS_PROC_NORMAL,
    FWS_PROC_ABNORMAL,
    FWS_PROC_DEFERRED,
    FWS_PROC_FWS_FAILED_FALLBACK
} FwsProcedureType;

/* ChecklistState: per-procedure item state shared by the FWS and the WD line generator.
 * timestamp[i] is seconds of the FWS clock, valid only when timestamp_valid[i]. */
typedef struct {
    unsigned id;
    unsigned char procedure_completed, procedure_activated;
    unsigned char items_checked[FWS_MAX_ITEMS];
    unsigned char items_to_show[FWS_MAX_ITEMS];
    unsigned char items_active[FWS_MAX_ITEMS];
    unsigned char timestamp_valid[FWS_MAX_ITEMS];
    double items_timestamp[FWS_MAX_ITEMS];
} FwsChecklistState;

/* WdLineData. item_index is originalItemIndex (FBW special indices are negative, e.g. -7 CLEAR);
 * has_item_index is 0 when FBW leaves originalItemIndex undefined. */
typedef struct {
    char text[FWS_TEXT_MAX];
    unsigned procedure_id;          /* 0 when undefined */
    int item_index;
    unsigned char has_item_index;
    unsigned char style;            /* FwsLineStyle */
    signed char special;            /* FwsSpecialLine */
    unsigned char active_procedure, sensed, checked, first_line, last_line;
    unsigned char abnormal_procedure, inactive;
} FwsWdLine;

#endif

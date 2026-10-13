/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * A380X MFD shared types: sides, KCCU keys, palette, fonts, rectangles, widget ids and the
 * display-list data structures (docs/mfd-spec/12-architecture.md sections 2 and 4,
 * docs/mfd-spec/01-framework-widgets.md sections 0, 3, 5.2 and 12.5).
 *
 * Simulator independent: no XPLM, no OpenGL. Canvas = 768 x 1024 px per side, origin top-left,
 * y down, float pixels.
 */
#ifndef A380X_MFD_TYPES_H
#define A380X_MFD_TYPES_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Canvas --------------------------------------------------------------------------- */

#define MFD_W 768
#define MFD_H 1024

/* Frame regions (spec 01 section 5.1). */
#define MFD_HEADER_ROW1_Y0 0.0f
#define MFD_HEADER_ROW2_Y0 44.0f
#define MFD_TITLE_Y0 98.0f
#define MFD_BODY_Y0 138.0f   /* page body 138..950 (812 px) */
#define MFD_BODY_Y1 950.0f
#define MFD_FOOTER_Y0 950.0f /* footer 950..1024 */

typedef struct { float x, y, w, h; } MfdRect;
typedef struct { float x, y; } MfdPoint;

static inline MfdRect mfd_rect(float x, float y, float w, float h)
{
    MfdRect r;
    r.x = x; r.y = y; r.w = w; r.h = h;
    return r;
}

static inline int mfd_rect_contains(MfdRect r, float x, float y)
{
    return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h;
}

static inline MfdRect mfd_rect_intersect(MfdRect a, MfdRect b)
{
    float x0 = a.x > b.x ? a.x : b.x, y0 = a.y > b.y ? a.y : b.y;
    float x1 = (a.x + a.w) < (b.x + b.w) ? (a.x + a.w) : (b.x + b.w);
    float y1 = (a.y + a.h) < (b.y + b.h) ? (a.y + a.h) : (b.y + b.h);
    MfdRect r;
    r.x = x0; r.y = y0; r.w = x1 > x0 ? x1 - x0 : 0.0f; r.h = y1 > y0 ? y1 - y0 : 0.0f;
    return r;
}

/* ---- Sides and keys (spec 01 sections 1.1, 12.5) --------------------------------------- */

typedef enum { MFD_CAPT = 0, MFD_FO = 1 } MfdSide;
#define MFD_SIDE_COUNT 2

typedef enum {
    MFD_KEY_CHAR,          /* ch in 'A'..'Z', '0'..'9', ' ', '/', '.', '+', '-' */
    MFD_KEY_PLUSMINUS, MFD_KEY_ENT, MFD_KEY_BACKSPACE, MFD_KEY_ESC, MFD_KEY_ESC2,
    MFD_KEY_UP, MFD_KEY_DOWN, MFD_KEY_LEFT, MFD_KEY_RIGHT,
    MFD_KEY_DIR, MFD_KEY_PERF, MFD_KEY_INIT, MFD_KEY_NAVAID, MFD_KEY_MAILBOX, MFD_KEY_FPLN,
    MFD_KEY_DEST, MFD_KEY_SECINDEX, MFD_KEY_SURV, MFD_KEY_ATCCOM, MFD_KEY_ND,
    MFD_KEY_KBD, MFD_KEY_CLRINFO, MFD_KEY_REWIND, MFD_KEY_FORWARD,
    MFD_KEY_COUNT
} MfdKey;

/* ---- Palette (spec 01 section 3). The RGB values live in mfd_color_rgb(). --------------- */

typedef enum {
    MFD_COL_BLACK = 0,       /* #000000 display-background */
    MFD_COL_WHITE,           /* #ffffff */
    MFD_COL_CYAN,            /* #00ffff */
    MFD_COL_GREEN,           /* #00ff00 */
    MFD_COL_AMBER,           /* #e68000 */
    MFD_COL_YELLOW,          /* #ffff00 */
    MFD_COL_MAGENTA,         /* #ff94ff */
    MFD_COL_LIGHT_GREY,      /* #c4c6cf widget borders, validating text */
    MFD_COL_DARK_GREY,       /* #868a9c title bar, disabled text, opened button */
    MFD_COL_DARKER_GREY,     /* #272525 button / menu face */
    MFD_COL_DARK_BLUE,       /* #0c66f7 units */
    MFD_COL_RED,             /* #ff0000 */
    MFD_COL_GREY,            /* #4f5467 */
    MFD_COL_TITLE_TEXT,      /* #222222 */
    MFD_COL_SURV_STATUS_BG,  /* #3c3c3c */
    MFD_COL_EMER,            /* #ff9601 ATCCOM EMER label background */
    MFD_COL_SELECTOR_SHADOW, /* #717070 page-selector bottom/right border */
    MFD_COL_CSS_LIGHTGREY,   /* #d3d3d3 CSS lightgrey (tab slants, frames) */
    MFD_COL_CSS_GREY,        /* #808080 CSS grey (disabled arrows/icons) */
    MFD_COL_BEVEL_DARK_LG,   /* #74757b dark side of a #c4c6cf bevel */
    MFD_COL_BEVEL_DARK_CYAN, /* #00abab dark side of a cyan bevel */
    MFD_COL_BOOT_BLUE,       /* #4d4dff Thales boot block */
    MFD_COL_COUNT
} MfdColor;

/* 0xRRGGBB for a palette entry. */
static inline uint32_t mfd_color_rgb(MfdColor c)
{
    switch (c) {
    case MFD_COL_BLACK: return 0x000000u;
    case MFD_COL_WHITE: return 0xffffffu;
    case MFD_COL_CYAN: return 0x00ffffu;
    case MFD_COL_GREEN: return 0x00ff00u;
    case MFD_COL_AMBER: return 0xe68000u;
    case MFD_COL_YELLOW: return 0xffff00u;
    case MFD_COL_MAGENTA: return 0xff94ffu;
    case MFD_COL_LIGHT_GREY: return 0xc4c6cfu;
    case MFD_COL_DARK_GREY: return 0x868a9cu;
    case MFD_COL_DARKER_GREY: return 0x272525u;
    case MFD_COL_DARK_BLUE: return 0x0c66f7u;
    case MFD_COL_RED: return 0xff0000u;
    case MFD_COL_GREY: return 0x4f5467u;
    case MFD_COL_TITLE_TEXT: return 0x222222u;
    case MFD_COL_SURV_STATUS_BG: return 0x3c3c3cu;
    case MFD_COL_EMER: return 0xff9601u;
    case MFD_COL_SELECTOR_SHADOW: return 0x717070u;
    case MFD_COL_CSS_LIGHTGREY: return 0xd3d3d3u;
    case MFD_COL_CSS_GREY: return 0x808080u;
    case MFD_COL_BEVEL_DARK_LG: return 0x74757bu;
    case MFD_COL_BEVEL_DARK_CYAN: return 0x00ababu;
    case MFD_COL_BOOT_BLUE: return 0x4d4dffu;
    case MFD_COL_COUNT: break;
    }
    return 0xff00ffu;
}

/* Chromium Color::Dark() for the bevel colours used by the MFD (spec 01 section 13.1). */
static inline MfdColor mfd_color_dark(MfdColor c)
{
    if (c == MFD_COL_LIGHT_GREY) return MFD_COL_BEVEL_DARK_LG;
    if (c == MFD_COL_CYAN) return MFD_COL_BEVEL_DARK_CYAN;
    return c;
}

/* ---- Fonts and text metrics (spec 01 section 0.4, spec 12 section 2) -------------------- */

typedef enum {
    MFD_FONT_SZ = 0,  /* FBW-Display-EIS-A380-SlashedZero: default */
    MFD_FONT_ECAM = 1 /* FBW-Display-EIS-A380 (plain zero): callsign, page selectors, SURV heading */
} MfdFont;

#define MFD_FONT_ADVANCE 0.626953125f /* advance = 2568/4096 em, every glyph */
#define MFD_FONT_BASELINE 0.7998f     /* baseline below the line-box top, x size */
#define MFD_FONT_CAP 0.760f           /* digit / capital height, x size */

/* Typography classes of spec 01 section 4 (sizes in px). */
#define MFD_FS_LABEL 20.0f
#define MFD_FS_LABEL_BIGGER 24.0f
#define MFD_FS_LABEL_BIGGEST 28.0f
#define MFD_FS_VALUE 25.0f
#define MFD_FS_VALUE_SMALLER 20.0f
#define MFD_FS_VALUE_BIGGER 30.0f
#define MFD_FS_BUTTON 22.0f
#define MFD_FS_MENU 20.0f
#define MFD_FS_PAGE_SELECTOR 22.0f
#define MFD_FS_INPUT 27.0f
#define MFD_FS_INPUT_COMPUTED 22.0f
#define MFD_FS_TITLE 30.0f
#define MFD_FS_FOOTER_MSG 26.0f

typedef enum { MFD_ANCHOR_LEFT = 0, MFD_ANCHOR_CENTER = 1, MFD_ANCHOR_RIGHT = 2 } MfdAnchor;

/* ---- Widget ids (spec 12 section 5) ------------------------------------------------------ */

typedef uint32_t MfdId;
#define MFD_ID(scope, n) ((MfdId)((((uint32_t)(scope)) << 16) | (((uint32_t)(n)) & 0xffffu)))
#define MFD_ID_NONE ((MfdId)0)
/* Scopes 0x0001..0x7fff are page ids (MfdPageId values); the frame uses 0xff00 and above. */
#define MFD_SCOPE_FRAME 0xff00u
#define MFD_SCOPE_HEADER 0xff01u
#define MFD_SCOPE_FOOTER 0xff02u
#define MFD_SCOPE_OVERLAY 0xff03u
#define MFD_SCOPE_TEST 0xff7fu

/* ---- Display list (spec 12 section 4) ---------------------------------------------------- */

/* Layers = CSS z of spec 01 section 5.2. Renderers draw by ascending layer, emission order
 * inside a layer. */
typedef enum {
    MFD_LAYER_PAGE = 0,
    MFD_LAYER_TAB_SELECTED = 1, /* not used by the builder: tabs keep emission order instead */
    MFD_LAYER_OVERFLOW = 5,     /* InputField overflow box */
    MFD_LAYER_DIALOG = 10,      /* ContextMenu, ConfirmationDialog */
    MFD_LAYER_OVERLAY = 20,     /* MSG LIST, DUPLICATE NAMES, FMS PAGE NOT AVAIL */
    MFD_LAYER_MENU = 60,        /* every dropdown list */
    MFD_LAYER_CURSOR = 99
} MfdLayer;

#define MFD_DRAW_MAX_CMDS 4096
#define MFD_DRAW_MAX_POINTS 16384
#define MFD_DRAW_MAX_TEXT 32768
#define MFD_DRAW_MAX_CLIPS 256
#define MFD_DRAW_CLIP_DEPTH 16

typedef enum {
    MFD_CMD_FILL_RECT = 0,  /* rect */
    MFD_CMD_STROKE_RECT,    /* rect, width (stroke inside the rect) */
    MFD_CMD_LINE,           /* a[0..3] = x1 y1 x2 y2, width (butt caps) */
    MFD_CMD_POLYLINE,       /* points[first .. first+count), width, closed */
    MFD_CMD_POLYGON,        /* filled convex polygon, points[first .. first+count) */
    MFD_CMD_TRIANGLE,       /* filled triangle, 3 points */
    MFD_CMD_TEXT            /* text[first .. first+count) UTF-8, a[0], a[1] = anchor x, baseline y */
} MfdCmdKind;

typedef struct {
    uint8_t kind;   /* MfdCmdKind */
    uint8_t layer;  /* MfdLayer value */
    uint8_t color;  /* MfdColor */
    uint8_t font;   /* MfdFont (text) */
    uint8_t anchor; /* MfdAnchor (text) */
    uint8_t closed; /* polyline: 1 = closed */
    uint16_t clip;  /* index into MfdDrawList.clips; 0 = whole canvas */
    float a[4];     /* rect: x y w h; line: x1 y1 x2 y2; text: x baseline */
    float width;    /* stroke width (px) */
    float size;     /* font size (px) */
    uint32_t first; /* first point / first text byte */
    uint32_t count; /* point count / text byte count */
} MfdCmd;

/* Fixed-capacity display list. Overflow of any buffer sets the sticky `overflowed` flag and
 * drops the command (never written out of bounds). Clipping is resolved at emission time into
 * a per-command clip rectangle, so the renderer can reorder by layer safely. */
typedef struct {
    MfdCmd cmds[MFD_DRAW_MAX_CMDS];
    MfdPoint points[MFD_DRAW_MAX_POINTS];
    char text[MFD_DRAW_MAX_TEXT];
    MfdRect clips[MFD_DRAW_MAX_CLIPS];
    uint32_t cmd_count, point_count, text_len, clip_count;
    uint16_t clip_stack[MFD_DRAW_CLIP_DEPTH];
    int clip_depth;
    uint8_t layer; /* current layer for new commands */
    int overflowed;
} MfdDrawList;

#ifdef __cplusplus
}
#endif

#endif

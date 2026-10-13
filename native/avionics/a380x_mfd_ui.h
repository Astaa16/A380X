/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * A380X MFD per-side UI state and immediate-mode widgets
 * (docs/mfd-spec/12-architecture.md section 5, docs/mfd-spec/01-framework-widgets.md sections 11-13).
 *
 * Model: immediate-mode GUI with LAST-FRAME hit testing.
 *   1. Raw input (pointer move/down/up/wheel/leave, KCCU or PC keys) is queued at any time with
 *      mfd_ui_push_event() (the MFD top level does this from mfd_pointer_*() / mfd_key()).
 *   2. mfd_ui_begin() resolves queued events against the previous frame's hit list (topmost
 *      layer wins, then the latest emitted). It processes pointer moves freely and at most ONE
 *      consequential event (down, up, wheel or key) per frame, so every action sees the state
 *      produced by the previous one (e.g. a mouse-down outside a focused field commits the field
 *      in this frame, and the click on the new target is dispatched in the next frame).
 *   3. Pages call widgets. A widget draws itself into ui->dl, registers its hit rectangles for
 *      the NEXT frame and returns its interaction result for THIS frame.
 *   4. mfd_ui_end() swaps the hit lists, draws the cursor (layer 99) and drops the focus of a
 *      field that was not drawn this frame.
 *
 * Widget ids are MfdId = MFD_ID(scope, n): scope = your page id, n = any number unique inside
 * the page. A widget may register several "parts" (menu rows, list items) under one id.
 *
 * GEOMETRY NOTE (border-box): MSFS/Coherent sizes FBW boxes with box-sizing: border-box (the
 * reference screenshot shows the footer button `width:100px` as 100 px and MODIFY `width:125px`
 * as 125 px; specs 03 and 05 reach the same conclusion). Every rectangle passed to or returned
 * by these widgets is an OUTER (border-box) rectangle, and an FBW `width: W` / `height: H`
 * style maps to W / H directly. Spec 01 numbers that assumed content-box (InputField 49 px,
 * footer button 128 px, SurvButton 118x83, radio circle 34 px) are therefore 37 px, 100 px,
 * 100x65 and 30 px here.
 */
#ifndef A380X_MFD_UI_H
#define A380X_MFD_UI_H

#include "a380x_mfd_types.h"
#include "a380x_mfd_draw.h"
#include "a380x_mfd_formats.h"
#include "a380x_fms_messages.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Events ------------------------------------------------------------------------------ */

typedef enum {
    MFD_EV_MOVE = 0, /* x, y: canvas px */
    MFD_EV_DOWN,     /* primary button pressed at the current pointer position */
    MFD_EV_UP,       /* primary button released */
    MFD_EV_WHEEL,    /* wheel: notches, + = up */
    MFD_EV_LEAVE,    /* pointer left the screen */
    MFD_EV_KEY       /* key (MfdKey), ch for MFD_KEY_CHAR */
} MfdEventKind;

typedef struct {
    uint8_t kind;
    uint8_t key;
    char ch;
    int8_t wheel;
    float x, y;
} MfdEvent;

#define MFD_UI_MAX_EVENTS 64
#define MFD_UI_MAX_HITS 768
#define MFD_UI_VALIDATING 4
#define MFD_FIELD_MAX_CHARS 24 /* MAX_CHARACTERS_FREE_TEXT */
#define MFD_FIELD_BUF (MFD_FIELD_MAX_CHARS * 2 + 1)

/* Parts of composite widgets (MfdHit.part). Items of lists use their index (>= 0). */
#define MFD_PART_MAIN (-1)
#define MFD_PART_SCROLLBAR (-2)
#define MFD_PART_BLOCKER (-3) /* overlay/menu background that only swallows input */

typedef struct {
    MfdRect r;
    MfdId id;
    int16_t part;
    uint8_t layer;
    uint8_t flags;
} MfdHit;

typedef enum { MFD_EDIT_IDLE = 0, MFD_EDIT_FOCUSED, MFD_EDIT_EDITING } MfdEditState;

typedef struct {
    MfdId id;          /* 0 = free slot */
    double until;      /* commit time (now + 0.5 s) */
    int status;        /* MFD_PARSE_OK or MFD_PARSE_CLEAR */
    MfdValue value;    /* parsed value */
    char text[MFD_FIELD_BUF]; /* text shown in light grey while validating */
    int seen;          /* drawn this frame */
} MfdValidating;

/* Per-side UI state. Treat as opaque outside a380x_mfd_ui.c except for the documented read-only
 * fields (side, dl, now, dt, kbd_mode). */
typedef struct MfdUi {
    MfdSide side;
    MfdDrawList *dl;   /* display list widgets draw into */
    MfdMsgQueue *msgs; /* shared FMS message queue: format errors go here (may be NULL) */
    double now, dt;    /* seconds (adapter clock) */
    int kbd_mode;      /* 1 = KCCU KBD switch ON (no PC keyboard capture request) */

    /* event queue (ring) */
    MfdEvent events[MFD_UI_MAX_EVENTS];
    int ev_head, ev_count, ev_dropped;

    /* pointer and cursor */
    float px, py;
    int pointer_inside, button_down;
    double last_move;
    int cursor_visible;

    /* hit lists: [prev] is tested, [cur] is being built */
    MfdHit hits[2][MFD_UI_MAX_HITS];
    int hit_count[2];
    int hit_cur;
    int hits_overflowed;

    /* interaction facts of the current frame */
    MfdId hover_id; int hover_part;
    MfdId press_id; int press_part;   /* target of the button-down still held */
    MfdId down_id; int down_part;     /* mouse-down happened this frame on this target */
    MfdId click_id; int click_part;   /* press + release on the same target this frame */
    int click_happened;               /* a release happened this frame (outside-click logic) */
    MfdId wheel_id; int wheel_part; int wheel;
    int key_valid; MfdKey key; char key_ch;

    /* popups */
    MfdId open_menu;      /* Button menu / DropdownMenu / PageSelector currently open (0 = none) */
    float menu_scroll;    /* scroll offset of the open list, px */
    float menu_scroll_max;
    int menu_scroll_to;   /* -1 or an item index to scroll to when drawn */
    int menu_first_frame;
    MfdId ctx_menu; float ctx_x, ctx_y; double ctx_open_time;

    /* input field focus (one per side) */
    MfdId focus_id;
    MfdEditState edit;
    char buf[MFD_FIELD_BUF];
    double edit_t0;
    int overflow;
    int blur_pending;     /* mouse-down outside the focused field: commit when the field draws */
    int focus_seen;
    int focus_external;   /* focus owned by a DropdownMenu */
    double last_key_time;
    int kbd_capture;      /* PC keyboard capture requested (touchscreen mode, <= 20 s idle) */
    MfdValidating val[MFD_UI_VALIDATING];

    uint32_t frame;
} MfdUi;

/* ---- Lifecycle (called by the MFD top level; pages do not call these) --------------------- */

void mfd_ui_init(MfdUi *ui, MfdSide side, MfdDrawList *dl, MfdMsgQueue *msgs);
void mfd_ui_push_event(MfdUi *ui, const MfdEvent *ev);
void mfd_ui_begin(MfdUi *ui, double now);
void mfd_ui_end(MfdUi *ui);
/* Navigation: drop focus WITHOUT validation, close menus/context menu, cancel validations. */
void mfd_ui_reset_page(MfdUi *ui);
/* DU off: discard queued events, clear hover/press/focus, hide the cursor. */
void mfd_ui_suspend(MfdUi *ui);

/* ---- Interaction queries ----------------------------------------------------------------- */

/* Register a hit rectangle (clipped by the current draw clip) at the current draw layer. */
void mfd_ui_hit(MfdUi *ui, MfdId id, int part, MfdRect r);
int  mfd_ui_is_hovered(const MfdUi *ui, MfdId id);           /* any part */
int  mfd_ui_is_hovered_part(const MfdUi *ui, MfdId id, int part);
int  mfd_ui_was_clicked(const MfdUi *ui, MfdId id);          /* any part; returns 1 */
int  mfd_ui_clicked_part(const MfdUi *ui, MfdId id, int *part);
int  mfd_ui_wheel_notches(const MfdUi *ui, MfdId id);        /* wheel over id this frame (+ = up) */
/* The key event of this frame, if not consumed yet. */
int  mfd_ui_peek_key(const MfdUi *ui, MfdKey *key, char *ch);
void mfd_ui_consume_key(MfdUi *ui);
int  mfd_ui_menu_is_open(const MfdUi *ui, MfdId id);
void mfd_ui_close_menus(MfdUi *ui);
/* Push an FMS entry error (spec 02 section 1.7) to the shared queue. */
void mfd_ui_error(MfdUi *ui, const MfdFmsError *err, int with_details);
/* 1 while a field (or free-text dropdown) is focused and the PC keyboard should be captured. */
int  mfd_ui_wants_keyboard(const MfdUi *ui);
int  mfd_ui_has_focus(const MfdUi *ui);

/* Map a KCCU command suffix ("a".."z", "0".."9", "dot", "plusminus", "ent", "backspace", "sp",
 * "slash", "esc", "esc2", "up", ..., "clrinfo") to a key. Returns 0 if unknown. */
int mfd_key_from_name(const char *name, MfdKey *key, char *ch);

/* ---- Text helpers ------------------------------------------------------------------------ */

typedef enum {
    MFD_TXT_LABEL = 0,      /* .mfd-label 20 white */
    MFD_TXT_LABEL_BIGGER,   /* .mfd-label.bigger 24 white */
    MFD_TXT_LABEL_BIGGEST,  /* .mfd-label.biggest 28 white */
    MFD_TXT_UNIT,           /* .mfd-label-unit 20 #0c66f7 */
    MFD_TXT_UNIT_BIGGER,    /* .mfd-label-unit.bigger 24 */
    MFD_TXT_VALUE,          /* .mfd-value 25 green */
    MFD_TXT_VALUE_SMALLER,  /* .mfd-value.smaller 20 green */
    MFD_TXT_VALUE_BIGGER,   /* .mfd-value.bigger 30 green */
    MFD_TXT_BUTTON          /* .mfd-button 22 white */
} MfdTextClass;

float    mfd_text_class_size(MfdTextClass cls);
MfdColor mfd_text_class_color(MfdTextClass cls);
/* Raw text at a baseline point (SZ font). */
void  mfd_text(MfdUi *ui, float x, float baseline, MfdAnchor anchor, float size, MfdColor c, const char *s);
/* Text of a typography class whose 1.0-em line box TOP is `top` (CSS-like placement).
 * color = MFD_COL_COUNT keeps the class colour. Returns the text width. */
float mfd_label(MfdUi *ui, MfdTextClass cls, MfdColor color, MfdAnchor anchor, float x, float top, const char *s);
/* Horizontal rule [x0, x1) x [y, y + thickness). */
void  mfd_hrule(MfdUi *ui, float x0, float x1, float y, float thickness, MfdColor c);
/* Solid frame of width w drawn inside r. */
void  mfd_frame_rect(MfdUi *ui, MfdRect r, float w, MfdColor c);

/* ---- Button (spec 01 section 13.2) --------------------------------------------------------- */

typedef struct {
    const char *label;
    int disabled;
} MfdMenuItem;

#define MFD_BTN_DISABLED 0x0001u
#define MFD_BTN_SELECTED 0x0002u     /* segmented control: grey face, sunken */
#define MFD_BTN_HIGHLIGHTED 0x0004u  /* white face, dark text */
#define MFD_BTN_HIDDEN 0x0008u       /* visible=false: not drawn, not hit-testable */
#define MFD_BTN_NO_ARROW 0x0010u     /* menu button without the triangle (showArrow=false) */
#define MFD_BTN_MENU_RIGHT 0x0020u   /* dropdownMenuRightAligned */
#define MFD_BTN_TEXT_YELLOW 0x0040u  /* buttonStyle color: yellow (TMPY F-PLN) */

#define MFD_BUTTON_NONE (-1)
#define MFD_BUTTON_CLICKED (-2)
#define MFD_BUTTON_H 40.0f

typedef struct {
    const char *label;     /* '\n' separates lines; each line centred */
    const char *suffix;    /* optional right-aligned text on the label row (e.g. "*") */
    float inner_w;         /* width of the FBW label row (space-between); 0 = whole content box */
    float label_dx;        /* with suffix: left margin of `label` inside the row */
    float pad_right;       /* CSS padding-right; 0 = default (12, or 5 for menu buttons) */
    MfdColor color;        /* text colour override; MFD_COL_COUNT = default */
    uint32_t flags;        /* MFD_BTN_* */
    const MfdMenuItem *menu;
    int menu_count;
    int scroll_to;         /* scrollToMenuItem on open (-1 = none) */
} MfdButtonDesc;

/* Outer width of an auto-sized button: max line width at 22 px + 2x12 padding + 2x2 border. */
float mfd_button_auto_width(const char *label);
/* Outer width of an auto-sized menu button (label + 51). */
float mfd_menu_button_auto_width(const char *label);
static inline float mfd_button_height(int lines) { return 18.0f + 22.0f * (float)lines; }

void mfd_button_desc_init(MfdButtonDesc *d, const char *label);
/* Full button. r = outer box (r.w <= 0: auto width; r.h <= 0: 18 + 22 x lines). Returns
 * MFD_BUTTON_NONE, MFD_BUTTON_CLICKED (enabled button clicked; a menu button also toggles its
 * menu) or the index (>= 0) of the selected enabled menu item. */
int mfd_button_ex(MfdUi *ui, MfdId id, MfdRect r, const MfdButtonDesc *d);
/* Plain button: returns 1 when clicked (never when disabled or hidden). */
int mfd_button(MfdUi *ui, MfdId id, MfdRect r, const char *label, uint32_t flags);
/* Menu button: returns the selected item index or -1. */
int mfd_menu_button(MfdUi *ui, MfdId id, MfdRect r, const char *label, const MfdMenuItem *items, int n,
                    uint32_t flags);

/* ---- IconButton (spec 01 section 13.3) ---------------------------------------------------- */

typedef enum {
    MFD_ICON_SINGLE_UP, MFD_ICON_SINGLE_DOWN, MFD_ICON_SINGLE_LEFT, MFD_ICON_SINGLE_RIGHT,
    MFD_ICON_DOUBLE_UP, MFD_ICON_DOUBLE_DOWN, MFD_ICON_DOUBLE_LEFT, MFD_ICON_DOUBLE_RIGHT,
    MFD_ICON_FLAT_SINGLE_UP, MFD_ICON_FLAT_SINGLE_DOWN, MFD_ICON_FLAT_SINGLE_LEFT, MFD_ICON_FLAT_SINGLE_RIGHT,
    MFD_ICON_ECL_SINGLE_UP, MFD_ICON_ECL_SINGLE_DOWN
} MfdIcon;

/* Draw a 35x35 icon whose top-left is (x, y). */
void mfd_draw_icon(MfdDrawList *dl, MfdIcon icon, float x, float y, MfdColor c);
/* 15x15 TriangleDown / TriangleUp of shapes.tsx at (x, y). */
void mfd_draw_triangle_down(MfdDrawList *dl, float x, float y, MfdColor c);
void mfd_draw_triangle_up(MfdDrawList *dl, float x, float y, MfdColor c);
/* Icon button: r = outer box (e.g. 50x50); icon centred. Returns 1 when clicked (not disabled). */
int mfd_icon_button(MfdUi *ui, MfdId id, MfdRect r, MfdIcon icon, int disabled);

/* ---- InputField (spec 01 section 13.4, spec 02 section 1) --------------------------------- */

#define MFD_FIELD_MANDATORY 0x0001u   /* empty value -> amber U+25AF boxes */
#define MFD_FIELD_INACTIVE 0x0002u    /* static green text, no box, not focusable */
#define MFD_FIELD_DISABLED 0x0004u    /* grey box, not focusable */
#define MFD_FIELD_NOT_CLEARABLE 0x0008u /* canBeCleared = false */
#define MFD_FIELD_TMPY 0x0010u        /* tmpyActive: yellow text */
#define MFD_FIELD_BIG_UNIT 0x0020u    /* units at 24 px */
#define MFD_FIELD_FREE_TEXT 0x0040u   /* up to 24 characters, overflow box */
#define MFD_FIELD_YELLOW_WHEN_DISABLED 0x0080u
#define MFD_FIELD_TRANSPARENT_BORDER 0x0100u /* DropdownMenu's embedded field */
#define MFD_FIELD_NO_ERRORS 0x0200u   /* no-op error handler (dropdown free text) */

typedef struct {
    MfdFmt fmt;        /* format descriptor (a380x_mfd_formats.h); fmt.err_details selects details */
    uint32_t flags;    /* MFD_FIELD_* */
    float width;       /* outer width (FBW containerStyle width); 0 = auto from maxDigits + units */
    MfdAnchor align;   /* alignText (default MFD_ANCHOR_CENTER) */
    const char *mandatory_glyph; /* overrideEmptyMandatoryPlaceholder; NULL = U+25AF */
} MfdFieldDesc;

typedef struct {
    int has_value;     /* 0 = null (placeholder) */
    int is_computed;   /* 1 = enteredByPilot=false: 22 px font */
    double num;        /* numeric formats */
    const char *text;  /* text formats (may be NULL when has_value = 0) */
} MfdFieldValue;

typedef enum { MFD_FIELD_NONE = 0, MFD_FIELD_COMMITTED, MFD_FIELD_CLEARED } MfdFieldResult;

#define MFD_FIELD_H 37.0f

void mfd_field_desc_init(MfdFieldDesc *d, MfdFmtKind kind);
/* Auto outer width for a descriptor (8 + lead + 6 + max(18 x maxDigits, text) + 6 + trail). */
float mfd_field_auto_width(const MfdFieldDesc *d, const MfdFieldValue *v);
/* Draws the field with its top-left at (x, y) (height 37) and runs its state machine.
 * Returns MFD_FIELD_COMMITTED with *out filled (already parsed and validated by the format),
 * MFD_FIELD_CLEARED (parse('') -> null), or MFD_FIELD_NONE. Format errors are pushed to the
 * shared message queue; the page never sees invalid input. Range checks that depend on FMS
 * state are the page's job: forward the FMS error with mfd_ui_error(). */
MfdFieldResult mfd_input_field(MfdUi *ui, MfdId id, float x, float y, const MfdFieldDesc *d,
                               const MfdFieldValue *v, MfdValue *out);

/* ---- DropdownMenu (spec 01 section 13.5) -------------------------------------------------- */

typedef struct {
    const char *const *values;
    int count;
    int selected;        /* -1 = none (placeholder dashes) */
    int free_text;       /* freeTextAllowed */
    int align_left;      /* alignLabels = flex-start (value at x + 12, items left) */
    int inactive, disabled, tmpy;
    int digits;          /* numberOfDigitsForInputField (0 = 6) */
} MfdDropdownDesc;

#define MFD_DROPDOWN_H 44.0f

/* r = container box (r.h ignored: 44). Returns 1 when the selection changed: *out_index = the
 * original index, or -1 with the free text in out_text (n bytes) for free-text entries. */
int mfd_dropdown(MfdUi *ui, MfdId id, MfdRect r, const MfdDropdownDesc *d, int *out_index, char *out_text,
                 size_t n);

/* ---- PageSelectorDropdownMenu (spec 01 section 13.6) ---------------------------------------- */

#define MFD_PSEL_EMER 0x0001u /* ATCCOM EMER: black text on #ff9601 */

/* r = selector box (normally 54 px tall). label may contain '\n' (two-line variant).
 * More than one item: toggles a menu and returns the chosen enabled item. Exactly one item:
 * returns 0 when clicked. Otherwise -1. */
int mfd_page_selector(MfdUi *ui, MfdId id, MfdRect r, const char *label, const MfdMenuItem *items, int n,
                      int active, uint32_t flags);

/* ---- TopTabNavigator (spec 01 section 13.7) ------------------------------------------------- */

typedef struct {
    float bar_h;            /* tabBarHeight, default 36 */
    float slant_deg;        /* tabBarSlantedEdgeAngle, default 30 */
    float right_space;      /* additionalRightSpace */
    int highlighted;        /* highlightedTab (-1 = none) */
    MfdColor selected_text; /* selectedTabTextColor (default white) */
    float content_h;        /* outer height of the content frame; 0 = down to region bottom - 2 */
} MfdTabsDesc;

void mfd_tabs_desc_init(MfdTabsDesc *d);
/* Draws the tab bar and the content frame in `region` (container margin 2). Returns the clicked
 * tab index or -1. *content = content box (inside the 2 px border and 10 px padding);
 * *clip = padding box (FBW overflow: hidden), both may be NULL. */
int mfd_top_tabs(MfdUi *ui, MfdId id, MfdRect region, const char *const *titles, int n, int selected,
                 const MfdTabsDesc *d, MfdRect *content, MfdRect *clip);

/* ---- RadioButtonGroup (spec 01 section 13.8) ------------------------------------------------ */

/* Rows of 34 px (pitch 39; with extra_spacing s every row gets margin-top s). Returns the clicked
 * enabled index or -1. disabled_mask bit i disables row i. color: MFD_COL_CYAN default. */
int mfd_radio_group(MfdUi *ui, MfdId id, float x, float y, float w, const char *const *labels, int n,
                    int selected, uint32_t disabled_mask, MfdColor color, float extra_spacing);

/* ---- ContextMenu (spec 01 section 13.9) ------------------------------------------------------ */

void mfd_context_menu_open(MfdUi *ui, MfdId id, float x, float y);
int  mfd_context_menu_is_open(const MfdUi *ui, MfdId id);
/* Draw (when open) and return the clicked enabled item or -1. Closes on any click > 100 ms
 * after opening. */
int  mfd_context_menu(MfdUi *ui, MfdId id, const MfdMenuItem *items, int n);

/* ---- ConfirmationDialog (spec 01 section 13.10) ---------------------------------------------- */

#define MFD_DIALOG_NONE 0
#define MFD_DIALOG_CANCEL 1
#define MFD_DIALOG_CONFIRM 2
/* box = .mfd-dialog border box. Draws at layer 10 and swallows clicks inside the box. */
int mfd_confirm_dialog(MfdUi *ui, MfdId id, MfdRect box, const char *title, int amber_title);

/* ---- SURV widgets (spec 01 section 13.12) --------------------------------------------------- */

/* 100x65 two-state toggle at (x, y). Returns 1 when clicked (enabled): caller sets !state. */
int  mfd_surv_button(MfdUi *ui, MfdId id, float x, float y, const char *label_true, const char *label_false,
                     int state, int disabled);
int  mfd_adsc_button(MfdUi *ui, MfdId id, float x, float y, const char *label_true, const char *label_false,
                     int state, int disabled);
/* 90x60 status button with three indicator bars; r = outer box. Returns 1 when clicked. */
int  mfd_surv_status_button(MfdUi *ui, MfdId id, MfdRect r, const char *label, int active);
/* `${label} ${active ? sys : 'OFF'}`, SZ 24; green when active, amber when failed. */
void mfd_surv_status_item(MfdUi *ui, float x, float top, const char *label, const char *sys, int active,
                          int failed);

/* ---- Cursor (spec 01 section 11.1) ----------------------------------------------------------- */

/* Draws the yellow chevron cursor (FO rotated 90 deg) centred on (x, y). Called by mfd_ui_end. */
void mfd_draw_cursor(MfdDrawList *dl, float x, float y, int rotated);

#ifdef __cplusplus
}
#endif

#endif

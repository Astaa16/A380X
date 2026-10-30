/* SPDX-License-Identifier: GPL-3.0-or-later
 * Port of FBW ProcedureLinesGenerator.ts (see a380x_fws_lines.h). Line references below are to the pinned
 * fbw-a380x/src/systems/instruments/src/MsfsAvionicsCommon/EcamMessages/ProcedureLinesGenerator.ts unless
 * another file is named.
 */
#include "a380x_fws_lines.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define NBSP "\xc2\xa0"
#define LEVEL_MAP_SIZE 16
#define TEXT_BUFFER 256

/* ------------------------------------------------------------------------------------------------ */
/* Text helpers (JS string semantics on UTF-8, BMP only)                                             */
/* ------------------------------------------------------------------------------------------------ */
typedef struct {
    char *buf;
    size_t size, len;
} Text;

static void text_init(Text *t, char *buf, size_t size)
{
    t->buf = buf;
    t->size = size;
    t->len = 0;
    if (size) buf[0] = 0;
}

static size_t utf8_seq_len(unsigned char c)
{
    if (c < 0x80) return 1;
    if ((c & 0xe0) == 0xc0) return 2;
    if ((c & 0xf0) == 0xe0) return 3;
    return 4;
}

static void text_put(Text *t, const char *s)
{
    while (s && *s) {
        size_t n = utf8_seq_len((unsigned char)*s), i;
        if (t->len + n + 1 > t->size) break; /* never split a character */
        for (i = 0; i < n && s[i]; ++i) t->buf[t->len++] = s[i];
        s += i;
    }
    if (t->size) t->buf[t->len] = 0;
}

static void text_repeat(Text *t, const char *s, long count)
{
    for (; count > 0; --count) text_put(t, s);
}

size_t fws_js_length(const char *s)
{
    size_t n = 0;
    for (; s && *s; ++s)
        if (((unsigned char)*s & 0xc0) != 0x80) ++n;
    return n;
}

void fws_js_prefix(char *out, size_t size, const char *s, size_t max_chars)
{
    size_t chars = 0, len = 0;
    if (!size) return;
    while (s && s[len] && chars < max_chars) {
        size_t n = utf8_seq_len((unsigned char)s[len]);
        if (len + n + 1 > size) break;
        len += n;
        ++chars;
    }
    if (len) memcpy(out, s, len);
    out[len] = 0;
}

static int starts_with(const char *s, const char *prefix)
{
    return strncmp(s, prefix, strlen(prefix)) == 0;
}

/* ------------------------------------------------------------------------------------------------ */
/* index.ts item predicates                                                                          */
/* ------------------------------------------------------------------------------------------------ */
int fws_item_is_action(const FwsChecklistItem *item) { return item->label_not_completed != NULL; }
int fws_item_is_timed_action(const FwsChecklistItem *item) { return fws_item_is_action(item) && item->has_time; }
int fws_item_is_condition(const FwsChecklistItem *item) { return item->condition != -1; }
int fws_item_is_timed_condition(const FwsChecklistItem *item) { return fws_item_is_condition(item) && item->has_time; }
int fws_item_is_timed(const FwsChecklistItem *item)
{
    return fws_item_is_timed_action(item) || fws_item_is_timed_condition(item);
}
/* index.ts isChecklistHeadline: `[(ChecklistLineStyle.SubHeadline, ChecklistLineStyle.CenteredSubHeadline)]`
 * is a comma expression, so only CenteredSubHeadline counts. Kept as is. */
int fws_item_is_headline(const FwsChecklistItem *item) { return item->style == FWS_STYLE_CENTERED_SUB_HEADLINE; }

static const FwsChecklistItem *item_at(const FwsProcLines *g, int index)
{
    if (!g->procedure || index < 0 || index >= (int)g->procedure->num_items) return NULL;
    return &g->procedure->items[index];
}

static int num_items(const FwsProcLines *g)
{
    return g->procedure ? (int)g->procedure->num_items : 0;
}

/* ------------------------------------------------------------------------------------------------ */
/* Construction                                                                                      */
/* ------------------------------------------------------------------------------------------------ */
const FwsProcedureDef *fws_lines_find_procedure(FwsProcedureType type, unsigned id)
{
    switch (type) { /* lines 76-86 */
    case FWS_PROC_NORMAL: return fws_normal_procedure(id);
    case FWS_PROC_ABNORMAL:
    case FWS_PROC_FWS_FAILED_FALLBACK: return fws_abnormal_procedure(id);
    case FWS_PROC_DEFERRED: return fws_deferred_procedure(id);
    }
    return NULL;
}

void fws_lines_init(FwsProcLines *g, unsigned procedure_id, FwsProcedureType type, const FwsChecklistState *state,
                    int timestamps_defined, FwsRecommendation recommendation, int is_last_procedure)
{
    memset(g, 0, sizeof(*g));
    g->procedure_id = procedure_id;
    g->type = type;
    g->procedure = fws_lines_find_procedure(type, procedure_id);
    if (state) g->checklist_state = *state;
    g->timestamps_defined = (unsigned char)(timestamps_defined != 0);
    g->recommendation = (signed char)recommendation;
    g->is_last_procedure = (unsigned char)(is_last_procedure != 0);
    g->selected_item_index = 0; /* line 54 */
}

static void set_sii(FwsProcLines *g, int value)
{
    if (g->selected_item_index == value) return; /* Subject.set() only notifies on change */
    g->selected_item_index = value;
    if (g->selected_changed) g->selected_changed(g->ctx, g);
}

/* ------------------------------------------------------------------------------------------------ */
/* Timed items (lines 691-727)                                                                       */
/* ------------------------------------------------------------------------------------------------ */
int fws_lines_has_timed_item_elapsed(double timestamp, const FwsChecklistItem *item, double now)
{
    return (now - timestamp) > (double)item->time;
}

/* getTimedItemLineText: returns 0 for null. */
static int timed_item_line_text(const FwsChecklistItem *item, const FwsChecklistState *st, int index,
                                int timestamps_defined, double now, char *out, size_t size)
{
    double seconds;
    if (!timestamps_defined) return 0; /* console.warn(...) then null */
    if (index < 0 || index >= FWS_MAX_ITEMS || !st->timestamp_valid[index]) {
        seconds = item->time; /* undefined (warns) or null */
    } else {
        double diff = floor(now - st->items_timestamp[index]); /* Math.floor((Date.now() - ts) / 1000) */
        if (diff < (double)item->time) {
            seconds = (double)item->time - diff;
        } else if (fws_item_is_timed_condition(item) && item->append_time_if_elapsed == 1) {
            seconds = item->time;
        } else {
            return 0;
        }
    }
    if (seconds > 2000000000.0) seconds = 2000000000.0;
    snprintf(out, size, " AFTER %ld S :", (long)seconds);
    return 1;
}

/* ------------------------------------------------------------------------------------------------ */
/* Texts (lines 635-689)                                                                             */
/* ------------------------------------------------------------------------------------------------ */
void fws_lines_checklist_action_text(const FwsChecklistItem *item, int item_index, int item_complete,
                                     const FwsChecklistState *state, int timestamps_defined, double now,
                                     char *out, size_t size)
{
    Text t;
    const char *colon = item->colon_if_completed == 0 ? " " : " : ";
    text_init(&t, out, size);
    text_repeat(&t, NBSP, item->level);
    text_put(&t, "-");
    text_put(&t, item->name);
    if (item_complete && item->label_completed && *item->label_completed) {
        text_put(&t, colon);
        text_put(&t, item->label_completed);
    } else if (item_complete && item->label_not_completed && *item->label_not_completed) {
        text_put(&t, colon);
        text_put(&t, item->label_not_completed);
    } else if (!item_complete && item->label_not_completed && *item->label_not_completed) {
        long padding;
        if (fws_item_is_timed_action(item)) {
            char timed[48];
            if (timed_item_line_text(item, state, item_index, timestamps_defined, now, timed, sizeof(timed)))
                text_put(&t, timed);
        }
        /* Pad to 40 characters max: WD_LINE_CHARACTERS - 1 - (label + text + level + 2) */
        padding = (long)(FWS_WD_LINE_CHARACTERS - 1) -
                  (long)(fws_js_length(item->label_not_completed) + fws_js_length(t.buf) + item->level + 2);
        if (padding < 0) padding = 0;
        text_put(&t, " ");
        text_repeat(&t, ".", padding);
        text_put(&t, item->label_not_completed);
    }
}

void fws_lines_checklist_condition_text(const FwsChecklistItem *item, int item_index, int item_complete,
                                        const FwsChecklistState *state, int timestamps_defined, double now,
                                        char *out, size_t size)
{
    Text t;
    size_t name_len = strlen(item->name);
    int has_double_colon = name_len > 0 && item->name[name_len - 1] == ':';
    text_init(&t, out, size);
    text_repeat(&t, NBSP, item->level);
    if (starts_with(item->name, "WHEN")) {
        text_put(&t, ".");
        text_put(&t, item->name);
        text_put(&t, " ");
        text_put(&t, has_double_colon ? "" : ":");
    } else {
        char timed[48];
        int has_timed_text = 0;
        int is_timed = fws_item_is_timed_condition(item);
        const char *append = has_double_colon ? " " : " :";
        const char *name = starts_with(item->name, "IF") ? item->name + 2 : item->name;
        if (is_timed)
            has_timed_text = timed_item_line_text(item, state, item_index, timestamps_defined, now, timed,
                                                  sizeof(timed));
        text_put(&t, (item_complete || (is_timed && !has_timed_text)) ? ".AS " : ".IF ");
        text_put(&t, name);
        text_put(&t, " ");
        text_put(&t, has_timed_text ? timed : append);
    }
}

/* ------------------------------------------------------------------------------------------------ */
/* conditionalActiveItems (lines 99-154)                                                             */
/* ------------------------------------------------------------------------------------------------ */
void fws_lines_conditional_active_items(const FwsProcedureDef *proc, unsigned char *items_checked,
                                        unsigned char *items_active, const double *items_timestamp,
                                        const unsigned char *timestamp_valid, const unsigned char *items_to_show,
                                        double now)
{
    signed char level_condition[LEVEL_MAP_SIZE]; /* Map<number, boolean>: -1 = not set */
    int i, previous_level = -1;                  /* -1 = null */
    if (!proc || !proc->items) return;
    memset(level_condition, -1, sizeof(level_condition));
    for (i = 0; i < (int)proc->num_items && i < FWS_MAX_ITEMS; ++i) {
        const FwsChecklistItem *item = &proc->items[i];
        int level = item->level < LEVEL_MAP_SIZE - 1 ? item->level : LEVEL_MAP_SIZE - 2;
        int item_is_active = 1, is_parent_condition;
        /* Going up a level while the current level is active resets the deeper levels. */
        if (previous_level != -1 && previous_level > level && level_condition[level] != 0) {
            int l;
            for (l = previous_level; l > level; --l) level_condition[l] = 1;
        }
        if (level != 0) {
            int l;
            for (l = 1; l <= level; ++l) {
                if (level_condition[l] == 0) {
                    item_is_active = 0;
                    break;
                }
            }
        }
        /* isChecklistCondition(item) && itemsToShow !== undefined ? itemsToShow[i] ?? true : false */
        is_parent_condition = fws_item_is_condition(item) && items_to_show ? items_to_show[i] != 0 : 0;
        if (is_parent_condition) {
            int is_timed = fws_item_is_timed_condition(item);
            int has_timestamp = items_timestamp && timestamp_valid && timestamp_valid[i];
            int parent_is_active = items_checked[i] ||
                                   (is_timed && has_timestamp &&
                                    fws_lines_has_timed_item_elapsed(items_timestamp[i], item, now));
            level_condition[level + 1] = (signed char)(parent_is_active != 0);
            if (is_timed) items_checked[i] = (unsigned char)(parent_is_active != 0); /* enforce checked */
        }
        items_active[i] = (unsigned char)item_is_active;
        previous_level = level;
    }
}

/* ------------------------------------------------------------------------------------------------ */
/* toLineData (lines 404-633)                                                                        */
/* ------------------------------------------------------------------------------------------------ */
static FwsWdLine *push_line(FwsWdLine *out, int *n, int max)
{
    FwsWdLine *l;
    if (*n >= max) return NULL;
    l = &out[(*n)++];
    memset(l, 0, sizeof(*l));
    l->special = FWS_SPECIAL_NONE;
    l->item_index = 0;
    l->has_item_index = 0;
    return l;
}

static void line_text(FwsWdLine *l, const char *s)
{
    fws_js_prefix(l->text, sizeof(l->text), s, (size_t)-1);
}

static void line_index(FwsWdLine *l, int index)
{
    l->item_index = index;
    l->has_item_index = 1;
}

static int is_abnormal_not_sensed(const FwsProcLines *g)
{
    const FwsProcedureDef *abn = fws_abnormal_procedure(g->procedure_id);
    return g->type == FWS_PROC_ABNORMAL && abn && abn->sensed == 0;
}

int fws_lines_to_line_data(const FwsProcLines *g, FwsWdLine *out, int max)
{
    const FwsChecklistState *st = &g->checklist_state;
    int n = 0, i;
    int is_abnormal_or_deferred = g->type == FWS_PROC_ABNORMAL || g->type == FWS_PROC_DEFERRED ||
                                  g->type == FWS_PROC_FWS_FAILED_FALLBACK;
    int is_abnormal = g->type == FWS_PROC_ABNORMAL || g->type == FWS_PROC_FWS_FAILED_FALLBACK;
    int is_abnormal_ns = is_abnormal_not_sensed(g);
    int is_deferred = g->type == FWS_PROC_DEFERRED;
    int active = g->procedure_is_active != 0;
    const char *title = g->procedure && g->procedure->title ? g->procedure->title : "UNDEFINED";
    FwsWdLine *l;
    char buf[TEXT_BUFFER];

    if (is_deferred) {
        Text t;
        text_init(&t, buf, sizeof(buf));
        text_put(&t, st->procedure_completed ? "\x1b<7m> " : "\x1b<4m> ");
        text_put(&t, title);
        if ((l = push_line(out, &n, max))) {
            l->abnormal_procedure = 1;
            l->active_procedure = (unsigned char)active;
            l->sensed = 1;
            line_text(l, buf);
            l->style = FWS_STYLE_HEADLINE;
        }
    } else if ((l = push_line(out, &n, max))) {
        l->abnormal_procedure = (unsigned char)is_abnormal_or_deferred;
        l->active_procedure = (unsigned char)active;
        l->sensed = 1;
        line_text(l, title);
        l->style = FWS_STYLE_HEADLINE;
        l->first_line = 1;
        l->last_line = (unsigned char)!active;
    }

    if (!is_abnormal || active || g->type == FWS_PROC_FWS_FAILED_FALLBACK) {
        if (g->recommendation != FWS_RECOMMENDATION_NONE && (l = push_line(out, &n, max))) {
            l->abnormal_procedure = (unsigned char)is_abnormal_or_deferred;
            l->active_procedure = (unsigned char)active;
            l->sensed = 1;
            line_text(l, g->recommendation == FWS_RECOMMENDATION_LAND_ASAP ? "LAND ASAP" : "LAND ANSA");
            l->style = g->recommendation == FWS_RECOMMENDATION_LAND_ASAP ? FWS_STYLE_LAND_ASAP : FWS_STYLE_LAND_ANSA;
        }

        if (((is_deferred && !st->procedure_completed) || is_abnormal_ns) && (l = push_line(out, &n, max))) {
            Text t;
            text_init(&t, buf, sizeof(buf));
            text_repeat(&t, NBSP, 31);
            text_put(&t, "ACTIVATE");
            l->abnormal_procedure = (unsigned char)is_abnormal_or_deferred;
            l->active_procedure = (unsigned char)active;
            l->checked = st->procedure_activated;
            line_text(l, buf);
            l->style = FWS_STYLE_CHECKLIST_ITEM;
            line_index(l, FWS_SPECIAL_INDEX_ACTIVATE);
        }

        for (i = 0; i < num_items(g); ++i) {
            const FwsChecklistItem *item = &g->procedure->items[i];
            int cl_style, inactive = 0, is_condition = 0;
            int complete = i < FWS_MAX_ITEMS ? st->items_checked[i] : 0;
            Text t;
            if ((is_abnormal && !(i < FWS_MAX_ITEMS && st->items_to_show[i])) ||
                (is_deferred && st->procedure_completed))
                continue;
            cl_style = item->style >= 0 ? item->style : FWS_STYLE_CHECKLIST_ITEM;
            text_init(&t, buf, sizeof(buf));
            if (item->level && item->style != FWS_STYLE_CENTERED_SUB_HEADLINE) text_repeat(&t, NBSP, item->level);
            if (!(i < FWS_MAX_ITEMS && st->items_active[i]) || !st->procedure_activated) inactive = 1;

            if (fws_item_is_action(item)) {
                fws_lines_checklist_action_text(item, i, complete, st, g->timestamps_defined, g->now, buf,
                                                sizeof(buf));
                if (inactive && cl_style == FWS_STYLE_CHECKLIST_ITEM) cl_style = FWS_STYLE_CHECKLIST_ITEM_INACTIVE;
            } else if (fws_item_is_condition(item)) {
                is_condition = 1;
                cl_style = FWS_STYLE_CHECKLIST_CONDITION;
                fws_lines_checklist_condition_text(item, i, complete, st, g->timestamps_defined, g->now, buf,
                                                   sizeof(buf));
            } else if (fws_item_is_headline(item)) {
                if (cl_style == FWS_STYLE_CENTERED_SUB_HEADLINE) text_put(&t, ".");
                text_put(&t, item->name);
            } else {
                text_put(&t, NBSP);
                text_put(&t, item->name);
            }

            if ((l = push_line(out, &n, max))) {
                l->procedure_id = g->procedure_id;
                l->abnormal_procedure = (unsigned char)is_abnormal_or_deferred;
                l->active_procedure = (unsigned char)active;
                l->sensed = (unsigned char)(is_condition ? 1 : item->sensed);
                l->checked = (unsigned char)(complete != 0);
                fws_js_prefix(l->text, sizeof(l->text), buf, FWS_WD_LINE_CHARACTERS - 1);
                l->style = (unsigned char)cl_style;
                l->first_line = (unsigned char)((!active && is_abnormal) || g->type == FWS_PROC_FWS_FAILED_FALLBACK);
                l->last_line = l->first_line;
                /* FIXME (FBW) It should be possible to scroll to non sensed conditions */
                if (!is_condition || item->sensed) line_index(l, i);
                l->inactive = (unsigned char)inactive;
                l->special = cl_style == FWS_STYLE_EMPTY ? FWS_SPECIAL_EMPTY : FWS_SPECIAL_NONE;
            }

            if (is_condition && !item->sensed && (l = push_line(out, &n, max))) {
                /* CONFIRM <condition>: trailing colon and leading IF / WHEN removed */
                char name[TEXT_BUFFER];
                const char *shown = name;
                size_t len = strlen(item->name);
                snprintf(name, sizeof(name), "%s", item->name);
                if (len > 0 && len < sizeof(name) && name[len - 1] == ':') name[len - 1] = 0;
                if (starts_with(name, "IF")) shown = name + 2;
                else if (starts_with(name, "WHEN")) shown = name + 4;
                text_init(&t, buf, sizeof(buf));
                text_repeat(&t, NBSP, item->level);
                text_put(&t, "CONFIRM ");
                text_put(&t, shown);
                l->abnormal_procedure = (unsigned char)is_abnormal_or_deferred;
                l->active_procedure = (unsigned char)active;
                l->sensed = item->sensed;
                l->checked = (unsigned char)(complete != 0);
                line_text(l, buf);
                l->style = (unsigned char)cl_style;
                l->first_line = (unsigned char)(!active && is_abnormal);
                l->last_line = l->first_line;
                line_index(l, i);
                l->inactive = (unsigned char)inactive;
            }
        }

        if (is_abnormal && g->type != FWS_PROC_FWS_FAILED_FALLBACK) {
            if ((l = push_line(out, &n, max))) {
                Text t;
                text_init(&t, buf, sizeof(buf));
                text_repeat(&t, NBSP, 34);
                text_put(&t, "CLEAR");
                l->abnormal_procedure = (unsigned char)is_abnormal_or_deferred;
                l->active_procedure = (unsigned char)active;
                line_text(l, buf);
                l->style = FWS_STYLE_CHECKLIST_ITEM;
                l->last_line = g->is_last_procedure;
                line_index(l, FWS_SPECIAL_INDEX_CLEAR);
            }
        } else if (g->type == FWS_PROC_NORMAL) {
            if ((l = push_line(out, &n, max))) {
                Text t;
                text_init(&t, buf, sizeof(buf));
                text_repeat(&t, NBSP, 27);
                text_put(&t, "C/L COMPLETE");
                l->active_procedure = (unsigned char)active;
                l->checked = st->procedure_completed;
                line_text(l, buf);
                l->style = FWS_STYLE_CHECKLIST_ITEM;
                line_index(l, FWS_SPECIAL_INDEX_NORMAL_CL_COMPLETE);
            }
            if ((l = push_line(out, &n, max))) {
                Text t;
                text_init(&t, buf, sizeof(buf));
                text_repeat(&t, NBSP, 34);
                text_put(&t, "RESET");
                l->active_procedure = (unsigned char)active;
                line_text(l, buf);
                l->style = FWS_STYLE_CHECKLIST_ITEM;
                l->last_line = 1;
                line_index(l, FWS_SPECIAL_INDEX_NORMAL_RESET);
            }
        } else if (is_deferred && (l = push_line(out, &n, max))) {
            Text t;
            text_init(&t, buf, sizeof(buf));
            if (st->procedure_completed) {
                text_repeat(&t, NBSP, 19);
                text_put(&t, "DEFERRED PROC RECALL");
                line_index(l, FWS_SPECIAL_INDEX_DEFERRED_PROC_RECALL);
            } else {
                text_repeat(&t, NBSP, 17);
                text_put(&t, "DEFERRED PROC COMPLETE");
                l->checked = st->procedure_completed;
                line_index(l, FWS_SPECIAL_INDEX_DEFERRED_PROC_COMPLETE);
            }
            l->active_procedure = (unsigned char)active;
            line_text(l, buf);
            l->style = FWS_STYLE_CHECKLIST_ITEM;
            l->inactive = (unsigned char)!st->procedure_activated;
        }
    } else if (num_items(g) > 0 && (l = push_line(out, &n, max))) {
        /* Only three dots for following procedures */
        l->abnormal_procedure = (unsigned char)is_abnormal_or_deferred;
        l->sensed = 1;
        line_text(l, "...");
        l->style = FWS_STYLE_OMISSION_DOTS;
        l->first_line = 1;
        l->last_line = 1;
    }

    /* Empty line after procedure */
    if ((l = push_line(out, &n, max))) {
        l->abnormal_procedure = (unsigned char)is_abnormal_or_deferred;
        l->active_procedure = (unsigned char)(active || is_deferred);
        l->sensed = 1;
        l->style = FWS_STYLE_CHECKLIST_ITEM;
        l->first_line = (unsigned char)!is_deferred;
        l->last_line = (unsigned char)!is_deferred;
    }
    return n;
}

/* getActualShownItems(): originalItemIndex of every line, has[] = 0 for NaN. */
static int actual_shown_items(const FwsProcLines *g, int *index, unsigned char *has)
{
    FwsWdLine lines[FWS_LINES_MAX]; /* on the stack: generators may be nested through the selection hook */
    int n = fws_lines_to_line_data(g, lines, FWS_LINES_MAX), i;
    for (i = 0; i < n; ++i) {
        index[i] = lines[i].item_index;
        has[i] = lines[i].has_item_index;
    }
    return n;
}

int fws_lines_num_lines_until_selected(const FwsProcLines *g)
{
    int index[FWS_LINES_MAX], n, i;
    unsigned char has[FWS_LINES_MAX];
    n = actual_shown_items(g, index, has);
    for (i = 0; i < n; ++i)
        if (has[i] && index[i] == g->selected_item_index) return i;
    return -1;
}

int fws_lines_first_line_is_selected(const FwsProcLines *g)
{
    if (g->type == FWS_PROC_DEFERRED || g->type == FWS_PROC_ABNORMAL) {
        if (g->checklist_state.procedure_activated) {
            /* returns getActualShownItems()[0]: the headline's index is undefined -> NaN -> falsy */
            int index[FWS_LINES_MAX], n;
            unsigned char has[FWS_LINES_MAX];
            n = actual_shown_items(g, index, has);
            return n > 0 && has[0] && index[0] != 0;
        }
        return g->selected_item_index == FWS_SPECIAL_INDEX_ACTIVATE;
    }
    return 0;
}

int fws_lines_last_line_is_selected(const FwsProcLines *g)
{
    int sii = g->selected_item_index;
    if (g->type == FWS_PROC_DEFERRED) {
        return g->checklist_state.procedure_activated
                   ? sii == FWS_SPECIAL_INDEX_DEFERRED_PROC_COMPLETE || sii == FWS_SPECIAL_INDEX_DEFERRED_PROC_RECALL
                   : sii == FWS_SPECIAL_INDEX_ACTIVATE;
    }
    if (g->type == FWS_PROC_ABNORMAL) {
        return g->checklist_state.procedure_activated ? sii == FWS_SPECIAL_INDEX_CLEAR
                                                      : sii == FWS_SPECIAL_INDEX_ACTIVATE;
    }
    return 0;
}

/* ------------------------------------------------------------------------------------------------ */
/* Navigation (lines 201-402)                                                                        */
/* ------------------------------------------------------------------------------------------------ */
static int item_is_selectable(const FwsProcLines *g, int i, int skip_completed_sensed)
{
    const FwsChecklistItem *item = item_at(g, i);
    const FwsChecklistState *st = &g->checklist_state;
    if (!item || i >= FWS_MAX_ITEMS) return 0;
    if (!st->items_active[i] || !st->items_to_show[i]) return 0;
    switch (item->style) { /* nonSelectableItemStyles */
    case FWS_STYLE_HEADLINE:
    case FWS_STYLE_OMISSION_DOTS:
    case FWS_STYLE_SEPARATION_LINE:
    case FWS_STYLE_SUB_HEADLINE:
    case FWS_STYLE_CENTERED_SUB_HEADLINE:
    case FWS_STYLE_RED: return 0;
    default: break;
    }
    return !item->sensed || !skip_completed_sensed || !st->items_checked[i];
}

static int selectable_items(const FwsProcLines *g, int skip_completed_sensed, int *out)
{
    int i, n = 0;
    for (i = 0; i < num_items(g); ++i)
        if (item_is_selectable(g, i, skip_completed_sensed)) out[n++] = i;
    return n;
}

void fws_lines_move_up(FwsProcLines *g)
{
    int selectable[FWS_MAX_ITEMS], n, sii;
    if ((g->type == FWS_PROC_DEFERRED || g->type == FWS_PROC_ABNORMAL) && !g->checklist_state.procedure_activated) {
        set_sii(g, FWS_SPECIAL_INDEX_ACTIVATE);
        return;
    }
    n = selectable_items(g, 1, selectable);
    if (n == 0) {
        fws_lines_select_first(g);
        return;
    }
    sii = g->selected_item_index;
    if (sii == FWS_SPECIAL_INDEX_NORMAL_RESET) {
        set_sii(g, FWS_SPECIAL_INDEX_NORMAL_CL_COMPLETE);
    } else if (sii == FWS_SPECIAL_INDEX_DEFERRED_PROC_COMPLETE || sii == FWS_SPECIAL_INDEX_NORMAL_CL_COMPLETE ||
               sii == FWS_SPECIAL_INDEX_CLEAR) {
        set_sii(g, selectable[n - 1]);
    } else if (sii >= 0) {
        if (sii == selectable[0] && g->type == FWS_PROC_ABNORMAL && is_abnormal_not_sensed(g)) {
            set_sii(g, FWS_SPECIAL_INDEX_ACTIVATE);
        } else {
            int i, previous = -1;
            for (i = n - 1; i >= 0; --i) {
                if (selectable[i] < sii) {
                    previous = selectable[i];
                    break;
                }
            }
            if (previous >= 0) set_sii(g, previous);
        }
    }
}

void fws_lines_move_down(FwsProcLines *g, int skip_completed_sensed)
{
    int selectable[FWS_MAX_ITEMS], n, sii = g->selected_item_index;
    if ((g->type == FWS_PROC_DEFERRED || g->type == FWS_PROC_ABNORMAL) && !g->checklist_state.procedure_activated) {
        set_sii(g, FWS_SPECIAL_INDEX_ACTIVATE);
        return;
    }
    n = selectable_items(g, skip_completed_sensed, selectable);
    if (n == 0 || sii >= selectable[n - 1]) {
        /* Last element before first special line (CLEAR etc.) */
        if (g->type == FWS_PROC_NORMAL) {
            set_sii(g, FWS_SPECIAL_INDEX_NORMAL_CL_COMPLETE);
        } else if (g->type == FWS_PROC_ABNORMAL) {
            set_sii(g, FWS_SPECIAL_INDEX_CLEAR);
        } else if (g->type == FWS_PROC_DEFERRED) {
            set_sii(g, g->checklist_state.procedure_completed ? FWS_SPECIAL_INDEX_DEFERRED_PROC_RECALL
                                                              : FWS_SPECIAL_INDEX_DEFERRED_PROC_COMPLETE);
        }
    } else if (sii == FWS_SPECIAL_INDEX_NORMAL_CL_COMPLETE) {
        set_sii(g, FWS_SPECIAL_INDEX_NORMAL_RESET);
    } else if (sii == FWS_SPECIAL_INDEX_ACTIVATE) {
        set_sii(g, selectable[0]);
    } else if (sii > FWS_HIGHEST_SPECIAL_INDEX) {
        int i, next = selectable[n - 1];
        for (i = 0; i < n; ++i) {
            if (selectable[i] > sii) {
                next = selectable[i];
                break;
            }
        }
        set_sii(g, next < selectable[n - 1] ? next : selectable[n - 1]);
    }
}

unsigned fws_lines_check_selected(FwsProcLines *g)
{
    FwsChecklistState cl = g->checklist_state; /* clState: copies of all arrays */
    int index[FWS_LINES_MAX], n, i, last_item_index = 0, has_last = 0, sii = g->selected_item_index;
    unsigned char has[FWS_LINES_MAX];
    unsigned events = 0;
    const FwsChecklistItem *selected = item_at(g, sii);

    n = actual_shown_items(g, index, has);
    for (i = 0; i < n; ++i) {
        if (has[i] && index[i] > FWS_HIGHEST_SPECIAL_INDEX) {
            last_item_index = index[i];
            has_last = 1;
        }
    }

    if (sii >= 0 && has_last && sii <= last_item_index && !(selected && selected->sensed)) {
        cl.items_checked[sii] = (unsigned char)!cl.items_checked[sii];
        events |= FWS_LINES_EVENT_ITEM_CHECKED;
        if (g->item_checked_callback) g->item_checked_callback(g->ctx, g, &cl);
        if (cl.items_checked[sii]) {
            if (selected && fws_item_is_condition(selected) && selected->condition == 1) {
                /* Force 'active' status update */
                fws_lines_conditional_active_items(g->procedure, cl.items_checked, cl.items_active,
                                                   g->timestamps_defined ? cl.items_timestamp : NULL,
                                                   g->timestamps_defined ? cl.timestamp_valid : NULL,
                                                   cl.items_to_show, g->now);
            }
            if (g->item_checked_adopts_state) g->checklist_state = cl;
            fws_lines_move_down(g, 1);
        }
    } else if (sii == FWS_SPECIAL_INDEX_CLEAR) {
        events |= FWS_LINES_EVENT_CLEARED_OR_RESET;
        if (g->procedure_cleared_or_reset_callback) g->procedure_cleared_or_reset_callback(g->ctx, g, &cl);
    } else if (sii == FWS_SPECIAL_INDEX_NORMAL_CL_COMPLETE) {
        cl.procedure_completed = 1;
        for (i = 0; i < num_items(g) && i < FWS_MAX_ITEMS; ++i)
            if (!g->procedure->items[i].sensed) cl.items_checked[i] = 1;
        events |= FWS_LINES_EVENT_COMPLETED;
        if (g->procedure_completed_callback) g->procedure_completed_callback(g->ctx, g, &cl);
    } else if (sii == FWS_SPECIAL_INDEX_NORMAL_RESET) {
        cl.procedure_completed = 0;
        for (i = 0; i < num_items(g) && i < FWS_MAX_ITEMS; ++i)
            if (!g->procedure->items[i].sensed) cl.items_checked[i] = 0;
        events |= FWS_LINES_EVENT_CLEARED_OR_RESET;
        if (g->procedure_cleared_or_reset_callback) g->procedure_cleared_or_reset_callback(g->ctx, g, &cl);
    } else if (sii == FWS_SPECIAL_INDEX_ACTIVATE) {
        cl.procedure_activated = (unsigned char)!cl.procedure_activated;
        events |= FWS_LINES_EVENT_CLEARED_OR_RESET;
        if (g->procedure_cleared_or_reset_callback) g->procedure_cleared_or_reset_callback(g->ctx, g, &cl);
    } else if (sii == FWS_SPECIAL_INDEX_DEFERRED_PROC_COMPLETE) {
        cl.procedure_completed = 1;
        set_sii(g, FWS_SPECIAL_INDEX_DEFERRED_PROC_RECALL);
        events |= FWS_LINES_EVENT_COMPLETED;
        if (g->procedure_completed_callback) g->procedure_completed_callback(g->ctx, g, &cl);
    } else if (sii == FWS_SPECIAL_INDEX_DEFERRED_PROC_RECALL) {
        cl.procedure_completed = 0;
        set_sii(g, FWS_SPECIAL_INDEX_DEFERRED_PROC_COMPLETE);
        events |= FWS_LINES_EVENT_COMPLETED;
        if (g->procedure_completed_callback) g->procedure_completed_callback(g->ctx, g, &cl);
    }
    g->checklist_state = cl;

    if (g->selected_item_index == FWS_SPECIAL_INDEX_NORMAL_RESET ||
        g->selected_item_index == FWS_SPECIAL_INDEX_ACTIVATE)
        fws_lines_select_first(g);
    return events;
}

void fws_lines_select_first(FwsProcLines *g)
{
    int selectable[FWS_MAX_ITEMS], n;
    if ((g->type == FWS_PROC_DEFERRED || g->type == FWS_PROC_ABNORMAL) && !g->checklist_state.procedure_activated) {
        set_sii(g, FWS_SPECIAL_INDEX_ACTIVATE);
        return;
    }
    if (g->type == FWS_PROC_DEFERRED && g->checklist_state.procedure_completed) {
        set_sii(g, FWS_SPECIAL_INDEX_DEFERRED_PROC_RECALL);
        return;
    }
    if (g->type == FWS_PROC_NORMAL && g->checklist_state.procedure_completed) {
        set_sii(g, FWS_SPECIAL_INDEX_NORMAL_RESET);
        return;
    }
    n = selectable_items(g, 1, selectable);
    /* itemsChecked.length is the procedure's item count in this port */
    set_sii(g, n > 0 ? selectable[0] - 1 : num_items(g) - 1);
    fws_lines_move_down(g, 1);
}

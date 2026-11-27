/* Weststar SDL Browser — the Nuklear backend: SDL input in, chrome pixels out.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "nkui.h"

#include "icons.h"

#include <math.h>
#include <string.h>

#define NS_NK_WIDTH_CACHE_MAX 16384

typedef struct {
    NsNk    *nk;
    double   size;
    gboolean bold;
} FontSpec;

struct NsNk {
    struct nk_context    ctx;
    struct nk_user_font  fonts[NS_NK_FONT_COUNT];
    FontSpec             specs[NS_NK_FONT_COUNT];
    GHashTable          *widths;      /* "size|bold|text" -> width */

    NsUiLayer            layer;
    double               scale;
    void                *last_cmds;   /* the previous frame's command buffer */
    gsize                last_len;
    SDL_Rect             last_box;    /* what the previous frame drew, pixels */
    gboolean             have_last_box;
};

/* ---- fonts ------------------------------------------------------------- */

static float
font_width(nk_handle handle, float height, const char *text, int len)
{
    (void)height;
    const FontSpec *spec = handle.ptr;
    NsNk *nk = spec->nk;
    if (!text || len <= 0)
        return 0.0f;
    char *key = g_strdup_printf("%.2f|%d|%.*s", spec->size, spec->bold ? 1 : 0,
                                len, text);
    gpointer hit = nk ? g_hash_table_lookup(nk->widths, key) : NULL;
    if (hit) {
        g_free(key);
        return *(float *)hit;
    }
    float w = (float)ns_ui_text_width(text, len, spec->size, spec->bold);
    if (nk) {
        if (g_hash_table_size(nk->widths) > NS_NK_WIDTH_CACHE_MAX)
            g_hash_table_remove_all(nk->widths);
        float *v = g_new(float, 1);
        *v = w;
        g_hash_table_insert(nk->widths, key, v);
    } else {
        g_free(key);
    }
    return w;
}

const struct nk_user_font *
ns_nk_font(NsNk *nk, NsNkFontRole role)
{
    return &nk->fonts[role];
}

static void
init_font(NsNk *nk, NsNkFontRole role, double size, gboolean bold)
{
    nk->specs[role].nk = nk;
    nk->specs[role].size = size;
    nk->specs[role].bold = bold;
    nk->fonts[role].userdata = nk_handle_ptr(&nk->specs[role]);
    nk->fonts[role].height = (float)size;
    nk->fonts[role].width = font_width;
}

/* ---- clipboard --------------------------------------------------------- */

static void
clip_copy(nk_handle user, const char *text, int len)
{
    (void)user;
    char *s = g_strndup(text, (gsize)len);
    SDL_SetClipboardText(s);
    g_free(s);
}

static void
clip_paste(nk_handle user, struct nk_text_edit *edit)
{
    (void)user;
    char *text = SDL_GetClipboardText();
    if (text && *text) {
        /* A single-line field: line breaks become spaces. */
        for (char *p = text; *p; p++)
            if (*p == '\n' || *p == '\r' || *p == '\t')
                *p = ' ';
        nk_textedit_paste(edit, text, (int)strlen(text));
    }
    SDL_free(text);
}

/* ---- lifetime and style ------------------------------------------------ */

NsNk *
ns_nk_new(void)
{
    NsNk *nk = g_new0(NsNk, 1);
    nk->widths = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    init_font(nk, NS_NK_FONT_NORMAL, 13.0, FALSE);
    init_font(nk, NS_NK_FONT_SMALL, 12.0, FALSE);
    init_font(nk, NS_NK_FONT_BOLD, 13.0, TRUE);
    nk_init_default(&nk->ctx, &nk->fonts[NS_NK_FONT_NORMAL]);
    nk->ctx.clip.copy = clip_copy;
    nk->ctx.clip.paste = clip_paste;
    nk->ctx.clip.userdata = nk_handle_ptr(nk);
    ns_nk_apply_theme(nk, ns_ui_theme());
    return nk;
}

void
ns_nk_free(NsNk *nk)
{
    if (!nk)
        return;
    nk_free(&nk->ctx);
    g_hash_table_destroy(nk->widths);
    ns_ui_layer_clear(&nk->layer);
    g_free(nk->last_cmds);
    g_free(nk);
}

struct nk_context *
ns_nk_context(NsNk *nk)
{
    return &nk->ctx;
}

static struct nk_color
to_nk(NsColor c)
{
    return nk_rgba((int)lround(c.r * 255), (int)lround(c.g * 255),
                   (int)lround(c.b * 255), (int)lround(c.a * 255));
}

void
ns_nk_apply_theme(NsNk *nk, const NsUiTheme *t)
{
    struct nk_color table[NK_COLOR_COUNT];
    struct nk_color text = to_nk(t->text), bar = to_nk(t->toolbar);
    struct nk_color field = to_nk(t->field), border = to_nk(t->border);
    struct nk_color hover = to_nk(t->hover), pressed = to_nk(t->pressed);
    struct nk_color sel = to_nk(t->selection), popup = to_nk(t->popup);
    struct nk_color dim = to_nk(t->text_dim);
    table[NK_COLOR_TEXT] = text;
    table[NK_COLOR_WINDOW] = bar;
    table[NK_COLOR_HEADER] = bar;
    table[NK_COLOR_BORDER] = border;
    table[NK_COLOR_BUTTON] = bar;
    table[NK_COLOR_BUTTON_HOVER] = hover;
    table[NK_COLOR_BUTTON_ACTIVE] = pressed;
    table[NK_COLOR_TOGGLE] = field;
    table[NK_COLOR_TOGGLE_HOVER] = hover;
    table[NK_COLOR_TOGGLE_CURSOR] = to_nk(t->focus);
    table[NK_COLOR_SELECT] = popup;
    table[NK_COLOR_SELECT_ACTIVE] = sel;
    table[NK_COLOR_SLIDER] = field;
    table[NK_COLOR_SLIDER_CURSOR] = dim;
    table[NK_COLOR_SLIDER_CURSOR_HOVER] = text;
    table[NK_COLOR_SLIDER_CURSOR_ACTIVE] = text;
    table[NK_COLOR_PROPERTY] = field;
    table[NK_COLOR_EDIT] = field;
    table[NK_COLOR_EDIT_CURSOR] = text;
    table[NK_COLOR_COMBO] = field;
    table[NK_COLOR_CHART] = field;
    table[NK_COLOR_CHART_COLOR] = dim;
    table[NK_COLOR_CHART_COLOR_HIGHLIGHT] = text;
    table[NK_COLOR_SCROLLBAR] = bar;
    table[NK_COLOR_SCROLLBAR_CURSOR] = border;
    table[NK_COLOR_SCROLLBAR_CURSOR_HOVER] = dim;
    table[NK_COLOR_SCROLLBAR_CURSOR_ACTIVE] = dim;
    table[NK_COLOR_TAB_HEADER] = bar;
    table[NK_COLOR_KNOB] = field;
    table[NK_COLOR_KNOB_CURSOR] = dim;
    table[NK_COLOR_KNOB_CURSOR_HOVER] = text;
    table[NK_COLOR_KNOB_CURSOR_ACTIVE] = text;
    nk_style_from_table(&nk->ctx, table);

    struct nk_style *s = &nk->ctx.style;
    s->window.padding = nk_vec2(6, 6);
    s->window.spacing = nk_vec2(4, 4);
    s->window.border = 1;
    s->window.rounding = 0;
    s->window.popup_border = 1;
    s->window.menu_border = 1;
    s->window.contextual_border = 1;
    s->window.popup_border_color = border;
    s->window.border_color = border;
    s->window.popup_padding = nk_vec2(4, 4);
    s->window.menu_padding = nk_vec2(4, 4);
    s->window.contextual_padding = nk_vec2(4, 4);
    /* Nuklear takes the rounding off the content box too: 3 + 0 + 0 leaves
     * a 30 x 28 toolbar button a 24 x 22 icon. */
    s->button.rounding = 3;
    s->button.border = 0;
    s->button.padding = nk_vec2(0, 0);
    s->button.image_padding = nk_vec2(0, 0);
    /* Flat buttons: only hover and press are drawn, so a disabled button's
     * darkened background stays invisible too. */
    s->button.normal = nk_style_item_hide();
    s->button.text_active = text;
    s->button.text_hover = text;
    s->button.text_normal = text;
    s->edit.rounding = 6;
    s->edit.border = 1;
    s->edit.border_color = to_nk(t->field_border);
    s->edit.padding = nk_vec2(8, 4);
    s->edit.cursor_size = 1;
    s->edit.selected_normal = sel;
    s->edit.selected_hover = sel;
    s->edit.selected_text_normal = text;
    s->edit.selected_text_hover = text;
    s->edit.text_normal = text;
    s->edit.text_hover = text;
    s->edit.text_active = text;
    s->edit.normal = nk_style_item_color(field);
    s->edit.hover = nk_style_item_color(field);
    s->edit.active = nk_style_item_color(field);
    s->edit.cursor_normal = text;
    s->edit.cursor_hover = text;
    s->contextual_button.rounding = 3;
    s->contextual_button.padding = nk_vec2(8, 3);
    s->menu_button.rounding = 3;
    s->selectable.rounding = 3;
    s->selectable.padding = nk_vec2(8, 3);
    s->selectable.hover = nk_style_item_color(hover);
    s->selectable.text_hover = text;
    s->text.color = text;
}

struct nk_image
ns_nk_icon(const char *name)
{
    struct nk_image img = nk_image_ptr((void *)name);
    img.w = img.h = 24;
    img.region[2] = img.region[3] = 24;
    return img;
}

/* ---- input ------------------------------------------------------------- */

void
ns_nk_input_begin(NsNk *nk)
{
    nk_input_begin(&nk->ctx);
}

void
ns_nk_input_end(NsNk *nk)
{
    nk_input_end(&nk->ctx);
}

static void
input_key(struct nk_context *ctx, const SDL_KeyboardEvent *k)
{
    nk_bool down = k->down;
    gboolean ctrl = (k->mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) != 0;
    gboolean shift = (k->mod & SDL_KMOD_SHIFT) != 0;
    switch (k->key) {
    case SDLK_LSHIFT:
    case SDLK_RSHIFT:
        nk_input_key(ctx, NK_KEY_SHIFT, down);
        break;
    case SDLK_LCTRL:
    case SDLK_RCTRL:
    case SDLK_LGUI:
    case SDLK_RGUI:
        nk_input_key(ctx, NK_KEY_CTRL, down);
        break;
    case SDLK_DELETE:
        nk_input_key(ctx, NK_KEY_DEL, down);
        break;
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
        nk_input_key(ctx, NK_KEY_ENTER, down);
        break;
    case SDLK_TAB:
        nk_input_key(ctx, NK_KEY_TAB, down);
        break;
    case SDLK_BACKSPACE:
        nk_input_key(ctx, NK_KEY_BACKSPACE, down);
        break;
    case SDLK_HOME:
        nk_input_key(ctx, NK_KEY_TEXT_START, down);
        nk_input_key(ctx, NK_KEY_SCROLL_START, down);
        break;
    case SDLK_END:
        nk_input_key(ctx, NK_KEY_TEXT_END, down);
        nk_input_key(ctx, NK_KEY_SCROLL_END, down);
        break;
    case SDLK_PAGEDOWN:
        nk_input_key(ctx, NK_KEY_SCROLL_DOWN, down);
        break;
    case SDLK_PAGEUP:
        nk_input_key(ctx, NK_KEY_SCROLL_UP, down);
        break;
    case SDLK_UP:
        nk_input_key(ctx, NK_KEY_UP, down);
        break;
    case SDLK_DOWN:
        nk_input_key(ctx, NK_KEY_DOWN, down);
        break;
    case SDLK_LEFT:
        nk_input_key(ctx, ctrl ? NK_KEY_TEXT_WORD_LEFT : NK_KEY_LEFT, down);
        break;
    case SDLK_RIGHT:
        nk_input_key(ctx, ctrl ? NK_KEY_TEXT_WORD_RIGHT : NK_KEY_RIGHT, down);
        break;
    case SDLK_A:
        if (ctrl) nk_input_key(ctx, NK_KEY_TEXT_SELECT_ALL, down);
        break;
    case SDLK_C:
        if (ctrl) nk_input_key(ctx, NK_KEY_COPY, down);
        break;
    case SDLK_X:
        if (ctrl) nk_input_key(ctx, NK_KEY_CUT, down);
        break;
    case SDLK_V:
        if (ctrl) nk_input_key(ctx, NK_KEY_PASTE, down);
        break;
    case SDLK_Z:
        if (ctrl) nk_input_key(ctx, shift ? NK_KEY_TEXT_REDO : NK_KEY_TEXT_UNDO, down);
        break;
    case SDLK_Y:
        if (ctrl) nk_input_key(ctx, NK_KEY_TEXT_REDO, down);
        break;
    default:
        break;
    }
}

static int
button_of(Uint8 sdl)
{
    switch (sdl) {
    case SDL_BUTTON_LEFT:   return NK_BUTTON_LEFT;
    case SDL_BUTTON_MIDDLE: return NK_BUTTON_MIDDLE;
    case SDL_BUTTON_RIGHT:  return NK_BUTTON_RIGHT;
    default:                return -1;
    }
}

void
ns_nk_input_event(NsNk *nk, const SDL_Event *ev, float to_ui)
{
    struct nk_context *ctx = &nk->ctx;
    switch (ev->type) {
    case SDL_EVENT_MOUSE_MOTION:
        nk_input_motion(ctx, (int)(ev->motion.x * to_ui),
                        (int)(ev->motion.y * to_ui));
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP: {
        int b = button_of(ev->button.button);
        int x = (int)(ev->button.x * to_ui), y = (int)(ev->button.y * to_ui);
        if (b < 0)
            break;
        if (b == NK_BUTTON_LEFT && ev->button.down && ev->button.clicks == 2)
            nk_input_button(ctx, NK_BUTTON_DOUBLE, x, y, nk_true);
        else if (b == NK_BUTTON_LEFT && !ev->button.down)
            nk_input_button(ctx, NK_BUTTON_DOUBLE, x, y, nk_false);
        nk_input_button(ctx, (enum nk_buttons)b, x, y, ev->button.down);
        break;
    }
    case SDL_EVENT_MOUSE_WHEEL:
        nk_input_scroll(ctx, nk_vec2(ev->wheel.x, ev->wheel.y));
        break;
    case SDL_EVENT_TEXT_INPUT: {
        const char *p = ev->text.text;
        while (p && *p) {
            gunichar c = g_utf8_get_char_validated(p, -1);
            if (c == (gunichar)-1 || c == (gunichar)-2)
                break;
            nk_input_unicode(ctx, (nk_rune)c);
            p = g_utf8_next_char(p);
        }
        break;
    }
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
        input_key(ctx, &ev->key);
        break;
    default:
        break;
    }
}

/* ---- rendering --------------------------------------------------------- */

static void
set_color(gfx_t *g, struct nk_color c)
{
    gfx_set_source_rgba(g, c.r / 255.0, c.g / 255.0, c.b / 255.0,
                          c.a / 255.0);
}

static void
rounded(gfx_t *g, double x, double y, double w, double h, double r)
{
    if (r <= 0) {
        gfx_rectangle(g, x, y, w, h);
        return;
    }
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    gfx_new_sub_path(g);
    gfx_arc(g, x + w - r, y + r, r, -G_PI / 2, 0);
    gfx_arc(g, x + w - r, y + h - r, r, 0, G_PI / 2);
    gfx_arc(g, x + r, y + h - r, r, G_PI / 2, G_PI);
    gfx_arc(g, x + r, y + r, r, G_PI, 3 * G_PI / 2);
    gfx_close_path(g);
}

/* The command's bounds in UI units; FALSE for commands that draw nothing. */
static gboolean
cmd_bounds(const struct nk_command *cmd, double *x0, double *y0, double *x1,
           double *y1)
{
#define BOX(px, py, pw, ph) do { *x0 = (px); *y0 = (py); \
        *x1 = (px) + (pw); *y1 = (py) + (ph); } while (0)
    switch (cmd->type) {
    case NK_COMMAND_LINE: {
        const struct nk_command_line *c = (const void *)cmd;
        double t = c->line_thickness;
        *x0 = MIN(c->begin.x, c->end.x) - t;
        *y0 = MIN(c->begin.y, c->end.y) - t;
        *x1 = MAX(c->begin.x, c->end.x) + t;
        *y1 = MAX(c->begin.y, c->end.y) + t;
        return TRUE;
    }
    case NK_COMMAND_RECT: {
        const struct nk_command_rect *c = (const void *)cmd;
        BOX(c->x, c->y, c->w, c->h);
        return TRUE;
    }
    case NK_COMMAND_RECT_FILLED: {
        const struct nk_command_rect_filled *c = (const void *)cmd;
        BOX(c->x, c->y, c->w, c->h);
        return TRUE;
    }
    case NK_COMMAND_RECT_MULTI_COLOR: {
        const struct nk_command_rect_multi_color *c = (const void *)cmd;
        BOX(c->x, c->y, c->w, c->h);
        return TRUE;
    }
    case NK_COMMAND_CIRCLE: {
        const struct nk_command_circle *c = (const void *)cmd;
        BOX(c->x - 1, c->y - 1, c->w + 2, c->h + 2);
        return TRUE;
    }
    case NK_COMMAND_CIRCLE_FILLED: {
        const struct nk_command_circle_filled *c = (const void *)cmd;
        BOX(c->x, c->y, c->w, c->h);
        return TRUE;
    }
    case NK_COMMAND_ARC: {
        const struct nk_command_arc *c = (const void *)cmd;
        BOX(c->cx - c->r - 1, c->cy - c->r - 1, 2 * c->r + 2, 2 * c->r + 2);
        return TRUE;
    }
    case NK_COMMAND_ARC_FILLED: {
        const struct nk_command_arc_filled *c = (const void *)cmd;
        BOX(c->cx - c->r, c->cy - c->r, 2 * c->r, 2 * c->r);
        return TRUE;
    }
    case NK_COMMAND_TRIANGLE: {
        const struct nk_command_triangle *c = (const void *)cmd;
        *x0 = MIN(c->a.x, MIN(c->b.x, c->c.x)) - 1;
        *y0 = MIN(c->a.y, MIN(c->b.y, c->c.y)) - 1;
        *x1 = MAX(c->a.x, MAX(c->b.x, c->c.x)) + 1;
        *y1 = MAX(c->a.y, MAX(c->b.y, c->c.y)) + 1;
        return TRUE;
    }
    case NK_COMMAND_TRIANGLE_FILLED: {
        const struct nk_command_triangle_filled *c = (const void *)cmd;
        *x0 = MIN(c->a.x, MIN(c->b.x, c->c.x));
        *y0 = MIN(c->a.y, MIN(c->b.y, c->c.y));
        *x1 = MAX(c->a.x, MAX(c->b.x, c->c.x));
        *y1 = MAX(c->a.y, MAX(c->b.y, c->c.y));
        return TRUE;
    }
    case NK_COMMAND_POLYGON:
    case NK_COMMAND_POLYGON_FILLED:
    case NK_COMMAND_POLYLINE: {
        const struct nk_vec2i *pts;
        unsigned short n;
        if (cmd->type == NK_COMMAND_POLYGON) {
            const struct nk_command_polygon *c = (const void *)cmd;
            pts = c->points;
            n = c->point_count;
        } else if (cmd->type == NK_COMMAND_POLYGON_FILLED) {
            const struct nk_command_polygon_filled *c = (const void *)cmd;
            pts = c->points;
            n = c->point_count;
        } else {
            const struct nk_command_polyline *c = (const void *)cmd;
            pts = c->points;
            n = c->point_count;
        }
        if (n == 0)
            return FALSE;
        *x0 = *x1 = pts[0].x;
        *y0 = *y1 = pts[0].y;
        for (unsigned short i = 1; i < n; i++) {
            *x0 = MIN(*x0, pts[i].x);
            *y0 = MIN(*y0, pts[i].y);
            *x1 = MAX(*x1, pts[i].x);
            *y1 = MAX(*y1, pts[i].y);
        }
        *x0 -= 1; *y0 -= 1; *x1 += 1; *y1 += 1;
        return TRUE;
    }
    case NK_COMMAND_TEXT: {
        const struct nk_command_text *c = (const void *)cmd;
        BOX(c->x, c->y, c->w + 2, c->h);
        return TRUE;
    }
    case NK_COMMAND_IMAGE: {
        const struct nk_command_image *c = (const void *)cmd;
        BOX(c->x, c->y, c->w, c->h);
        return TRUE;
    }
    default:
        return FALSE;
    }
#undef BOX
}

static void
path_points(gfx_t *g, const struct nk_vec2i *pts, unsigned short n)
{
    for (unsigned short i = 0; i < n; i++) {
        if (i == 0)
            gfx_move_to(g, pts[i].x, pts[i].y);
        else
            gfx_line_to(g, pts[i].x, pts[i].y);
    }
}

static void
draw_cmd(gfx_t *g, const struct nk_command *cmd, const SDL_Rect *dirty_ui)
{
    switch (cmd->type) {
    case NK_COMMAND_SCISSOR: {
        const struct nk_command_scissor *c = (const void *)cmd;
        gfx_reset_clip(g);
        gfx_rectangle(g, dirty_ui->x, dirty_ui->y, dirty_ui->w, dirty_ui->h);
        gfx_clip(g);
        gfx_rectangle(g, c->x, c->y, c->w, c->h);
        gfx_clip(g);
        break;
    }
    case NK_COMMAND_LINE: {
        const struct nk_command_line *c = (const void *)cmd;
        set_color(g, c->color);
        gfx_set_line_width(g, c->line_thickness);
        gfx_move_to(g, c->begin.x + 0.5, c->begin.y + 0.5);
        gfx_line_to(g, c->end.x + 0.5, c->end.y + 0.5);
        gfx_stroke(g);
        break;
    }
    case NK_COMMAND_CURVE: {
        const struct nk_command_curve *c = (const void *)cmd;
        set_color(g, c->color);
        gfx_set_line_width(g, c->line_thickness);
        gfx_move_to(g, c->begin.x, c->begin.y);
        gfx_curve_to(g, c->ctrl[0].x, c->ctrl[0].y, c->ctrl[1].x,
                       c->ctrl[1].y, c->end.x, c->end.y);
        gfx_stroke(g);
        break;
    }
    case NK_COMMAND_RECT: {
        const struct nk_command_rect *c = (const void *)cmd;
        double t = c->line_thickness;
        set_color(g, c->color);
        gfx_set_line_width(g, t);
        rounded(g, c->x + t / 2, c->y + t / 2, c->w - t, c->h - t, c->rounding);
        gfx_stroke(g);
        break;
    }
    case NK_COMMAND_RECT_FILLED: {
        const struct nk_command_rect_filled *c = (const void *)cmd;
        set_color(g, c->color);
        rounded(g, c->x, c->y, c->w, c->h, c->rounding);
        gfx_fill(g);
        break;
    }
    case NK_COMMAND_RECT_MULTI_COLOR: {
        const struct nk_command_rect_multi_color *c = (const void *)cmd;
        gfx_pattern_t *p = gfx_pattern_create_linear(c->x, c->y, c->x,
                                                         c->y + c->h);
        gfx_pattern_add_color_stop_rgba(p, 0, c->top.r / 255.0,
                                          c->top.g / 255.0, c->top.b / 255.0,
                                          c->top.a / 255.0);
        gfx_pattern_add_color_stop_rgba(p, 1, c->bottom.r / 255.0,
                                          c->bottom.g / 255.0,
                                          c->bottom.b / 255.0,
                                          c->bottom.a / 255.0);
        gfx_set_source(g, p);
        gfx_rectangle(g, c->x, c->y, c->w, c->h);
        gfx_fill(g);
        gfx_pattern_destroy(p);
        break;
    }
    case NK_COMMAND_CIRCLE:
    case NK_COMMAND_CIRCLE_FILLED: {
        gboolean fill = cmd->type == NK_COMMAND_CIRCLE_FILLED;
        const struct nk_command_circle *c = (const void *)cmd;
        const struct nk_command_circle_filled *f = (const void *)cmd;
        short x = fill ? f->x : c->x, y = fill ? f->y : c->y;
        unsigned short w = fill ? f->w : c->w, h = fill ? f->h : c->h;
        if (w == 0 || h == 0)
            break;
        set_color(g, fill ? f->color : c->color);
        gfx_save(g);
        gfx_translate(g, x + w / 2.0, y + h / 2.0);
        gfx_scale(g, w / 2.0, h / 2.0);
        gfx_arc(g, 0, 0, 1, 0, 2 * G_PI);
        gfx_restore(g);
        if (fill) {
            gfx_fill(g);
        } else {
            gfx_set_line_width(g, c->line_thickness);
            gfx_stroke(g);
        }
        break;
    }
    case NK_COMMAND_ARC: {
        const struct nk_command_arc *c = (const void *)cmd;
        set_color(g, c->color);
        gfx_set_line_width(g, c->line_thickness);
        gfx_arc(g, c->cx, c->cy, c->r, c->a[0], c->a[1]);
        gfx_stroke(g);
        break;
    }
    case NK_COMMAND_ARC_FILLED: {
        const struct nk_command_arc_filled *c = (const void *)cmd;
        set_color(g, c->color);
        gfx_move_to(g, c->cx, c->cy);
        gfx_arc(g, c->cx, c->cy, c->r, c->a[0], c->a[1]);
        gfx_close_path(g);
        gfx_fill(g);
        break;
    }
    case NK_COMMAND_TRIANGLE: {
        const struct nk_command_triangle *c = (const void *)cmd;
        set_color(g, c->color);
        gfx_set_line_width(g, c->line_thickness);
        gfx_move_to(g, c->a.x, c->a.y);
        gfx_line_to(g, c->b.x, c->b.y);
        gfx_line_to(g, c->c.x, c->c.y);
        gfx_close_path(g);
        gfx_stroke(g);
        break;
    }
    case NK_COMMAND_TRIANGLE_FILLED: {
        const struct nk_command_triangle_filled *c = (const void *)cmd;
        set_color(g, c->color);
        gfx_move_to(g, c->a.x, c->a.y);
        gfx_line_to(g, c->b.x, c->b.y);
        gfx_line_to(g, c->c.x, c->c.y);
        gfx_close_path(g);
        gfx_fill(g);
        break;
    }
    case NK_COMMAND_POLYGON: {
        const struct nk_command_polygon *c = (const void *)cmd;
        set_color(g, c->color);
        gfx_set_line_width(g, c->line_thickness);
        path_points(g, c->points, c->point_count);
        gfx_close_path(g);
        gfx_stroke(g);
        break;
    }
    case NK_COMMAND_POLYGON_FILLED: {
        const struct nk_command_polygon_filled *c = (const void *)cmd;
        set_color(g, c->color);
        path_points(g, c->points, c->point_count);
        gfx_close_path(g);
        gfx_fill(g);
        break;
    }
    case NK_COMMAND_POLYLINE: {
        const struct nk_command_polyline *c = (const void *)cmd;
        set_color(g, c->color);
        gfx_set_line_width(g, c->line_thickness);
        path_points(g, c->points, c->point_count);
        gfx_stroke(g);
        break;
    }
    case NK_COMMAND_TEXT: {
        const struct nk_command_text *c = (const void *)cmd;
        const FontSpec *spec = c->font ? c->font->userdata.ptr : NULL;
        if (!spec)
            break;
        ns_ui_text_draw(g, c->x, c->y, 0, c->h, c->string, c->length,
                        spec->size, spec->bold, c->foreground.r / 255.0,
                        c->foreground.g / 255.0, c->foreground.b / 255.0,
                        c->foreground.a / 255.0);
        break;
    }
    case NK_COMMAND_IMAGE: {
        const struct nk_command_image *c = (const void *)cmd;
        const char *name = c->img.handle.ptr;
        double size = MIN(c->w, c->h);
        if (!name)
            break;
        /* A disabled widget's image comes tinted grey: draw it faded. */
        double alpha = (c->col.r / 255.0) * (c->col.a / 255.0);
        if (alpha < 0.99)
            gfx_push_group(g);
        ns_sdl_icon_draw(g, name, c->x + (c->w - size) / 2,
                         c->y + (c->h - size) / 2, size);
        if (alpha < 0.99) {
            gfx_pop_group_to_source(g);
            gfx_paint_with_alpha(g, MAX(alpha, 0.3));
        }
        break;
    }
    default:
        break;
    }
}

static void
rect_union(SDL_Rect *a, const SDL_Rect *b)
{
    SDL_GetRectUnion(a, b, a);
}

gboolean
ns_nk_render(NsNk *nk, SDL_Renderer *renderer, int px_w, int px_h,
             double ui_scale)
{
    struct nk_context *ctx = &nk->ctx;
    gboolean resized = ns_ui_layer_size(&nk->layer, px_w, px_h) ||
                       ui_scale != nk->scale;
    nk->scale = ui_scale;
    const void *cmds = nk_buffer_memory_const(&ctx->memory);
    gsize len = ctx->memory.allocated;
    gboolean same = !resized && nk->last_cmds && len == nk->last_len &&
                    memcmp(cmds, nk->last_cmds, len) == 0;
    if (same) {
        nk_clear(ctx);
        return FALSE;
    }
    g_free(nk->last_cmds);
    nk->last_cmds = g_memdup2(cmds, len);
    nk->last_len = len;

    /* What this frame draws, in pixels. */
    SDL_Rect box = { 0, 0, 0, 0 };
    gboolean have_box = FALSE;
    const struct nk_command *cmd;
    nk_foreach(cmd, ctx) {
        double x0, y0, x1, y1;
        if (!cmd_bounds(cmd, &x0, &y0, &x1, &y1))
            continue;
        SDL_Rect r = { (int)floor(x0 * ui_scale) - 1, (int)floor(y0 * ui_scale) - 1,
                       (int)ceil((x1 - x0) * ui_scale) + 3,
                       (int)ceil((y1 - y0) * ui_scale) + 3 };
        if (!have_box) {
            box = r;
            have_box = TRUE;
        } else {
            rect_union(&box, &r);
        }
    }
    SDL_Rect dirty = box;
    if (resized) {
        dirty = (SDL_Rect){ 0, 0, px_w, px_h };
    } else if (nk->have_last_box) {
        if (have_box)
            rect_union(&dirty, &nk->last_box);
        else
            dirty = nk->last_box;
    }
    nk->last_box = box;
    nk->have_last_box = have_box;
    SDL_Rect all = { 0, 0, px_w, px_h };
    if (!SDL_GetRectIntersection(&dirty, &all, &dirty)) {
        nk_clear(ctx);
        return FALSE;
    }

    gfx_t *g = gfx_create(nk->layer.surface);
    gfx_rectangle(g, dirty.x, dirty.y, dirty.w, dirty.h);
    gfx_clip(g);
    gfx_set_operator(g, GFX_OPERATOR_CLEAR);
    gfx_paint(g);
    gfx_set_operator(g, GFX_OPERATOR_OVER);
    gfx_scale(g, ui_scale, ui_scale);
    SDL_Rect dirty_ui = { (int)floor(dirty.x / ui_scale), (int)floor(dirty.y / ui_scale),
                          (int)ceil(dirty.w / ui_scale) + 2,
                          (int)ceil(dirty.h / ui_scale) + 2 };
    nk_foreach(cmd, ctx)
        draw_cmd(g, cmd, &dirty_ui);
    gfx_destroy(g);
    nk_clear(ctx);
    ns_ui_layer_upload(&nk->layer, renderer, &dirty);
    return TRUE;
}

void
ns_nk_present(NsNk *nk, SDL_Renderer *renderer)
{
    ns_ui_layer_present(&nk->layer, renderer, 0, 0);
}

void
ns_nk_device_reset(NsNk *nk)
{
    ns_ui_layer_lose_texture(&nk->layer);
    /* Redraw everything next frame. */
    g_free(nk->last_cmds);
    nk->last_cmds = NULL;
    nk->last_len = 0;
    nk->have_last_box = FALSE;
    nk->scale = 0;
}

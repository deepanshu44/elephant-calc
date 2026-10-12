#define _POSIX_C_SOURCE 200809L
#include "elephant.h"
#include "undo.h"
#include "timezone.h"
#include "autocomplete.h"
#include "currency.h"

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/keysym.h>
#include <cairo/cairo.h>
#include <cairo/cairo-xlib.h>
#include <cairo/cairo-ft.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <unistd.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdbool.h>
#include <stdint.h>
#include <math.h>
#include <ctype.h>
#include <time.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ==========================================================================
 * Direct FreeType Font Management (Zero-delay Fontconfig bypass)
 * ========================================================================== */

static cairo_font_face_t *g_font_regular = NULL;
static cairo_font_face_t *g_font_bold = NULL;
static FT_Library g_ft_lib = NULL;
static FT_Face g_ft_face_regular = NULL;
static FT_Face g_ft_face_bold = NULL;

static void init_fonts(void) {
    if (FT_Init_FreeType(&g_ft_lib) != 0) return;

    const char *reg_paths[] = {
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
        "/usr/share/fonts/truetype/ubuntu/Ubuntu-R.ttf"
    };
    const char *bold_paths[] = {
        "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
        "/usr/share/fonts/dejavu/DejaVuSans-Bold.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf",
        "/usr/share/fonts/truetype/ubuntu/Ubuntu-B.ttf"
    };

    for (size_t i = 0; i < sizeof(reg_paths)/sizeof(reg_paths[0]); i++) {
        if (access(reg_paths[i], R_OK) == 0) {
            if (FT_New_Face(g_ft_lib, reg_paths[i], 0, &g_ft_face_regular) == 0) {
                g_font_regular = cairo_ft_font_face_create_for_ft_face(g_ft_face_regular, 0);
                break;
            }
        }
    }

    for (size_t i = 0; i < sizeof(bold_paths)/sizeof(bold_paths[0]); i++) {
        if (access(bold_paths[i], R_OK) == 0) {
            if (FT_New_Face(g_ft_lib, bold_paths[i], 0, &g_ft_face_bold) == 0) {
                g_font_bold = cairo_ft_font_face_create_for_ft_face(g_ft_face_bold, 0);
                break;
            }
        }
    }
}

static void set_font(cairo_t *cr, bool bold) {
    cairo_font_face_t *face = bold ? g_font_bold : g_font_regular;
    if (face) {
        cairo_set_font_face(cr, face);
    } else {
        cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, bold ? CAIRO_FONT_WEIGHT_BOLD : CAIRO_FONT_WEIGHT_NORMAL);
    }
}

static void cleanup_fonts(void) {
    if (g_font_regular) { cairo_font_face_destroy(g_font_regular); g_font_regular = NULL; }
    if (g_font_bold) { cairo_font_face_destroy(g_font_bold); g_font_bold = NULL; }
    if (g_ft_face_regular) { FT_Done_Face(g_ft_face_regular); g_ft_face_regular = NULL; }
    if (g_ft_face_bold) { FT_Done_Face(g_ft_face_bold); g_ft_face_bold = NULL; }
    if (g_ft_lib) { FT_Done_FreeType(g_ft_lib); g_ft_lib = NULL; }
}

static double get_time_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1000000.0;
}

static char active_clipboard_buf[512] = "";

static void copy_to_clipboard(Display *dpy, Window win, const char *text) {
    if (text) {
        strncpy(active_clipboard_buf, text, sizeof(active_clipboard_buf) - 1);
        active_clipboard_buf[sizeof(active_clipboard_buf) - 1] = '\0';
    }
    Atom clipboard = XInternAtom(dpy, "CLIPBOARD", False);
    XSetSelectionOwner(dpy, clipboard, win, CurrentTime);
    XSetSelectionOwner(dpy, XA_PRIMARY, win, CurrentTime);
}

static void utf8_truncate_char(char *s) {
    size_t len = strlen(s);
    if (len == 0) return;
    len--;
    while (len > 0 && ((unsigned char)s[len] & 0xC0) == 0x80) {
        len--;
    }
    s[len] = '\0';
}

typedef struct {
    double x, y, w, h;
    double del_x, del_w;
    char name[32];
    bool is_func;
} DrawerChipHit;

static DrawerChipHit g_drawer_chips[MAX_SYMBOLS];
static int g_drawer_chip_count = 0;
static double g_drawer_clear_all_x = 0, g_drawer_clear_all_y = 0;
static double g_drawer_clear_all_w = 0, g_drawer_clear_all_h = 0;
static double g_drawer_scroll_y = 0.0;
static double g_drawer_max_scroll = 0.0;
static int g_hover_type = 0; /* 0: none, 1: fmt, 2: dec, 3: mode, 4: scale, 5: rates, 6: clear_all, 7: chip, 8: gear */
static int g_hover_idx = -1;
static bool g_hover_del = false;

static void draw_rounded_rect(cairo_t *cr, double x, double y, double w, double h, double r) {
    if (r <= 0.0) {
        cairo_rectangle(cr, x, y, w, h);
        return;
    }
    if (r > w / 2.0) r = w / 2.0;
    if (r > h / 2.0) r = h / 2.0;
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + w - r, y + r, r, -M_PI / 2.0, 0.0);
    cairo_arc(cr, x + w - r, y + h - r, r, 0.0, M_PI / 2.0);
    cairo_arc(cr, x + r, y + h - r, r, M_PI / 2.0, M_PI);
    cairo_arc(cr, x + r, y + r, r, M_PI, 3.0 * M_PI / 2.0);
    cairo_close_path(cr);
}

static int find_words_char_index(cairo_t *cr, const char *text, double rel_x, double font_scale) {
    if (!text || !*text || rel_x <= 0.0) return 0;
    size_t len = strlen(text);
    set_font(cr, true);
    cairo_set_font_size(cr, 13.5 * font_scale);
    for (size_t i = 1; i <= len; i++) {
        if (i < len && ((unsigned char)text[i] & 0xC0) == 0x80) continue;
        char sub[256];
        memcpy(sub, text, i);
        sub[i] = '\0';
        cairo_text_extents_t ext;
        cairo_text_extents(cr, sub, &ext);
        if (ext.x_advance >= rel_x) {
            return (int)i;
        }
    }
    return (int)len;
}

static size_t jump_word_backward(const char *buf, size_t cursor) {
    if (cursor == 0) return 0;
    size_t i = cursor;
    while (i > 0 && isspace((unsigned char)buf[i - 1])) i--;
    if (i > 0) {
        if (isalnum((unsigned char)buf[i - 1]) || buf[i - 1] == '_') {
            while (i > 0 && (isalnum((unsigned char)buf[i - 1]) || buf[i - 1] == '_')) i--;
        } else {
            while (i > 0 && !isalnum((unsigned char)buf[i - 1]) && !isspace((unsigned char)buf[i - 1]) && buf[i - 1] != '_') i--;
        }
    }
    return i;
}

static size_t jump_word_forward(const char *buf, size_t len, size_t cursor) {
    if (cursor >= len) return len;
    size_t i = cursor;
    while (i < len && isspace((unsigned char)buf[i])) i++;
    if (i < len) {
        if (isalnum((unsigned char)buf[i]) || buf[i] == '_') {
            while (i < len && (isalnum((unsigned char)buf[i]) || buf[i] == '_')) i++;
        } else {
            while (i < len && !isalnum((unsigned char)buf[i]) && !isspace((unsigned char)buf[i]) && buf[i] != '_') i++;
        }
    }
    return i;
}

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static cairo_surface_t *render_icon(int sz) {
    cairo_surface_t *surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, sz, sz);
    cairo_t *cr = cairo_create(surf);
    double s = (double)sz;

    /* Background: Rounded rectangle / squircle */
    double r = s * 0.22;
    cairo_new_sub_path(cr);
    cairo_arc(cr, s - r, r, r, -M_PI/2, 0);
    cairo_arc(cr, s - r, s - r, r, 0, M_PI/2);
    cairo_arc(cr, r, s - r, r, M_PI/2, M_PI);
    cairo_arc(cr, r, r, r, M_PI, 3*M_PI/2);
    cairo_close_path(cr);

    cairo_set_source_rgb(cr, 0.118, 0.118, 0.141); /* Dark slate #1e1e24 */
    cairo_fill_preserve(cr);

    cairo_set_source_rgb(cr, 0.294, 0.588, 0.980); /* Accent blue border */
    cairo_set_line_width(cr, s * 0.04);
    cairo_stroke(cr);

    /* Left ear */
    cairo_set_source_rgb(cr, 0.294, 0.588, 0.980); /* Bright accent blue */
    cairo_arc(cr, s * 0.32, s * 0.44, s * 0.18, 0, 2 * M_PI);
    cairo_fill(cr);

    /* Right ear */
    cairo_arc(cr, s * 0.68, s * 0.44, s * 0.18, 0, 2 * M_PI);
    cairo_fill(cr);

    /* Head / forehead */
    cairo_set_source_rgb(cr, 0.92, 0.94, 0.98); /* Crisp off-white */
    cairo_arc(cr, s * 0.50, s * 0.44, s * 0.24, 0, 2 * M_PI);
    cairo_fill(cr);

    /* Eyes */
    cairo_set_source_rgb(cr, 0.118, 0.118, 0.141);
    cairo_arc(cr, s * 0.42, s * 0.40, s * 0.035, 0, 2 * M_PI);
    cairo_arc(cr, s * 0.58, s * 0.40, s * 0.035, 0, 2 * M_PI);
    cairo_fill(cr);

    /* Trunk curving upwards */
    cairo_set_source_rgb(cr, 0.294, 0.588, 0.980);
    cairo_set_line_width(cr, s * 0.08);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    cairo_move_to(cr, s * 0.50, s * 0.50);
    cairo_curve_to(cr, s * 0.50, s * 0.70, s * 0.66, s * 0.76, s * 0.66, s * 0.64);
    cairo_stroke(cr);

    /* Mint green math plus emblem on forehead */
    cairo_set_source_rgb(cr, 0.20, 0.82, 0.48);
    cairo_set_line_width(cr, s * 0.04);
    cairo_move_to(cr, s * 0.50, s * 0.26);
    cairo_line_to(cr, s * 0.50, s * 0.36);
    cairo_move_to(cr, s * 0.45, s * 0.31);
    cairo_line_to(cr, s * 0.55, s * 0.31);
    cairo_stroke(cr);

    cairo_destroy(cr);
    return surf;
}

static void set_window_icon(Display *dpy, Window win) {
    int sizes[] = { 64, 48, 32, 16 };
    int num_sizes = 4;

    size_t total_elements = 0;
    for (int i = 0; i < num_sizes; i++) {
        total_elements += 2 + (size_t)sizes[i] * (size_t)sizes[i];
    }

    unsigned long *icon_data = malloc(total_elements * sizeof(unsigned long));
    if (!icon_data) return;

    size_t offset = 0;
    for (int i = 0; i < num_sizes; i++) {
        int sz = sizes[i];
        cairo_surface_t *surf = render_icon(sz);
        cairo_surface_flush(surf);
        uint32_t *pixels = (uint32_t *)cairo_image_surface_get_data(surf);

        icon_data[offset++] = (unsigned long)sz;
        icon_data[offset++] = (unsigned long)sz;

        for (int p = 0; p < sz * sz; p++) {
            icon_data[offset++] = (unsigned long)pixels[p];
        }

        cairo_surface_destroy(surf);
    }

    Atom net_wm_icon = XInternAtom(dpy, "_NET_WM_ICON", False);
    XChangeProperty(
        dpy, win, net_wm_icon, XA_CARDINAL, 32,
        PropModeReplace, (unsigned char *)icon_data, (int)total_elements
    );

    free(icon_data);
}

int main(int argc, char *argv[]) {
    /* Check for --config flag first */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--config") == 0 && i + 1 < argc) {
            strncpy(custom_config_path, argv[i + 1], sizeof(custom_config_path) - 1);
            custom_config_path[sizeof(custom_config_path) - 1] = '\0';
            break;
        }
    }
    double font_scale = 1.0;
    comma_fmt = load_settings(&decimal_places, &font_scale, &output_mode);
    init_units();
    load_saved_symbols();

    /* Parse CLI flags and headless eval mode */
    const char *eval_expr = NULL;
    bool benchmark_mode = false;
    bool words_mode = false;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--config") == 0 && i + 1 < argc) {
            i++;
        }
        else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0 ||
                 strcmp(argv[i], "--units") == 0) {
            print_units_help();
            return 0;
        }
        else if (strcmp(argv[i], "--list-saved") == 0 || strcmp(argv[i], "--saved") == 0 || strcmp(argv[i], "--vars") == 0) {
            char list_buf[2048] = "";
            int cnt = format_saved_symbols_list(list_buf, sizeof(list_buf));
            if (cnt == 0) {
                printf("No saved variables or functions.\n");
            } else {
                printf("%s", list_buf);
            }
            return 0;
        }
        else if (strcmp(argv[i], "--delete-saved") == 0 && i + 1 < argc) {
            const char *del_name = argv[++i];
            if (delete_symbol(del_name)) {
                save_saved_symbols();
                printf("Deleted saved symbol: %s\n", del_name);
                return 0;
            } else {
                fprintf(stderr, "Error: symbol '%s' not found.\n", del_name);
                return 1;
            }
        }
        else if (strcmp(argv[i], "--indian") == 0) comma_fmt = FMT_INDIAN;
        else if (strcmp(argv[i], "--intl") == 0) comma_fmt = FMT_INTL;
        else if (strcmp(argv[i], "--plain") == 0 || strcmp(argv[i], "--no-commas") == 0) comma_fmt = FMT_PLAIN;
        else if (strcmp(argv[i], "--comp-only") == 0 || strcmp(argv[i], "--computing") == 0) output_mode = OUTPUT_COMP_ONLY;
        else if (strcmp(argv[i], "--all") == 0) output_mode = OUTPUT_ALL;
        else if (strcmp(argv[i], "--simple") == 0) output_mode = OUTPUT_SIMPLE;
        else if ((strcmp(argv[i], "--scale") == 0 || strcmp(argv[i], "--font-scale") == 0) && i + 1 < argc) {
            i++;
            font_scale = atof(argv[i]);
            if (font_scale < 0.8) font_scale = 0.8;
            if (font_scale > 1.8) font_scale = 1.8;
        }
        else if ((strcmp(argv[i], "--decimals") == 0 || strcmp(argv[i], "-d") == 0) && i + 1 < argc) {
            i++;
            if (strcasecmp(argv[i], "auto") == 0) decimal_places = -1;
            else decimal_places = atoi(argv[i]);
        }
        else if (strcmp(argv[i], "--benchmark") == 0) benchmark_mode = true;
        else if (strcmp(argv[i], "--words") == 0) words_mode = true;
        else if (strcmp(argv[i], "--eval") == 0 && i + 1 < argc) {
            eval_expr = argv[++i];
        }
    }

    if (eval_expr) {
        double ans_val = load_ans();
        double c_val = 0.0;
        char c_res[64];
        char c_unit[32] = "";
        char c_words_sfx[64];
        const char *c_err = NULL;
        if (try_convert_expression(eval_expr, ans_val, &c_val, c_res, sizeof(c_res), c_unit, sizeof(c_unit), c_words_sfx, sizeof(c_words_sfx), &c_err)) {
            if (c_err) {
                fprintf(stderr, "Error: %s\n", c_err);
                return 1;
            }
            if (words_mode) {
                char comp_out[512] = "";
                char w_out[512] = "";
                format_subtitle_preview(c_val, c_unit, c_words_sfx, false, false, w_out, sizeof(w_out));
                format_computing_row(c_val, output_mode, c_unit, c_words_sfx, false, false, comp_out, sizeof(comp_out));
                if (w_out[0]) printf("%s\n", w_out);
                if (comp_out[0]) printf("%s\n", comp_out);
            } else {
                printf("%s\n", c_res);
            }
            save_ans(c_val);
            save_history_entry(eval_expr);
            return 0;
        }

        double res = 0.0;
        const char *err = NULL;
        bool is_date = false;
        bool is_time = false;
        char expr_unit[32] = "";
        if (evaluate_equation_ex(eval_expr, ans_val, &res, expr_unit, sizeof(expr_unit), &is_date, &is_time, &err)) {
            char out[160];
            if (is_time) {
                format_time_result(res, false, out, sizeof(out));
            } else if (is_date) {
                if (expr_unit[0] && is_valid_timezone_query(expr_unit)) {
                    TimezoneResult tz_res;
                    if (convert_to_timezone((time_t)round(res), expr_unit, &tz_res)) {
                        snprintf(out, sizeof(out), "%.128s %.16s", tz_res.time_str, tz_res.abbrev);
                    } else {
                        format_date_result(res, out, sizeof(out));
                    }
                } else {
                    format_date_result(res, out, sizeof(out));
                }
            } else {
                char comp_unit_buf[128];
                if (expr_unit[0] && format_compound_unit(res, expr_unit, comp_unit_buf, sizeof(comp_unit_buf))) {
                    snprintf(out, sizeof(out), "%s", comp_unit_buf);
                } else {
                    format_result(res, out, sizeof(out));
                    if (expr_unit[0]) {
                        strncat(out, " ", sizeof(out) - strlen(out) - 1);
                        strncat(out, expr_unit, sizeof(out) - strlen(out) - 1);
                    }
                }
            }
            if (words_mode) {
                char comp_out[512] = "";
                char w_out[512] = "";
                format_subtitle_preview(res, expr_unit[0] ? expr_unit : NULL, NULL, is_date, is_time, w_out, sizeof(w_out));
                format_computing_row(res, output_mode, expr_unit[0] ? expr_unit : NULL, NULL, is_date, is_time, comp_out, sizeof(comp_out));
                if (w_out[0]) printf("%s\n", w_out);
                if (comp_out[0]) printf("%s\n", comp_out);
            } else {
                printf("%s\n", out);
            }
            save_ans(res);
            save_history_entry(eval_expr);
            return 0;
        } else {
            fprintf(stderr, "Error: %s\n", err ? err : "syntax error");
            return 1;
        }
    }

    init_fonts();

    double t_start = get_time_ms();

    /* Load persistent answer and history */
    double t_after_load = get_time_ms();
    double ans_val = load_ans();

    char history[MAX_HISTORY][256];
    int hist_count = load_history(history);
    int hist_idx = hist_count;
    char saved_draft[256] = "";

    /* 1. Connect to X server */
    double t_before_xopen = get_time_ms();
    Display *dpy = XOpenDisplay(NULL);
    if (!dpy) {
        fprintf(stderr, "Error: cannot open DISPLAY\n");
        return 1;
    }
    double t_after_xopen = get_time_ms();

    int screen = DefaultScreen(dpy);
    Window root = RootWindow(dpy, screen);
    int screen_w = DisplayWidth(dpy, screen);
    int screen_h = DisplayHeight(dpy, screen);

    /* 2. Window Geometry: Floating Spotlight Bar with Words Row & Drawer */
    int win_w = 680;
    int win_h_collapsed = 204;
    int win_h_expanded = 504;
    int cur_h = win_h_collapsed;
    bool drawer_open = false;
    int win_x = (screen_w - win_w) / 2;
    int win_y = (screen_h - win_h_collapsed) / 3;

    XSetWindowAttributes swa;
    swa.background_pixmap = None;
    swa.event_mask = ExposureMask | KeyPressMask | ButtonPressMask | ButtonReleaseMask | PointerMotionMask | FocusChangeMask | StructureNotifyMask | LeaveWindowMask;
    swa.override_redirect = False;

    Window win = XCreateWindow(
        dpy, root,
        win_x, win_y, win_w, cur_h, 0,
        DefaultDepth(dpy, screen), InputOutput,
        DefaultVisual(dpy, screen),
        CWBackPixmap | CWEventMask | CWOverrideRedirect, &swa
    );
    XSetWindowBackgroundPixmap(dpy, win, None);

    XStoreName(dpy, win, "Elephant Calc");

    /* Set Application Class & Icon for Dock / Taskbar */
    XClassHint class_hint = {
        .res_name = "elephant-calc",
        .res_class = "Elephant-calc"
    };
    XSetClassHint(dpy, win, &class_hint);
    double t_before_icon = get_time_ms();
    set_window_icon(dpy, win);
    double t_after_icon = get_time_ms();

    Atom wm_delete = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(dpy, win, &wm_delete, 1);

    Atom type_atom = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE", False);
    Atom dialog_atom = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_DIALOG", False);
    XChangeProperty(dpy, win, type_atom, XA_ATOM, 32, PropModeReplace, (unsigned char *)&dialog_atom, 1);

    Atom state_atom = XInternAtom(dpy, "_NET_WM_STATE", False);
    Atom above_atom = XInternAtom(dpy, "_NET_WM_STATE_ABOVE", False);
    XChangeProperty(dpy, win, state_atom, XA_ATOM, 32, PropModeReplace, (unsigned char *)&above_atom, 1);

    /* 3. Map Window immediately */
    double t_before_map = get_time_ms();
    XMapRaised(dpy, win);
    XFlush(dpy);
    double t_after_map = get_time_ms();

    /* 4. Prepare Cairo Offscreen Double Buffer */
    double t_before_cairo = get_time_ms();
    cairo_surface_t *xlib_surf = cairo_xlib_surface_create(dpy, win, DefaultVisual(dpy, screen), win_w, cur_h);
    cairo_surface_t *buf_surf = cairo_image_surface_create(CAIRO_FORMAT_RGB24, win_w, cur_h);
    cairo_t *cr = cairo_create(buf_surf);
    double t_after_cairo = get_time_ms();

    char input_buf[256] = "";
    size_t input_len = 0;
    size_t cursor_pos = 0;
    char result_buf[128] = "";
    char comp_buf[512] = "";
    char words_buf[512] = "";
    int words_sel_start = -1;
    int words_sel_end = -1;
    bool words_dragging = false;
    double current_evaluated_val = 0.0;
    bool has_valid_result = false;
    bool current_is_conv = false;
    bool current_is_date = false;
    bool current_is_time = false;
    char current_unit_sym[32] = "";
    char current_words_sfx[64] = "";
    bool is_showing_hint = false;
    RetroParenState retro_state = {0};
    AutocompleteSuggestion autocomp_state = {0};
    size_t autocomp_cycle_idx = 0;
    bool autocomp_just_cycled = false;
    size_t autocomp_prev_comp_len = 0;
    char last_constant_op[128] = "";
    bool just_evaluated_constant = false;
    CurrencyState currency_state;
    currency_init_state(&currency_state);
    UndoStack undo_stack;
    undo_stack_init(&undo_stack, input_buf, cursor_pos);

    Atom utf8_string = XInternAtom(dpy, "UTF8_STRING", False);
    Atom targets = XInternAtom(dpy, "TARGETS", False);

    XEvent ev;
    bool running = true;
    double t_loop_start = get_time_ms();
    double t_first_event = 0.0;
    double t_expose_start = 0.0;
    int event_count_before_expose = 0;

    while (running) {
        XNextEvent(dpy, &ev);
        if (t_first_event == 0.0) t_first_event = get_time_ms();
        if (ev.type != Expose) event_count_before_expose++;

        if (ev.type == Expose) {
            /* Drain any queued redundant Expose events to prevent redundant redraw passes */
            XEvent exp_ev;
            while (XCheckTypedWindowEvent(dpy, win, Expose, &exp_ev));
            t_expose_start = get_time_ms();
            /* Guard: if Cairo context is ever in an error state, recreate it cleanly */
            if (cairo_status(cr) != CAIRO_STATUS_SUCCESS) {
                cairo_destroy(cr);
                cr = cairo_create(buf_surf);
            }

            /* Render Background: Modern dark slate #1e1e24 */
            cairo_set_source_rgb(cr, 0.118, 0.118, 0.141);
            cairo_paint(cr);

            /* Border: Subtle outline #3c3c4a */
            cairo_set_source_rgb(cr, 0.235, 0.235, 0.290);
            cairo_set_line_width(cr, 2.0);
            cairo_rectangle(cr, 1.0, 1.0, win_w - 2.0, cur_h - 2.0);
            cairo_stroke(cr);

            /* Prompt Icon "> " */
            double t_before_first_text = get_time_ms();
            set_font(cr, true);
            cairo_set_font_size(cr, 20.0 * font_scale);
            cairo_set_source_rgb(cr, 0.294, 0.588, 0.980); /* Accent blue */
            cairo_move_to(cr, 20.0, 30.0);
            cairo_show_text(cr, ">");
            double t_after_first_text = get_time_ms();

            /* Input Expression or Placeholder */
            set_font(cr, false);
            cairo_set_font_size(cr, 20.0 * font_scale);

            if (input_len == 0) {
                const char *placeholder = "Type equation (e.g. sqrt(144) + 2pi or 500 - 20%)";
                cairo_set_source_rgb(cr, 0.45, 0.45, 0.50); /* Placeholder */
                cairo_move_to(cr, 46.0, 30.0);
                cairo_show_text(cr, placeholder);

                /* Cursor at start */
                cairo_set_source_rgb(cr, 0.294, 0.588, 0.980);
                cairo_rectangle(cr, 46.0, 30.0 - 18.0 * font_scale, 2.0, 22.0 * font_scale);
                cairo_fill(cr);
            } else {
                /* Visual Bracket Matching: Highlight matching pair if cursor touches bracket */
                int match_a = -1, match_b = -1;
                find_matching_bracket(input_buf, input_len, cursor_pos, &match_a, &match_b);
                if (match_a >= 0 && match_b >= 0) {
                    int matches[2] = { match_a, match_b };
                    for (int m = 0; m < 2; m++) {
                        char prefix[256];
                        size_t p_len = (size_t)matches[m];
                        if (p_len > sizeof(prefix) - 1) p_len = sizeof(prefix) - 1;
                        memcpy(prefix, input_buf, p_len);
                        prefix[p_len] = '\0';

                        cairo_text_extents_t ext_p;
                        cairo_text_extents(cr, prefix, &ext_p);
                        double bx = 46.0 + ext_p.x_advance;

                        char single[2] = { input_buf[matches[m]], '\0' };
                        cairo_text_extents_t ext_s;
                        cairo_text_extents(cr, single, &ext_s);

                        cairo_set_source_rgb(cr, 0.18, 0.32, 0.56); /* Soft luminous blue highlight */
                        cairo_rectangle(cr, bx - 1.0, 30.0 - 18.0 * font_scale, ext_s.x_advance + 2.0, 22.0 * font_scale);
                        cairo_fill(cr);
                    }
                }

                if (retro_state.active && retro_state.cut_count > 0) {
                    typedef struct {
                        size_t pos;
                        bool is_active;
                    } RenderCut;
                    RenderCut cuts[8];
                    int n_cuts = 0;

                    if (retro_state.mode == BRACKET_CYCLE_RETRO) {
                        size_t active_cut_pos = retro_state.cut_points[retro_state.current_idx];
                        size_t close_pos = retro_state.anchor;
                        if (close_pos > input_len) close_pos = input_len;

                        /* Collect distinct cut points <= close_pos */
                        for (int i = 0; i < retro_state.cut_count && i < 8; i++) {
                            size_t p = retro_state.cut_points[i];
                            if (p > close_pos) p = close_pos;
                            bool found = false;
                            for (int j = 0; j < n_cuts; j++) {
                                if (cuts[j].pos == p) {
                                    if (p == active_cut_pos) cuts[j].is_active = true;
                                    found = true;
                                    break;
                                }
                            }
                            if (!found && n_cuts < 8) {
                                cuts[n_cuts].pos = p;
                                cuts[n_cuts].is_active = (p == active_cut_pos);
                                n_cuts++;
                            }
                        }

                        /* Sort cuts in ascending order of pos */
                        for (int i = 0; i < n_cuts - 1; i++) {
                            for (int j = i + 1; j < n_cuts; j++) {
                                if (cuts[i].pos > cuts[j].pos) {
                                    RenderCut tmp = cuts[i];
                                    cuts[i] = cuts[j];
                                    cuts[j] = tmp;
                                }
                            }
                        }

                        double cur_x = 46.0;
                        size_t cur_pos = 0;
                        cairo_text_extents_t ext_seg;

                        for (int i = 0; i < n_cuts; i++) {
                            if (cuts[i].pos > cur_pos) {
                                size_t seg_len = cuts[i].pos - cur_pos;
                                char seg[256];
                                if (seg_len >= sizeof(seg)) seg_len = sizeof(seg) - 1;
                                memcpy(seg, &input_buf[cur_pos], seg_len);
                                seg[seg_len] = '\0';

                                cairo_set_source_rgb(cr, 0.95, 0.95, 0.97);
                                cairo_move_to(cr, cur_x, 30.0);
                                cairo_show_text(cr, seg);
                                cairo_text_extents(cr, seg, &ext_seg);
                                cur_x += ext_seg.x_advance;
                                cur_pos = cuts[i].pos;
                            }

                            /* Render ghost '(': bright if active, dim if alternative */
                            if (cuts[i].is_active) {
                                cairo_set_source_rgba(cr, 0.20, 0.85, 0.95, 0.95); /* Bright luminous cyan */
                            } else {
                                cairo_set_source_rgba(cr, 0.45, 0.58, 0.75, 0.55); /* Dim luminous muted ghost */
                            }
                            cairo_move_to(cr, cur_x, 30.0);
                            cairo_show_text(cr, "(");
                            cairo_text_extents(cr, "(", &ext_seg);
                            cur_x += ext_seg.x_advance;
                        }

                        /* Remaining text up to close_pos */
                        if (close_pos > cur_pos) {
                            size_t seg_len = close_pos - cur_pos;
                            char seg[256];
                            if (seg_len >= sizeof(seg)) seg_len = sizeof(seg) - 1;
                            memcpy(seg, &input_buf[cur_pos], seg_len);
                            seg[seg_len] = '\0';

                            cairo_set_source_rgb(cr, 0.95, 0.95, 0.97);
                            cairo_move_to(cr, cur_x, 30.0);
                            cairo_show_text(cr, seg);
                            cairo_text_extents(cr, seg, &ext_seg);
                            cur_x += ext_seg.x_advance;
                            cur_pos = close_pos;
                        }

                        /* Active ghost ')' at cursor */
                        cairo_set_source_rgba(cr, 0.20, 0.85, 0.95, 0.95);
                        cairo_move_to(cr, cur_x, 30.0);
                        cairo_show_text(cr, ")");
                        cairo_text_extents(cr, ")", &ext_seg);
                        cur_x += ext_seg.x_advance;

                        double cursor_x = cur_x;

                        /* Suffix after close_pos */
                        if (input_len > close_pos) {
                            size_t seg_len = input_len - close_pos;
                            char seg[256];
                            if (seg_len >= sizeof(seg)) seg_len = sizeof(seg) - 1;
                            memcpy(seg, &input_buf[close_pos], seg_len);
                            seg[seg_len] = '\0';

                            cairo_set_source_rgb(cr, 0.95, 0.95, 0.97);
                            cairo_move_to(cr, cur_x, 30.0);
                            cairo_show_text(cr, seg);
                        }

                        /* Draw cursor */
                        cairo_set_source_rgb(cr, 0.294, 0.588, 0.980);
                        cairo_rectangle(cr, cursor_x, 30.0 - 18.0 * font_scale, 2.0, 22.0 * font_scale);
                        cairo_fill(cr);
                    } else {
                        /* BRACKET_CYCLE_PRO: anchor is open_pos, cuts are candidate close_pos */
                        size_t active_cut_pos = retro_state.cut_points[retro_state.current_idx];
                        size_t open_pos = retro_state.anchor;
                        if (open_pos > input_len) open_pos = input_len;

                        /* Collect distinct cut points >= open_pos */
                        for (int i = 0; i < retro_state.cut_count && i < 8; i++) {
                            size_t p = retro_state.cut_points[i];
                            if (p < open_pos) p = open_pos;
                            if (p > input_len) p = input_len;
                            bool found = false;
                            for (int j = 0; j < n_cuts; j++) {
                                if (cuts[j].pos == p) {
                                    if (p == active_cut_pos) cuts[j].is_active = true;
                                    found = true;
                                    break;
                                }
                            }
                            if (!found && n_cuts < 8) {
                                cuts[n_cuts].pos = p;
                                cuts[n_cuts].is_active = (p == active_cut_pos);
                                n_cuts++;
                            }
                        }

                        /* Sort cuts in ascending order of pos */
                        for (int i = 0; i < n_cuts - 1; i++) {
                            for (int j = i + 1; j < n_cuts; j++) {
                                if (cuts[i].pos > cuts[j].pos) {
                                    RenderCut tmp = cuts[i];
                                    cuts[i] = cuts[j];
                                    cuts[j] = tmp;
                                }
                            }
                        }

                        double cur_x = 46.0;
                        size_t cur_pos = 0;
                        cairo_text_extents_t ext_seg;

                        /* Text before open_pos */
                        if (open_pos > 0) {
                            size_t seg_len = open_pos;
                            char seg[256];
                            if (seg_len >= sizeof(seg)) seg_len = sizeof(seg) - 1;
                            memcpy(seg, input_buf, seg_len);
                            seg[seg_len] = '\0';

                            cairo_set_source_rgb(cr, 0.95, 0.95, 0.97);
                            cairo_move_to(cr, cur_x, 30.0);
                            cairo_show_text(cr, seg);
                            cairo_text_extents(cr, seg, &ext_seg);
                            cur_x += ext_seg.x_advance;
                            cur_pos = open_pos;
                        }

                        /* Active ghost '(' at open_pos */
                        cairo_set_source_rgba(cr, 0.20, 0.85, 0.95, 0.95);
                        cairo_move_to(cr, cur_x, 30.0);
                        cairo_show_text(cr, "(");
                        cairo_text_extents(cr, "(", &ext_seg);
                        cur_x += ext_seg.x_advance;
                        double cursor_x = cur_x;

                        /* Render text segments up to each cut point, followed by ghost ')' */
                        for (int i = 0; i < n_cuts; i++) {
                            if (cuts[i].pos > cur_pos) {
                                size_t seg_len = cuts[i].pos - cur_pos;
                                char seg[256];
                                if (seg_len >= sizeof(seg)) seg_len = sizeof(seg) - 1;
                                memcpy(seg, &input_buf[cur_pos], seg_len);
                                seg[seg_len] = '\0';

                                cairo_set_source_rgb(cr, 0.95, 0.95, 0.97);
                                cairo_move_to(cr, cur_x, 30.0);
                                cairo_show_text(cr, seg);
                                cairo_text_extents(cr, seg, &ext_seg);
                                cur_x += ext_seg.x_advance;
                                cur_pos = cuts[i].pos;
                            }

                            if (cuts[i].is_active) {
                                cairo_set_source_rgba(cr, 0.20, 0.85, 0.95, 0.95); /* Bright luminous cyan */
                            } else {
                                cairo_set_source_rgba(cr, 0.45, 0.58, 0.75, 0.55); /* Dim luminous muted ghost */
                            }
                            cairo_move_to(cr, cur_x, 30.0);
                            cairo_show_text(cr, ")");
                            cairo_text_extents(cr, ")", &ext_seg);
                            cur_x += ext_seg.x_advance;
                        }

                        /* Suffix after furthest cut */
                        if (input_len > cur_pos) {
                            size_t seg_len = input_len - cur_pos;
                            char seg[256];
                            if (seg_len >= sizeof(seg)) seg_len = sizeof(seg) - 1;
                            memcpy(seg, &input_buf[cur_pos], seg_len);
                            seg[seg_len] = '\0';

                            cairo_set_source_rgb(cr, 0.95, 0.95, 0.97);
                            cairo_move_to(cr, cur_x, 30.0);
                            cairo_show_text(cr, seg);
                        }

                        /* Draw cursor */
                        cairo_set_source_rgb(cr, 0.294, 0.588, 0.980);
                        cairo_rectangle(cr, cursor_x, 30.0 - 18.0 * font_scale, 2.0, 22.0 * font_scale);
                        cairo_fill(cr);
                    }
                } else {
                    cairo_set_source_rgb(cr, 0.95, 0.95, 0.97); /* Crisp white */
                    cairo_move_to(cr, 46.0, 30.0);
                    cairo_show_text(cr, input_buf);

                    /* Intelligent autocomplete ghost text or predicted closing brackets */
                    if (autocomp_state.active && !autocomp_just_cycled && !retro_state.active && cursor_pos == input_len && autocomp_state.ghost_suffix[0]) {
                        cairo_text_extents_t ext_in;
                        cairo_text_extents(cr, input_buf, &ext_in);
                        double ghost_x = 46.0 + ext_in.x_advance;

                        cairo_set_source_rgba(cr, 0.45, 0.58, 0.75, 0.55); /* Luminous muted accent ghost */
                        cairo_move_to(cr, ghost_x, 30.0);
                        cairo_show_text(cr, autocomp_state.ghost_suffix);
                    } else {
                        /* Intelligent ghost predicted closing brackets */
                        int unclosed = count_unclosed_parens(input_buf, input_len);
                        if (unclosed > 0) {
                            cairo_text_extents_t ext_in;
                            cairo_text_extents(cr, input_buf, &ext_in);
                            double ghost_x = 46.0 + ext_in.x_advance;

                            char ghost_str[32];
                            if (unclosed > 16) unclosed = 16;
                            for (int g = 0; g < unclosed; g++) ghost_str[g] = ')';
                            ghost_str[unclosed] = '\0';

                            cairo_set_source_rgba(cr, 0.45, 0.58, 0.75, 0.55); /* Luminous muted accent ghost */
                            cairo_move_to(cr, ghost_x, 30.0);
                            cairo_show_text(cr, ghost_str);
                        }
                    }

                    /* Cursor positioned at cursor_pos */
                    char before_cursor[256];
                    size_t c_pos = cursor_pos;
                    if (c_pos > input_len) c_pos = input_len;
                    if (c_pos > sizeof(before_cursor) - 1) c_pos = sizeof(before_cursor) - 1;
                    memcpy(before_cursor, input_buf, c_pos);
                    before_cursor[c_pos] = '\0';

                    cairo_text_extents_t ext;
                    cairo_text_extents(cr, before_cursor, &ext);
                    double cursor_x = 46.0 + ext.x_advance;
                    cairo_set_source_rgb(cr, 0.294, 0.588, 0.980);
                    cairo_rectangle(cr, cursor_x, 30.0 - 18.0 * font_scale, 2.0, 22.0 * font_scale);
                    cairo_fill(cr);
                }
            }

            /* Row 2: Live Evaluated Result (Dedicated Line, Right-aligned) */
            if (has_valid_result) {
                char display_res[160];
                snprintf(display_res, sizeof(display_res), "= %s", result_buf);

                set_font(cr, true);
                cairo_set_font_size(cr, 22.0 * font_scale);
                cairo_set_source_rgb(cr, 0.200, 0.820, 0.480); /* Bright mint green */

                cairo_text_extents_t ext_res;
                cairo_text_extents(cr, display_res, &ext_res);
                double res_x = (double)win_w - ext_res.width - 24.0;
                cairo_move_to(cr, res_x, 68.0);
                cairo_show_text(cr, display_res);
            }

            /* Divider 1 between Result Row and Vars Row at Y = 82 */
            cairo_set_source_rgb(cr, 0.20, 0.20, 0.26);
            cairo_set_line_width(cr, 1.0);
            cairo_move_to(cr, 12.0, 82.0);
            cairo_line_to(cr, win_w - 12.0, 82.0);
            cairo_stroke(cr);

            /* Row 3: Variables Row (Y = 82 to 110) */
            set_font(cr, false);
            cairo_set_font_size(cr, 11.0 * font_scale);
            cairo_set_source_rgb(cr, 0.78, 0.62, 0.96); /* Soft lavender */
            cairo_move_to(cr, 20.0, 100.0);
            cairo_show_text(cr, "Vars:");

            CalcSymbol all_syms[MAX_SYMBOLS];
            int sym_cnt = get_all_symbols(all_syms, MAX_SYMBOLS);
            int session_cnt = 0;
            for (int v = 0; v < sym_cnt; v++) {
                if (!all_syms[v].is_persistent) session_cnt++;
            }
            int var_cnt = session_cnt;

            double del_btn_w = 84.0, del_btn_h = 20.0;
            double del_btn_x = win_w - del_btn_w - 20.0, del_btn_y = 86.0;

            /* Persistent ans formatting */
            char ans_str[32];
            format_result_ex(ans_val, comma_fmt, ans_str, sizeof(ans_str));

            char vars_str[512] = "";
            snprintf(vars_str, sizeof(vars_str), "ans = %s", ans_str);

            for (int v = 0; v < sym_cnt; v++) {
                CalcSymbol *s = &all_syms[v];
                if (s->is_persistent) continue; /* Show only session variables created with '=' */
                char v_item[256];
                if (s->is_func) {
                    char p_list[32] = "";
                    for (int p = 0; p < s->param_count; p++) {
                        if (p > 0) strncat(p_list, ",", sizeof(p_list) - strlen(p_list) - 1);
                        strncat(p_list, s->params[p], sizeof(p_list) - strlen(p_list) - 1);
                    }
                    snprintf(v_item, sizeof(v_item), "%.31s(%.31s) %s %.127s", s->name, p_list, s->is_persistent ? ":=" : "=", s->expr);
                } else if (s->expr[0] != '\0') {
                    double live_v = 0.0;
                    get_variable(s->name, &live_v);
                    char v_val_str[32];
                    if (s->is_date) {
                        format_date_result(live_v, v_val_str, sizeof(v_val_str));
                    } else if (s->is_time) {
                        format_time_result(live_v, false, v_val_str, sizeof(v_val_str));
                    } else if (s->unit[0] && format_compound_unit(live_v, s->unit, v_val_str, sizeof(v_val_str))) {
                        /* formatted as compound */
                    } else {
                        format_result_ex(live_v, FMT_PLAIN, v_val_str, sizeof(v_val_str));
                    }
                    snprintf(v_item, sizeof(v_item), "%.31s %s %.127s (%.31s)", s->name, s->is_persistent ? ":=" : "=", s->expr, v_val_str);
                } else {
                    char v_val_str[32];
                    if (s->is_date) {
                        format_date_result(s->cached_val, v_val_str, sizeof(v_val_str));
                    } else if (s->is_time) {
                        format_time_result(s->cached_val, false, v_val_str, sizeof(v_val_str));
                    } else if (s->unit[0] && format_compound_unit(s->cached_val, s->unit, v_val_str, sizeof(v_val_str))) {
                        /* formatted as compound */
                    } else {
                        format_result_ex(s->cached_val, FMT_PLAIN, v_val_str, sizeof(v_val_str));
                    }
                    snprintf(v_item, sizeof(v_item), "%.31s %s %.31s", s->name, s->is_persistent ? ":=" : "=", v_val_str);
                }
                if (strlen(vars_str) + 5 < sizeof(vars_str)) {
                    strcat(vars_str, "  •  ");
                }
                if (strlen(vars_str) + strlen(v_item) < sizeof(vars_str)) {
                    strcat(vars_str, v_item);
                }
            }

            /* Truncate if string approaches Delete All button (or edge of window) */
            set_font(cr, true);
            cairo_set_font_size(cr, 11.5 * font_scale);
            cairo_text_extents_t v_ext;
            cairo_text_extents(cr, vars_str, &v_ext);
            double max_vars_w = (var_cnt > 0) ? (del_btn_x - 76.0) : (win_w - 88.0);
            if (max_vars_w < 60.0) max_vars_w = 60.0;
            if (v_ext.width > max_vars_w) {
                while (v_ext.width > max_vars_w && strlen(vars_str) > 0) {
                    utf8_truncate_char(vars_str);
                    char tmp[512];
                    snprintf(tmp, sizeof(tmp), "%s...", vars_str);
                    cairo_text_extents(cr, tmp, &v_ext);
                    if (v_ext.width <= max_vars_w) {
                        strcpy(vars_str, tmp);
                        break;
                    }
                }
                if (strlen(vars_str) == 0) {
                    strcpy(vars_str, "...");
                }
            }

            cairo_set_source_rgb(cr, 0.92, 0.86, 0.98); /* Pale lilac */
            cairo_move_to(cr, 68.0, 100.0);
            cairo_show_text(cr, vars_str);

            /* Render "✕ Delete All" Button only when user variables exist */
            if (var_cnt > 0) {
                cairo_set_source_rgb(cr, 0.28, 0.12, 0.14); /* Subtle dark red bg */
                cairo_rectangle(cr, del_btn_x, del_btn_y, del_btn_w, del_btn_h);
                cairo_fill(cr);
                cairo_set_source_rgb(cr, 0.58, 0.22, 0.25); /* Subtle red outline */
                cairo_set_line_width(cr, 1.0);
                cairo_rectangle(cr, del_btn_x, del_btn_y, del_btn_w, del_btn_h);
                cairo_stroke(cr);

                set_font(cr, true);
                cairo_set_font_size(cr, 10.5 * font_scale);
                cairo_set_source_rgb(cr, 0.95, 0.45, 0.48); /* Coral red label */
                cairo_move_to(cr, del_btn_x + 8.0, del_btn_y + 14.5);
                cairo_show_text(cr, "✕ Delete All");
            }

            /* Divider 2 between Variables Row and Words Row at Y = 110 */
            cairo_set_source_rgb(cr, 0.20, 0.20, 0.26);
            cairo_set_line_width(cr, 1.0);
            cairo_move_to(cr, 12.0, 110.0);
            cairo_line_to(cr, win_w - 12.0, 110.0);
            cairo_stroke(cr);

            /* Row 4: Spoken Words / Hints / Autocomplete Candidates (Y = 110 to 138) */
            set_font(cr, false);
            cairo_set_font_size(cr, 11.0 * font_scale);
            if (autocomp_state.active && autocomp_state.match_count > 1) {
                cairo_set_source_rgb(cr, 0.45, 0.75, 0.98); /* Bright cyan */
                cairo_move_to(cr, 20.0, 128.0);
                cairo_show_text(cr, "Complete:");
            } else if (is_showing_hint) {
                cairo_set_source_rgb(cr, 0.294, 0.588, 0.980); /* Bright accent blue */
                cairo_move_to(cr, 20.0, 128.0);
                cairo_show_text(cr, "Hints:");
            } else {
                cairo_set_source_rgb(cr, 0.45, 0.45, 0.55);
                cairo_move_to(cr, 20.0, 128.0);
                cairo_show_text(cr, "Words:");
            }

            if (autocomp_state.active && autocomp_state.match_count > 1 && autocomp_state.preview_bar[0]) {
                set_font(cr, true);
                cairo_set_font_size(cr, 12.0 * font_scale);
                cairo_set_source_rgb(cr, 0.88, 0.88, 0.98); /* Bright lavender */
                cairo_move_to(cr, 86.0, 128.0);
                cairo_show_text(cr, autocomp_state.preview_bar);
            } else if ((has_valid_result || is_showing_hint) && words_buf[0]) {
                /* Render blue selection highlight if text is selected */
                if (words_sel_start >= 0 && words_sel_end >= 0 && words_sel_start != words_sel_end) {
                    int s = words_sel_start < words_sel_end ? words_sel_start : words_sel_end;
                    int e = words_sel_start < words_sel_end ? words_sel_end : words_sel_start;
                    size_t wlen = strlen(words_buf);
                    if (s > (int)wlen) s = (int)wlen;
                    if (e > (int)wlen) e = (int)wlen;

                    char p_s[256], p_e[256];
                    memcpy(p_s, words_buf, s); p_s[s] = '\0';
                    memcpy(p_e, words_buf, e); p_e[e] = '\0';

                    set_font(cr, true);
                    cairo_set_font_size(cr, 13.5 * font_scale);
                    cairo_text_extents_t ext_s, ext_e;
                    cairo_text_extents(cr, p_s, &ext_s);
                    cairo_text_extents(cr, p_e, &ext_e);

                    double x1 = 68.0 + ext_s.x_advance;
                    double x2 = 68.0 + ext_e.x_advance;

                    cairo_set_source_rgb(cr, 0.15, 0.35, 0.65); /* Desktop selection blue */
                    cairo_rectangle(cr, x1 - 1.0, 128.0 - 14.0 * font_scale, (x2 - x1) + 2.0, 18.0 * font_scale);
                    cairo_fill(cr);
                }

                set_font(cr, true);
                cairo_set_font_size(cr, 13.5 * font_scale);
                if (is_showing_hint) {
                    cairo_set_source_rgb(cr, 0.35, 0.85, 0.65); /* Mint green for hints */
                } else {
                    cairo_set_source_rgb(cr, 0.70, 0.85, 1.0); /* Soft luminous cyan */
                }
                cairo_move_to(cr, 68.0, 128.0);
                cairo_show_text(cr, words_buf);
            } else {
                set_font(cr, false);
                cairo_set_font_size(cr, 13.5 * font_scale);
                cairo_set_source_rgb(cr, 0.35, 0.35, 0.42);
                cairo_move_to(cr, 68.0, 128.0);
                cairo_show_text(cr, "—");
            }

            /* Divider 3 between Words Row and Computing Row at Y = 138 */
            cairo_set_source_rgb(cr, 0.20, 0.20, 0.26);
            cairo_set_line_width(cr, 1.0);
            cairo_move_to(cr, 12.0, 138.0);
            cairo_line_to(cr, win_w - 12.0, 138.0);
            cairo_stroke(cr);

            /* Row 5: Computing / Radix Row (Y = 138 to 168) */
            set_font(cr, false);
            cairo_set_font_size(cr, 11.0 * font_scale);
            cairo_set_source_rgb(cr, 0.45, 0.45, 0.55);
            cairo_move_to(cr, 20.0, 156.0);
            cairo_show_text(cr, output_mode == OUTPUT_ALL ? "Dec:" : "Base:");

            if (has_valid_result && comp_buf[0]) {
                set_font(cr, true);
                cairo_set_font_size(cr, 12.0 * font_scale);
                cairo_set_source_rgb(cr, 0.70, 0.85, 1.0); /* Soft cyan */
                cairo_move_to(cr, 68.0, 156.0);
                cairo_show_text(cr, comp_buf);
            } else {
                set_font(cr, false);
                cairo_set_font_size(cr, 12.0 * font_scale);
                cairo_set_source_rgb(cr, 0.35, 0.35, 0.42);
                cairo_move_to(cr, 68.0, 156.0);
                cairo_show_text(cr, "—");
            }

            /* Divider 4 between Computing Row and Footer Row at Y = 168 */
            cairo_set_source_rgb(cr, 0.20, 0.20, 0.26);
            cairo_set_line_width(cr, 1.0);
            cairo_move_to(cr, 12.0, 168.0);
            cairo_line_to(cr, win_w - 12.0, 168.0);
            cairo_stroke(cr);

            /* Footer Row: Keyboard shortcuts on left (Y: 168 to 204) */
            set_font(cr, false);
            cairo_set_font_size(cr, 11.5 * (font_scale > 1.2 ? 1.2 : font_scale));
            cairo_set_source_rgb(cr, 0.45, 0.45, 0.55);
            cairo_move_to(cr, 20.0, 189.0);
            cairo_show_text(cr, "[Enter] Calc    [Tab/→] Complete    [Ctrl+Z/Y] Undo/Redo    [Ctrl+C] Copy    [Ctrl+;] Settings");

            /* Footer Row: Dedicated Gear Settings Button on right */
            double btn_w = 34.0, btn_h = 28.0;
            double btn_x = win_w - btn_w - 14.0, btn_y = 172.0;
            bool gear_hovered = (g_hover_type == 8);
            if (drawer_open) {
                cairo_set_source_rgb(cr, 0.20, 0.28, 0.44); /* Active blue bg */
                draw_rounded_rect(cr, btn_x, btn_y, btn_w, btn_h, 6.0);
                cairo_fill(cr);
                cairo_set_source_rgb(cr, 0.48, 0.64, 0.97); /* Border */
                cairo_set_line_width(cr, 1.0);
                draw_rounded_rect(cr, btn_x, btn_y, btn_w, btn_h, 6.0);
                cairo_stroke(cr);
                cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
            } else if (gear_hovered) {
                cairo_set_source_rgb(cr, 0.22, 0.24, 0.32); /* Hover bg */
                draw_rounded_rect(cr, btn_x, btn_y, btn_w, btn_h, 6.0);
                cairo_fill(cr);
                cairo_set_source_rgb(cr, 0.45, 0.48, 0.60); /* Hover border */
                cairo_set_line_width(cr, 1.0);
                draw_rounded_rect(cr, btn_x, btn_y, btn_w, btn_h, 6.0);
                cairo_stroke(cr);
                cairo_set_source_rgb(cr, 0.95, 0.95, 1.0);
            } else {
                cairo_set_source_rgb(cr, 0.16, 0.16, 0.20); /* Subtle dark bg */
                draw_rounded_rect(cr, btn_x, btn_y, btn_w, btn_h, 6.0);
                cairo_fill(cr);
                cairo_set_source_rgb(cr, 0.28, 0.28, 0.35); /* Subtle outline */
                cairo_set_line_width(cr, 1.0);
                draw_rounded_rect(cr, btn_x, btn_y, btn_w, btn_h, 6.0);
                cairo_stroke(cr);
                cairo_set_source_rgb(cr, 0.65, 0.65, 0.75);
            }
            set_font(cr, true);
            cairo_set_font_size(cr, 17.5 * font_scale);
            cairo_text_extents_t ext_g;
            cairo_text_extents(cr, "⚙", &ext_g);
            double gx = btn_x + (btn_w - ext_g.width) / 2.0 - ext_g.x_bearing;
            double gy = btn_y + (btn_h - ext_g.height) / 2.0 - ext_g.y_bearing;
            cairo_move_to(cr, gx, gy);
            cairo_show_text(cr, "⚙");

            /* Render Vertical Drawer Tray (if open) */
            if (drawer_open) {
                /* Divider line at 204px */
                cairo_set_source_rgb(cr, 0.20, 0.20, 0.26);
                cairo_set_line_width(cr, 1.0);
                cairo_move_to(cr, 12.0, 204.0);
                cairo_line_to(cr, win_w - 12.0, 204.0);
                cairo_stroke(cr);

                /* Line 1: Format / Grouping (Y = 206 to 236, baseline 225.5) */
                set_font(cr, true);
                cairo_set_font_size(cr, 11.0 * font_scale);
                cairo_set_source_rgb(cr, 0.70, 0.70, 0.80);
                cairo_move_to(cr, 16.0, 225.5);
                cairo_show_text(cr, "Format:");

                /* Format Pills */
                struct { const char *label; CommaFormat fmt; double x; double w; } fmt_pills[] = {
                    { "Intl (1,000,000)",   FMT_INTL,   78.0,  124.0 },
                    { "Indian (10,00,000)", FMT_INDIAN, 208.0, 136.0 },
                    { "Plain (1000000)",    FMT_PLAIN,  350.0, 118.0 }
                };
                for (size_t i = 0; i < sizeof(fmt_pills)/sizeof(fmt_pills[0]); i++) {
                    bool active = (comma_fmt == fmt_pills[i].fmt);
                    bool hovered = (g_hover_type == 1 && g_hover_idx == (int)i);
                    if (active) {
                        cairo_set_source_rgb(cr, 0.15, 0.23, 0.38);
                        draw_rounded_rect(cr, fmt_pills[i].x, 211.0, fmt_pills[i].w, 20.0, 4.0);
                        cairo_fill(cr);
                        cairo_set_source_rgb(cr, 0.48, 0.64, 0.97);
                        cairo_set_line_width(cr, 1.0);
                        draw_rounded_rect(cr, fmt_pills[i].x, 211.0, fmt_pills[i].w, 20.0, 4.0);
                        cairo_stroke(cr);
                        cairo_set_source_rgb(cr, 0.88, 0.93, 1.0);
                    } else if (hovered) {
                        cairo_set_source_rgb(cr, 0.22, 0.24, 0.32);
                        draw_rounded_rect(cr, fmt_pills[i].x, 211.0, fmt_pills[i].w, 20.0, 4.0);
                        cairo_fill(cr);
                        cairo_set_source_rgb(cr, 0.38, 0.42, 0.55);
                        cairo_set_line_width(cr, 1.0);
                        draw_rounded_rect(cr, fmt_pills[i].x, 211.0, fmt_pills[i].w, 20.0, 4.0);
                        cairo_stroke(cr);
                        cairo_set_source_rgb(cr, 0.85, 0.88, 0.98);
                    } else {
                        cairo_set_source_rgb(cr, 0.14, 0.15, 0.20);
                        draw_rounded_rect(cr, fmt_pills[i].x, 211.0, fmt_pills[i].w, 20.0, 4.0);
                        cairo_fill(cr);
                        cairo_set_source_rgb(cr, 0.22, 0.24, 0.30);
                        cairo_set_line_width(cr, 1.0);
                        draw_rounded_rect(cr, fmt_pills[i].x, 211.0, fmt_pills[i].w, 20.0, 4.0);
                        cairo_stroke(cr);
                        cairo_set_source_rgb(cr, 0.55, 0.55, 0.65);
                    }
                    cairo_move_to(cr, fmt_pills[i].x + 7.0, 225.5);
                    cairo_show_text(cr, fmt_pills[i].label);
                }

                /* Line 2: Decimals Precision (Y = 237 to 266, baseline 256.0) */
                set_font(cr, true);
                cairo_set_font_size(cr, 11.0 * font_scale);
                cairo_set_source_rgb(cr, 0.70, 0.70, 0.80);
                cairo_move_to(cr, 16.0, 256.0);
                cairo_show_text(cr, "Decimals:");

                struct { const char *label; int val; double x; double w; } dec_pills[] = {
                    { "Auto", -1, 78.0, 38.0 },
                    { "0",     0, 122.0, 24.0 },
                    { "1",     1, 152.0, 24.0 },
                    { "2",     2, 182.0, 24.0 },
                    { "3",     3, 212.0, 24.0 },
                    { "4",     4, 242.0, 24.0 },
                    { "6",     6, 272.0, 24.0 },
                    { "8",     8, 302.0, 24.0 }
                };

                for (size_t p = 0; p < sizeof(dec_pills)/sizeof(dec_pills[0]); p++) {
                    bool active = (decimal_places == dec_pills[p].val);
                    bool hovered = (g_hover_type == 2 && g_hover_idx == (int)p);
                    if (active) {
                        cairo_set_source_rgb(cr, 0.25, 0.18, 0.35); /* Purple accent */
                        draw_rounded_rect(cr, dec_pills[p].x, 241.5, dec_pills[p].w, 20.0, 4.0);
                        cairo_fill(cr);
                        cairo_set_source_rgb(cr, 0.78, 0.60, 0.98);
                        cairo_set_line_width(cr, 1.0);
                        draw_rounded_rect(cr, dec_pills[p].x, 241.5, dec_pills[p].w, 20.0, 4.0);
                        cairo_stroke(cr);
                        cairo_set_source_rgb(cr, 0.92, 0.85, 1.0);
                    } else if (hovered) {
                        cairo_set_source_rgb(cr, 0.22, 0.24, 0.32);
                        draw_rounded_rect(cr, dec_pills[p].x, 241.5, dec_pills[p].w, 20.0, 4.0);
                        cairo_fill(cr);
                        cairo_set_source_rgb(cr, 0.38, 0.42, 0.55);
                        cairo_set_line_width(cr, 1.0);
                        draw_rounded_rect(cr, dec_pills[p].x, 241.5, dec_pills[p].w, 20.0, 4.0);
                        cairo_stroke(cr);
                        cairo_set_source_rgb(cr, 0.85, 0.88, 0.98);
                    } else {
                        cairo_set_source_rgb(cr, 0.14, 0.15, 0.20);
                        draw_rounded_rect(cr, dec_pills[p].x, 241.5, dec_pills[p].w, 20.0, 4.0);
                        cairo_fill(cr);
                        cairo_set_source_rgb(cr, 0.22, 0.24, 0.30);
                        cairo_set_line_width(cr, 1.0);
                        draw_rounded_rect(cr, dec_pills[p].x, 241.5, dec_pills[p].w, 20.0, 4.0);
                        cairo_stroke(cr);
                        cairo_set_source_rgb(cr, 0.55, 0.55, 0.65);
                    }
                    cairo_move_to(cr, dec_pills[p].x + (dec_pills[p].val < 0 ? 5.0 : 8.0), 256.0);
                    cairo_show_text(cr, dec_pills[p].label);
                }

                /* Line 3: Output Mode (Y = 267 to 296, baseline 286.0) */
                set_font(cr, true);
                cairo_set_font_size(cr, 11.0 * font_scale);
                cairo_set_source_rgb(cr, 0.70, 0.70, 0.80);
                cairo_move_to(cr, 16.0, 286.0);
                cairo_show_text(cr, "Output:");

                struct { const char *label; OutputMode mode; double x; double w; } mode_pills[] = {
                    { "Comp Only", OUTPUT_COMP_ONLY,  78.0, 78.0 },
                    { "All",       OUTPUT_ALL,       162.0, 42.0 },
                    { "Simple",    OUTPUT_SIMPLE,    210.0, 58.0 }
                };

                for (size_t m = 0; m < sizeof(mode_pills)/sizeof(mode_pills[0]); m++) {
                    bool active = (output_mode == mode_pills[m].mode);
                    bool hovered = (g_hover_type == 3 && g_hover_idx == (int)m);
                    if (active) {
                        cairo_set_source_rgb(cr, 0.15, 0.23, 0.38);
                        draw_rounded_rect(cr, mode_pills[m].x, 271.5, mode_pills[m].w, 20.0, 4.0);
                        cairo_fill(cr);
                        cairo_set_source_rgb(cr, 0.48, 0.64, 0.97);
                        cairo_set_line_width(cr, 1.0);
                        draw_rounded_rect(cr, mode_pills[m].x, 271.5, mode_pills[m].w, 20.0, 4.0);
                        cairo_stroke(cr);
                        cairo_set_source_rgb(cr, 0.88, 0.93, 1.0);
                    } else if (hovered) {
                        cairo_set_source_rgb(cr, 0.22, 0.24, 0.32);
                        draw_rounded_rect(cr, mode_pills[m].x, 271.5, mode_pills[m].w, 20.0, 4.0);
                        cairo_fill(cr);
                        cairo_set_source_rgb(cr, 0.38, 0.42, 0.55);
                        cairo_set_line_width(cr, 1.0);
                        draw_rounded_rect(cr, mode_pills[m].x, 271.5, mode_pills[m].w, 20.0, 4.0);
                        cairo_stroke(cr);
                        cairo_set_source_rgb(cr, 0.85, 0.88, 0.98);
                    } else {
                        cairo_set_source_rgb(cr, 0.14, 0.15, 0.20);
                        draw_rounded_rect(cr, mode_pills[m].x, 271.5, mode_pills[m].w, 20.0, 4.0);
                        cairo_fill(cr);
                        cairo_set_source_rgb(cr, 0.22, 0.24, 0.30);
                        cairo_set_line_width(cr, 1.0);
                        draw_rounded_rect(cr, mode_pills[m].x, 271.5, mode_pills[m].w, 20.0, 4.0);
                        cairo_stroke(cr);
                        cairo_set_source_rgb(cr, 0.55, 0.55, 0.65);
                    }
                    cairo_move_to(cr, mode_pills[m].x + 7.0, 286.0);
                    cairo_show_text(cr, mode_pills[m].label);
                }

                /* Line 4: UI Scale / Zoom (Y = 297 to 328, baseline 316.0) */
                set_font(cr, true);
                cairo_set_font_size(cr, 11.0 * font_scale);
                cairo_set_source_rgb(cr, 0.70, 0.70, 0.80);
                cairo_move_to(cr, 16.0, 316.0);
                cairo_show_text(cr, "Scale:");

                struct { const char *label; double scale_val; double x; double w; } scale_pills[] = {
                    { "80%",  0.8,  78.0, 36.0 },
                    { "100%", 1.0, 120.0, 42.0 },
                    { "120%", 1.2, 168.0, 42.0 },
                    { "140%", 1.4, 216.0, 42.0 },
                    { "160%", 1.6, 264.0, 42.0 },
                    { "180%", 1.8, 312.0, 42.0 }
                };

                for (size_t s = 0; s < sizeof(scale_pills)/sizeof(scale_pills[0]); s++) {
                    bool active = (fabs(font_scale - scale_pills[s].scale_val) < 0.05);
                    bool hovered = (g_hover_type == 4 && g_hover_idx == (int)s);
                    if (active) {
                        cairo_set_source_rgb(cr, 0.12, 0.26, 0.18);
                        draw_rounded_rect(cr, scale_pills[s].x, 301.5, scale_pills[s].w, 20.0, 4.0);
                        cairo_fill(cr);
                        cairo_set_source_rgb(cr, 0.20, 0.82, 0.48);
                        cairo_set_line_width(cr, 1.0);
                        draw_rounded_rect(cr, scale_pills[s].x, 301.5, scale_pills[s].w, 20.0, 4.0);
                        cairo_stroke(cr);
                        cairo_set_source_rgb(cr, 0.85, 1.0, 0.90);
                    } else if (hovered) {
                        cairo_set_source_rgb(cr, 0.22, 0.24, 0.32);
                        draw_rounded_rect(cr, scale_pills[s].x, 301.5, scale_pills[s].w, 20.0, 4.0);
                        cairo_fill(cr);
                        cairo_set_source_rgb(cr, 0.38, 0.42, 0.55);
                        cairo_set_line_width(cr, 1.0);
                        draw_rounded_rect(cr, scale_pills[s].x, 301.5, scale_pills[s].w, 20.0, 4.0);
                        cairo_stroke(cr);
                        cairo_set_source_rgb(cr, 0.85, 0.88, 0.98);
                    } else {
                        cairo_set_source_rgb(cr, 0.14, 0.15, 0.20);
                        draw_rounded_rect(cr, scale_pills[s].x, 301.5, scale_pills[s].w, 20.0, 4.0);
                        cairo_fill(cr);
                        cairo_set_source_rgb(cr, 0.22, 0.24, 0.30);
                        cairo_set_line_width(cr, 1.0);
                        draw_rounded_rect(cr, scale_pills[s].x, 301.5, scale_pills[s].w, 20.0, 4.0);
                        cairo_stroke(cr);
                        cairo_set_source_rgb(cr, 0.55, 0.55, 0.65);
                    }
                    cairo_move_to(cr, scale_pills[s].x + 5.0, 316.0);
                    cairo_show_text(cr, scale_pills[s].label);
                }

                /* Line 5: Currency Rates Refresh (Y = 328 to 358, baseline 347.0) */
                set_font(cr, true);
                cairo_set_font_size(cr, 11.0 * font_scale);
                cairo_set_source_rgb(cr, 0.70, 0.70, 0.80);
                cairo_move_to(cr, 16.0, 347.0);
                cairo_show_text(cr, "Rates:");

                double rate_btn_x = 78.0, rate_btn_y = 332.5, rate_btn_w = 88.0, rate_btn_h = 20.0;
                bool rate_hovered = (g_hover_type == 5);
                if (currency_state.is_fetching) {
                    rate_btn_w = 98.0;
                    cairo_set_source_rgb(cr, 0.12, 0.20, 0.32);
                    draw_rounded_rect(cr, rate_btn_x, rate_btn_y, rate_btn_w, rate_btn_h, 4.0);
                    cairo_fill(cr);
                    cairo_set_source_rgb(cr, 0.29, 0.59, 0.98);
                    cairo_set_line_width(cr, 1.0);
                    draw_rounded_rect(cr, rate_btn_x, rate_btn_y, rate_btn_w, rate_btn_h, 4.0);
                    cairo_stroke(cr);
                    set_font(cr, true);
                    cairo_set_font_size(cr, 10.5 * font_scale);
                    cairo_set_source_rgb(cr, 0.40, 0.75, 1.0);
                    cairo_move_to(cr, rate_btn_x + 6.0, 347.0);
                    cairo_show_text(cr, "⟳ Fetching...");
                } else if (rate_hovered) {
                    cairo_set_source_rgb(cr, 0.22, 0.25, 0.34);
                    draw_rounded_rect(cr, rate_btn_x, rate_btn_y, rate_btn_w, rate_btn_h, 4.0);
                    cairo_fill(cr);
                    cairo_set_source_rgb(cr, 0.48, 0.64, 0.97);
                    cairo_set_line_width(cr, 1.0);
                    draw_rounded_rect(cr, rate_btn_x, rate_btn_y, rate_btn_w, rate_btn_h, 4.0);
                    cairo_stroke(cr);
                    set_font(cr, true);
                    cairo_set_font_size(cr, 10.5 * font_scale);
                    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
                    cairo_move_to(cr, rate_btn_x + 8.0, 347.0);
                    cairo_show_text(cr, "⟳ Refresh");
                } else {
                    cairo_set_source_rgb(cr, 0.16, 0.16, 0.20);
                    draw_rounded_rect(cr, rate_btn_x, rate_btn_y, rate_btn_w, rate_btn_h, 4.0);
                    cairo_fill(cr);
                    cairo_set_source_rgb(cr, 0.28, 0.28, 0.35);
                    cairo_set_line_width(cr, 1.0);
                    draw_rounded_rect(cr, rate_btn_x, rate_btn_y, rate_btn_w, rate_btn_h, 4.0);
                    cairo_stroke(cr);
                    set_font(cr, true);
                    cairo_set_font_size(cr, 10.5 * font_scale);
                    cairo_set_source_rgb(cr, 0.85, 0.85, 0.90);
                    cairo_move_to(cr, rate_btn_x + 8.0, 347.0);
                    cairo_show_text(cr, "⟳ Refresh");
                }

                /* Status reflection message beside button */
                set_font(cr, false);
                cairo_set_font_size(cr, 10.5 * font_scale);
                if (currency_state.status == CURRENCY_SUCCESS) {
                    cairo_set_source_rgb(cr, 0.20, 0.82, 0.48); /* Mint green */
                } else if (currency_state.status == CURRENCY_FETCHING) {
                    cairo_set_source_rgb(cr, 0.29, 0.59, 0.98); /* Accent blue */
                } else if (currency_state.status == CURRENCY_FAILED) {
                    cairo_set_source_rgb(cr, 0.95, 0.55, 0.40); /* Coral red/amber */
                } else {
                    cairo_set_source_rgb(cr, 0.55, 0.55, 0.65); /* Neutral gray */
                }
                cairo_move_to(cr, rate_btn_x + rate_btn_w + 12.0, 347.0);
                cairo_show_text(cr, currency_state.msg);

                /* Section 2 Divider at Y = 366 */
                cairo_set_source_rgb(cr, 0.20, 0.20, 0.26);
                cairo_set_line_width(cr, 1.0);
                cairo_move_to(cr, 12.0, 366.0);
                cairo_line_to(cr, win_w - 12.0, 366.0);
                cairo_stroke(cr);

                /* Section 2 Header at Y = 382 */
                set_font(cr, true);
                cairo_set_font_size(cr, 11.0 * font_scale);
                cairo_set_source_rgb(cr, 0.78, 0.62, 0.96); /* Soft lavender */
                cairo_move_to(cr, 16.0, 382.0);
                char sec2_hdr[64];
                CalcSymbol d_syms[MAX_SYMBOLS];
                int d_raw_cnt = get_all_symbols(d_syms, MAX_SYMBOLS);
                int d_cnt = 0;
                for (int i = 0; i < d_raw_cnt; i++) {
                    if (d_syms[i].is_persistent) d_cnt++;
                }
                snprintf(sec2_hdr, sizeof(sec2_hdr), "Saved Symbols (%d):", d_cnt);
                cairo_show_text(cr, sec2_hdr);

                set_font(cr, false);
                cairo_set_font_size(cr, 10.0 * (font_scale > 1.2 ? 1.2 : font_scale));
                cairo_set_source_rgb(cr, 0.45, 0.45, 0.55);
                cairo_move_to(cr, 142.0, 382.0);
                cairo_show_text(cr, "[Click chip to insert  •  ✕ to delete  •  Scroll to view more]");

                /* Clear All button in drawer header on right */
                double d_clear_w = 78.0, d_clear_h = 18.0;
                double d_clear_x = win_w - d_clear_w - 16.0, d_clear_y = 369.0;
                if (d_cnt > 0) {
                    g_drawer_clear_all_x = d_clear_x;
                    g_drawer_clear_all_y = d_clear_y;
                    g_drawer_clear_all_w = d_clear_w;
                    g_drawer_clear_all_h = d_clear_h;
                } else {
                    g_drawer_clear_all_w = 0.0;
                    g_drawer_clear_all_h = 0.0;
                }
                bool clear_hovered = (g_hover_type == 6);
                if (d_cnt > 0) {
                    if (clear_hovered) {
                        cairo_set_source_rgb(cr, 0.40, 0.14, 0.18);
                        draw_rounded_rect(cr, d_clear_x, d_clear_y, d_clear_w, d_clear_h, 4.0);
                        cairo_fill(cr);
                        cairo_set_source_rgb(cr, 0.85, 0.30, 0.35);
                        cairo_set_line_width(cr, 1.0);
                        draw_rounded_rect(cr, d_clear_x, d_clear_y, d_clear_w, d_clear_h, 4.0);
                        cairo_stroke(cr);
                        set_font(cr, true);
                        cairo_set_font_size(cr, 9.5 * font_scale);
                        cairo_set_source_rgb(cr, 1.0, 0.70, 0.75);
                    } else {
                        cairo_set_source_rgb(cr, 0.28, 0.12, 0.14);
                        draw_rounded_rect(cr, d_clear_x, d_clear_y, d_clear_w, d_clear_h, 4.0);
                        cairo_fill(cr);
                        cairo_set_source_rgb(cr, 0.58, 0.22, 0.25);
                        cairo_set_line_width(cr, 1.0);
                        draw_rounded_rect(cr, d_clear_x, d_clear_y, d_clear_w, d_clear_h, 4.0);
                        cairo_stroke(cr);
                        set_font(cr, true);
                        cairo_set_font_size(cr, 9.5 * font_scale);
                        cairo_set_source_rgb(cr, 0.95, 0.45, 0.48);
                    }
                    cairo_move_to(cr, d_clear_x + 6.0, d_clear_y + 13.0);
                    cairo_show_text(cr, "✕ Clear All");
                }

                g_drawer_chip_count = 0;
                if (d_cnt == 0) {
                    set_font(cr, false);
                    cairo_set_font_size(cr, 11.0 * font_scale);
                    cairo_set_source_rgb(cr, 0.45, 0.45, 0.55);
                    cairo_move_to(cr, 16.0, 416.0);
                    cairo_show_text(cr, "No saved symbols yet. Use := to save (e.g. tax := salary * 0.2 or f(x) := 2*x + 5)");
                    g_drawer_max_scroll = 0.0;
                    g_drawer_scroll_y = 0.0;
                } else {
                    double cur_cx = 16.0;
                    double cur_cy = 394.0;
                    double chip_h = 22.0;
                    double row_gap = 6.0;
                    double col_gap = 8.0;
                    double view_top = 390.0;
                    double view_bottom = win_h_expanded - 32.0;
                    double view_h = view_bottom - view_top;

                    /* First pass: calculate total layout height */
                    double calc_cx = 16.0, calc_cy = 394.0;
                    for (int i = 0; i < d_raw_cnt; i++) {
                        CalcSymbol *s = &d_syms[i];
                        if (!s->is_persistent) continue;
                        const char *badge_tag = "var";
                        char chip_label[128];
                        if (s->is_func) {
                            badge_tag = "fn";
                            char p_list[32] = "";
                            for (int p = 0; p < s->param_count; p++) {
                                if (p > 0) strncat(p_list, ",", sizeof(p_list) - strlen(p_list) - 1);
                                strncat(p_list, s->params[p], sizeof(p_list) - strlen(p_list) - 1);
                            }
                            snprintf(chip_label, sizeof(chip_label), "%.24s(%.16s) := %.48s", s->name, p_list, s->expr);
                        } else if (s->expr[0] != '\0') {
                            badge_tag = "fx";
                            double live_v = 0.0;
                            get_variable(s->name, &live_v);
                            char v_val_str[32];
                            if (s->is_date) format_date_result(live_v, v_val_str, sizeof(v_val_str));
                            else if (s->is_time) format_time_result(live_v, false, v_val_str, sizeof(v_val_str));
                            else if (s->unit[0] && format_compound_unit(live_v, s->unit, v_val_str, sizeof(v_val_str))) {}
                            else format_result_ex(live_v, comma_fmt, v_val_str, sizeof(v_val_str));
                            snprintf(chip_label, sizeof(chip_label), "%.24s := %.32s (%.24s)", s->name, s->expr, v_val_str);
                        } else {
                            char v_val_str[32];
                            if (s->is_date) {
                                badge_tag = "date";
                                format_date_result(s->cached_val, v_val_str, sizeof(v_val_str));
                            } else if (s->is_time) {
                                badge_tag = "time";
                                format_time_result(s->cached_val, false, v_val_str, sizeof(v_val_str));
                            } else if (s->unit[0] && format_compound_unit(s->cached_val, s->unit, v_val_str, sizeof(v_val_str))) {
                                badge_tag = "unit";
                            } else {
                                format_result_ex(s->cached_val, comma_fmt, v_val_str, sizeof(v_val_str));
                            }
                            snprintf(chip_label, sizeof(chip_label), "%.24s := %.24s", s->name, v_val_str);
                        }

                        set_font(cr, true);
                        cairo_set_font_size(cr, 9.0 * font_scale);
                        cairo_text_extents_t ext_badge;
                        cairo_text_extents(cr, badge_tag, &ext_badge);
                        double badge_w = ext_badge.width + 8.0;

                        set_font(cr, false);
                        cairo_set_font_size(cr, 10.5 * font_scale);
                        cairo_text_extents_t ext_lbl;
                        cairo_text_extents(cr, chip_label, &ext_lbl);
                        double lbl_w = ext_lbl.width;
                        double chip_w = 6.0 + badge_w + 6.0 + lbl_w + 6.0 + 16.0 + 4.0;

                        if (calc_cx + chip_w > win_w - 20.0 && calc_cx > 16.0) {
                            calc_cx = 16.0;
                            calc_cy += chip_h + row_gap;
                        }
                        calc_cx += chip_w + col_gap;
                    }

                    double total_content_h = (calc_cy + chip_h) - view_top;
                    g_drawer_max_scroll = fmax(0.0, total_content_h - view_h);
                    if (g_drawer_scroll_y > g_drawer_max_scroll) g_drawer_scroll_y = g_drawer_max_scroll;
                    if (g_drawer_scroll_y < 0.0) g_drawer_scroll_y = 0.0;

                    /* Second pass: Render chips with clipping and scrolling */
                    cairo_save(cr);
                    cairo_rectangle(cr, 12.0, view_top, win_w - 24.0, view_h);
                    cairo_clip(cr);

                    for (int i = 0; i < d_raw_cnt && g_drawer_chip_count < MAX_SYMBOLS; i++) {
                        CalcSymbol *s = &d_syms[i];
                        if (!s->is_persistent) continue;
                        const char *badge_tag = "var";
                        double badge_r = 0.20, badge_g = 0.82, badge_b = 0.48; // mint green

                        char chip_label[128];
                        if (s->is_func) {
                            badge_tag = "fn";
                            badge_r = 0.30; badge_g = 0.75; badge_b = 0.95; // cyan
                            char p_list[32] = "";
                            for (int p = 0; p < s->param_count; p++) {
                                if (p > 0) strncat(p_list, ",", sizeof(p_list) - strlen(p_list) - 1);
                                strncat(p_list, s->params[p], sizeof(p_list) - strlen(p_list) - 1);
                            }
                            snprintf(chip_label, sizeof(chip_label), "%.24s(%.16s) := %.48s", s->name, p_list, s->expr);
                        } else if (s->expr[0] != '\0') {
                            badge_tag = "fx";
                            badge_r = 0.78; badge_g = 0.60; badge_b = 0.98; // lavender
                            double live_v = 0.0;
                            get_variable(s->name, &live_v);
                            char v_val_str[32];
                            if (s->is_date) format_date_result(live_v, v_val_str, sizeof(v_val_str));
                            else if (s->is_time) format_time_result(live_v, false, v_val_str, sizeof(v_val_str));
                            else format_result_ex(live_v, comma_fmt, v_val_str, sizeof(v_val_str));
                            snprintf(chip_label, sizeof(chip_label), "%.24s := %.32s (%.24s)", s->name, s->expr, v_val_str);
                        } else {
                            char v_val_str[32];
                            if (s->is_date) {
                                badge_tag = "date";
                                badge_r = 0.95; badge_g = 0.65; badge_b = 0.30; // amber
                                format_date_result(s->cached_val, v_val_str, sizeof(v_val_str));
                            } else if (s->is_time) {
                                badge_tag = "time";
                                badge_r = 0.95; badge_g = 0.65; badge_b = 0.30; // amber
                                format_time_result(s->cached_val, false, v_val_str, sizeof(v_val_str));
                            } else {
                                format_result_ex(s->cached_val, comma_fmt, v_val_str, sizeof(v_val_str));
                            }
                            snprintf(chip_label, sizeof(chip_label), "%.24s := %.24s", s->name, v_val_str);
                        }

                        /* Measure badge width and label width */
                        set_font(cr, true);
                        cairo_set_font_size(cr, 9.0 * font_scale);
                        cairo_text_extents_t ext_badge;
                        cairo_text_extents(cr, badge_tag, &ext_badge);
                        double badge_w = ext_badge.width + 8.0;

                        set_font(cr, false);
                        cairo_set_font_size(cr, 10.5 * font_scale);
                        cairo_text_extents_t ext_lbl;
                        cairo_text_extents(cr, chip_label, &ext_lbl);
                        double lbl_w = ext_lbl.width;

                        double del_btn_w = 16.0;
                        double chip_w = 6.0 + badge_w + 6.0 + lbl_w + 6.0 + del_btn_w + 4.0;

                        if (cur_cx + chip_w > win_w - 20.0 && cur_cx > 16.0) {
                            cur_cx = 16.0;
                            cur_cy += chip_h + row_gap;
                        }

                        double draw_cy = cur_cy - g_drawer_scroll_y;
                        double del_x = cur_cx + chip_w - del_btn_w - 2.0;

                        /* Check if chip is in visible window area */
                        if (draw_cy + chip_h >= view_top && draw_cy <= view_bottom) {
                            int chip_idx = g_drawer_chip_count;
                            bool is_chip_hovered = (g_hover_type == 7 && g_hover_idx == chip_idx);

                            /* Draw Chip Container */
                            if (is_chip_hovered) {
                                if (g_hover_del) {
                                    cairo_set_source_rgb(cr, 0.22, 0.14, 0.18);
                                    draw_rounded_rect(cr, cur_cx, draw_cy, chip_w, chip_h, 4.0);
                                    cairo_fill(cr);
                                    cairo_set_source_rgb(cr, 0.65, 0.25, 0.30);
                                } else {
                                    cairo_set_source_rgb(cr, 0.18, 0.22, 0.32);
                                    draw_rounded_rect(cr, cur_cx, draw_cy, chip_w, chip_h, 4.0);
                                    cairo_fill(cr);
                                    cairo_set_source_rgb(cr, 0.48, 0.64, 0.97);
                                }
                            } else {
                                cairo_set_source_rgb(cr, 0.15, 0.16, 0.22);
                                draw_rounded_rect(cr, cur_cx, draw_cy, chip_w, chip_h, 4.0);
                                cairo_fill(cr);
                                cairo_set_source_rgb(cr, 0.28, 0.30, 0.40);
                            }
                            cairo_set_line_width(cr, 1.0);
                            draw_rounded_rect(cr, cur_cx, draw_cy, chip_w, chip_h, 4.0);
                            cairo_stroke(cr);

                            /* Draw Badge */
                            double b_x = cur_cx + 4.0, b_y = draw_cy + 3.0, b_h = chip_h - 6.0;
                            cairo_set_source_rgba(cr, badge_r, badge_g, badge_b, 0.20);
                            draw_rounded_rect(cr, b_x, b_y, badge_w, b_h, 3.0);
                            cairo_fill(cr);
                            set_font(cr, true);
                            cairo_set_font_size(cr, 8.5 * font_scale);
                            cairo_set_source_rgb(cr, badge_r, badge_g, badge_b);
                            cairo_move_to(cr, b_x + 4.0, b_y + b_h - 3.5);
                            cairo_show_text(cr, badge_tag);

                            /* Draw Chip Label */
                            double l_x = b_x + badge_w + 5.0;
                            set_font(cr, false);
                            cairo_set_font_size(cr, 10.5 * font_scale);
                            if (is_chip_hovered && !g_hover_del) {
                                cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
                            } else {
                                cairo_set_source_rgb(cr, 0.88, 0.88, 0.94);
                            }
                            cairo_move_to(cr, l_x, draw_cy + chip_h - 6.0);
                            cairo_show_text(cr, chip_label);

                            /* Draw Chip '✕' Delete Icon */
                            if (is_chip_hovered && g_hover_del) {
                                cairo_set_source_rgb(cr, 0.48, 0.16, 0.20);
                                draw_rounded_rect(cr, del_x - 3.0, draw_cy + 2.0, del_btn_w + 4.0, chip_h - 4.0, 3.0);
                                cairo_fill(cr);
                                cairo_set_source_rgb(cr, 1.0, 0.70, 0.75);
                            } else {
                                cairo_set_source_rgb(cr, 0.85, 0.40, 0.45);
                            }
                            set_font(cr, true);
                            cairo_set_font_size(cr, 9.5 * font_scale);
                            cairo_move_to(cr, del_x + 3.5, draw_cy + chip_h - 6.5);
                            cairo_show_text(cr, "✕");

                            /* Record click hit area */
                            DrawerChipHit *hit = &g_drawer_chips[g_drawer_chip_count++];
                            hit->x = cur_cx;
                            hit->y = draw_cy;
                            hit->w = chip_w;
                            hit->h = chip_h;
                            hit->del_x = del_x - 2.0;
                            hit->del_w = del_btn_w + 4.0;
                            snprintf(hit->name, sizeof(hit->name), "%s", s->name);
                            hit->is_func = s->is_func;
                        }

                        cur_cx += chip_w + col_gap;
                    }
                    cairo_restore(cr);

                    /* Draw subtle scrollbar on the right if content overflows */
                    if (g_drawer_max_scroll > 0.0) {
                        double sb_w = 4.0;
                        double sb_x = win_w - sb_w - 6.0;
                        double sb_track_y = view_top + 4.0;
                        double sb_track_h = view_h - 8.0;
                        double sb_thumb_h = fmax(20.0, sb_track_h * (view_h / (view_h + g_drawer_max_scroll)));
                        double sb_thumb_y = sb_track_y + (g_drawer_scroll_y / g_drawer_max_scroll) * (sb_track_h - sb_thumb_h);

                        /* Track */
                        cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.05);
                        draw_rounded_rect(cr, sb_x, sb_track_y, sb_w, sb_track_h, 2.0);
                        cairo_fill(cr);

                        /* Thumb */
                        cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.30);
                        draw_rounded_rect(cr, sb_x, sb_thumb_y, sb_w, sb_thumb_h, 2.0);
                        cairo_fill(cr);
                    }
                }

                /* Bottom Footer: Complete Config Path (for visibility) */
                cairo_set_source_rgb(cr, 0.20, 0.20, 0.26);
                cairo_set_line_width(cr, 1.0);
                cairo_move_to(cr, 12.0, win_h_expanded - 26.0);
                cairo_line_to(cr, win_w - 12.0, win_h_expanded - 26.0);
                cairo_stroke(cr);

                char cfg_path[512];
                get_elephant_file_path("settings.conf", cfg_path, sizeof(cfg_path));
                char cfg_label[600];
                snprintf(cfg_label, sizeof(cfg_label), "Config: %s", cfg_path);

                set_font(cr, false);
                cairo_set_font_size(cr, 9.5 * (font_scale > 1.2 ? 1.2 : font_scale));
                cairo_set_source_rgb(cr, 0.45, 0.48, 0.58);
                cairo_move_to(cr, 16.0, win_h_expanded - 9.0);
                cairo_show_text(cr, cfg_label);
            }

            /* Blit buffer to X11 window */
            double t_after_all_drawing = get_time_ms();
            cairo_t *cr_win = cairo_create(xlib_surf);
            cairo_set_source_surface(cr_win, buf_surf, 0, 0);
            cairo_paint(cr_win);
            cairo_destroy(cr_win);
            double t_after_blit = get_time_ms();
            XFlush(dpy);
            double t_after_flush = get_time_ms();

            if (benchmark_mode) {
                double t_end = get_time_ms();
                printf("Startup to first frame: %.2f ms\n", t_end - t_start);
                printf("  [Phase breakdown]\n");
                printf("  - Storage & history load:  %.2f ms\n", t_before_xopen - t_after_load);
                printf("  - XOpenDisplay:            %.2f ms\n", t_after_xopen - t_before_xopen);
                printf("  - Window create & setup:   %.2f ms\n", t_before_icon - t_after_xopen);
                printf("  - Icon render & attach:    %.2f ms\n", t_after_icon - t_before_icon);
                printf("  - Window map & flush:      %.2f ms\n", t_after_map - t_before_map);
                printf("  - Cairo surf/cr setup:     %.2f ms\n", t_after_cairo - t_before_cairo);
                printf("  - Wait for 1st X event:    %.2f ms\n", t_first_event - t_loop_start);
                printf("  - Wait 1st event -> Expose:%.2f ms (events skipped: %d)\n", t_expose_start - t_first_event, event_count_before_expose);
                printf("  - First frame render total:%.2f ms\n", t_end - t_expose_start);
                printf("      * Bg & border:         %.2f ms\n", t_before_first_text - t_expose_start);
                printf("      * 1st font/text init:  %.2f ms\n", t_after_first_text - t_before_first_text);
                printf("      * Rest of UI drawing:  %.2f ms\n", t_after_all_drawing - t_after_first_text);
                printf("      * Buffer blit to X11:  %.2f ms\n", t_after_blit - t_after_all_drawing);
                printf("      * Final XFlush:        %.2f ms\n", t_after_flush - t_after_blit);
                running = false;
                break;
            }
        } else if (ev.type == ButtonPress) {
            just_evaluated_constant = false;
            if (retro_state.active) retro_paren_cancel(&retro_state);
            int mx = ev.xbutton.x;
            int my = ev.xbutton.y;
            unsigned int btn = ev.xbutton.button;
            if (drawer_open && (btn == 4 || btn == 5)) {
                if (btn == 4) g_drawer_scroll_y = fmax(0.0, g_drawer_scroll_y - 28.0);
                else if (btn == 5) g_drawer_scroll_y = fmin(g_drawer_max_scroll, g_drawer_scroll_y + 28.0);
                g_hover_type = 0;
                g_hover_idx = -1;
                g_hover_del = false;
                XClearArea(dpy, win, 0, 0, 0, 0, True);
                continue;
            }
            if (btn != 1) continue;
            if (my >= 42 && my <= 82) {
                /* Click in Row 2 (Green evaluated result): Copy result */
                if (has_valid_result && result_buf[0]) {
                    copy_to_clipboard(dpy, win, result_buf);
                }
            } else if (my >= 82 && my <= 110) {
                /* Click in Row 3 (Variables row): Check Delete All button */
                CalcSymbol del_syms[MAX_SYMBOLS];
                int del_sym_cnt = get_all_symbols(del_syms, MAX_SYMBOLS);
                int session_del_cnt = 0;
                for (int d = 0; d < del_sym_cnt; d++) {
                    if (!del_syms[d].is_persistent) session_del_cnt++;
                }
                double del_btn_w = 84.0;
                double del_btn_x = win_w - del_btn_w - 20.0;
                if (session_del_cnt > 0 && mx >= del_btn_x - 4.0 && mx <= del_btn_x + del_btn_w + 4.0 && my >= 84 && my <= 108) {
                    for (int d = 0; d < del_sym_cnt; d++) {
                        if (!del_syms[d].is_persistent) {
                            delete_symbol(del_syms[d].name);
                        }
                    }
                    if (input_len > 0) {
                        const char *err = NULL;
                        double res_val = 0.0;
                        bool is_date = false;
                        bool is_time = false;
                        current_unit_sym[0] = '\0';
                        current_words_sfx[0] = '\0';
                        if (evaluate_equation_ex(input_buf, ans_val, &res_val, current_unit_sym, sizeof(current_unit_sym), &is_date, &is_time, &err)) {
                            has_valid_result = true;
                            current_evaluated_val = res_val;
                            current_is_conv = (current_unit_sym[0] != '\0');
                            current_is_date = is_date;
                            current_is_time = is_time;
                            if (is_time) {
                                format_time_result(res_val, false, result_buf, sizeof(result_buf));
                            } else if (is_date) {
                                if (current_unit_sym[0] && is_valid_timezone_query(current_unit_sym)) {
                                    TimezoneResult tz_res;
                                    if (convert_to_timezone((time_t)round(res_val), current_unit_sym, &tz_res)) {
                                        snprintf(result_buf, sizeof(result_buf), "%.96s %.16s", tz_res.time_str, tz_res.abbrev);
                                    } else {
                                        format_date_result(res_val, result_buf, sizeof(result_buf));
                                    }
                                } else {
                                    format_date_result(res_val, result_buf, sizeof(result_buf));
                                }
                            } else {
                                char comp_unit_buf[128];
                                if (current_unit_sym[0] && format_compound_unit(res_val, current_unit_sym, comp_unit_buf, sizeof(comp_unit_buf))) {
                                    snprintf(result_buf, sizeof(result_buf), "%s", comp_unit_buf);
                                } else {
                                    format_result(res_val, result_buf, sizeof(result_buf));
                                    if (current_unit_sym[0]) {
                                        strncat(result_buf, " ", sizeof(result_buf) - strlen(result_buf) - 1);
                                        strncat(result_buf, current_unit_sym, sizeof(result_buf) - strlen(result_buf) - 1);
                                    }
                                }
                            }
                            format_computing_row(res_val, output_mode, current_unit_sym[0] ? current_unit_sym : NULL, NULL, is_date, is_time, comp_buf, sizeof(comp_buf));
                            format_subtitle_preview(res_val, current_unit_sym[0] ? current_unit_sym : NULL, NULL, is_date, is_time, words_buf, sizeof(words_buf));
                        } else {
                            has_valid_result = false;
                            result_buf[0] = '\0';
                            comp_buf[0] = '\0';
                            words_buf[0] = '\0';
                        }
                    }
                    XClearArea(dpy, win, 0, 0, 0, 0, True);
                }
            } else if (my >= 110 && my <= 138 && mx >= 20) {
                /* Click in Row 4 (Words row): Start mouse drag selection */
                if (has_valid_result && words_buf[0]) {
                    words_sel_start = find_words_char_index(cr, words_buf, mx - 68.0, font_scale);
                    words_sel_end = words_sel_start;
                    words_dragging = true;
                    XClearArea(dpy, win, 0, 0, 0, 0, True);
                }
            } else if (my >= 138 && my <= 168 && mx >= 20) {
                /* Click in Row 5 (Computing row): Copy computing representation */
                if (has_valid_result && comp_buf[0]) {
                    copy_to_clipboard(dpy, win, comp_buf);
                }
            } else if (my >= 168 && my <= 204 && mx >= win_w - 55) {
                /* Click Gear Settings button on footer */
                drawer_open = !drawer_open;
                if (!drawer_open) {
                    g_drawer_scroll_y = 0.0;
                    g_hover_type = 0;
                    g_hover_idx = -1;
                    g_hover_del = false;
                }
                cur_h = drawer_open ? win_h_expanded : win_h_collapsed;
                XResizeWindow(dpy, win, win_w, cur_h);
                XSetWindowBackgroundPixmap(dpy, win, None);
                cairo_destroy(cr);
                cairo_surface_destroy(buf_surf);
                cairo_surface_destroy(xlib_surf);
                xlib_surf = cairo_xlib_surface_create(dpy, win, DefaultVisual(dpy, screen), win_w, cur_h);
                buf_surf = cairo_image_surface_create(CAIRO_FORMAT_RGB24, win_w, cur_h);
                cr = cairo_create(buf_surf);
                XClearArea(dpy, win, 0, 0, 0, 0, True);
            } else if (drawer_open && my >= 204) {
                if (my < 237) {
                    /* Line 1: Format pills */
                    if (mx >= 78 && mx <= 202) {
                        comma_fmt = FMT_INTL;
                    } else if (mx >= 208 && mx <= 344) {
                        comma_fmt = FMT_INDIAN;
                    } else if (mx >= 350 && mx <= 468) {
                        comma_fmt = FMT_PLAIN;
                    }
                    save_settings(comma_fmt, decimal_places, font_scale, output_mode);
                    reformat_buffer_numbers(input_buf, sizeof(input_buf), &cursor_pos, &input_len);
                } else if (my >= 237 && my < 267) {
                    /* Line 2: Decimal pills */
                    if (mx >= 78 && mx <= 116) decimal_places = -1;
                    else if (mx >= 122 && mx <= 146) decimal_places = 0;
                    else if (mx >= 152 && mx <= 176) decimal_places = 1;
                    else if (mx >= 182 && mx <= 206) decimal_places = 2;
                    else if (mx >= 212 && mx <= 236) decimal_places = 3;
                    else if (mx >= 242 && mx <= 266) decimal_places = 4;
                    else if (mx >= 272 && mx <= 296) decimal_places = 6;
                    else if (mx >= 302 && mx <= 326) decimal_places = 8;
                    save_settings(comma_fmt, decimal_places, font_scale, output_mode);
                } else if (my >= 267 && my < 297) {
                    /* Line 3: Output mode pills */
                    if (mx >= 78 && mx <= 156) output_mode = OUTPUT_COMP_ONLY;
                    else if (mx >= 162 && mx <= 204) output_mode = OUTPUT_ALL;
                    else if (mx >= 210 && mx <= 268) output_mode = OUTPUT_SIMPLE;
                    save_settings(comma_fmt, decimal_places, font_scale, output_mode);
                } else if (my >= 297 && my <= 332) {
                    /* Line 4: Scale preset pills */
                    if (mx >= 78 && mx <= 114) font_scale = 0.8;
                    else if (mx >= 120 && mx <= 162) font_scale = 1.0;
                    else if (mx >= 168 && mx <= 210) font_scale = 1.2;
                    else if (mx >= 216 && mx <= 258) font_scale = 1.4;
                    else if (mx >= 264 && mx <= 306) font_scale = 1.6;
                    else if (mx >= 312 && mx <= 354) font_scale = 1.8;
                    save_settings(comma_fmt, decimal_places, font_scale, output_mode);
                } else if (my >= 332 && my < 366) {
                    /* Line 5: Currency refresh button (X: 78 to 176) */
                    if (mx >= 78 && mx <= 176) {
                        currency_refresh_async(&currency_state, win);
                        XClearArea(dpy, win, 0, 0, 0, 0, True);
                    }
                } else if (my >= 366) {
                    /* Section 2: Saved Symbols & Functions */
                    if (mx >= g_drawer_clear_all_x && mx <= g_drawer_clear_all_x + g_drawer_clear_all_w &&
                        my >= g_drawer_clear_all_y && my <= g_drawer_clear_all_y + g_drawer_clear_all_h) {
                        CalcSymbol cur_syms[MAX_SYMBOLS];
                        int cur_cnt = get_all_symbols(cur_syms, MAX_SYMBOLS);
                        for (int d = 0; d < cur_cnt; d++) {
                            if (cur_syms[d].is_persistent) {
                                delete_symbol(cur_syms[d].name);
                            }
                        }
                        save_saved_symbols();
                        goto perform_live_eval;
                    }

                    for (int c = 0; c < g_drawer_chip_count; c++) {
                        DrawerChipHit *hit = &g_drawer_chips[c];
                        if (mx >= hit->x && mx <= hit->x + hit->w && my >= hit->y && my <= hit->y + hit->h) {
                            if (mx >= hit->del_x) {
                                /* Clicked ✕ on chip: delete symbol */
                                delete_symbol(hit->name);
                                save_saved_symbols();
                            } else {
                                /* Clicked chip body: insert symbol into input equation */
                                char ins[48];
                                if (hit->is_func) {
                                    snprintf(ins, sizeof(ins), "%s()", hit->name);
                                } else {
                                    snprintf(ins, sizeof(ins), "%s", hit->name);
                                }
                                size_t ins_len = strlen(ins);
                                if (input_len + ins_len < sizeof(input_buf) - 1) {
                                    memmove(&input_buf[cursor_pos + ins_len], &input_buf[cursor_pos], input_len - cursor_pos + 1);
                                    memcpy(&input_buf[cursor_pos], ins, ins_len);
                                    input_len += ins_len;
                                    cursor_pos += hit->is_func ? (ins_len - 1) : ins_len;
                                    undo_stack_push(&undo_stack, input_buf, cursor_pos, UNDO_ACTION_OTHER);
                                }
                            }
                            goto perform_live_eval;
                        }
                    }
                }
                if (has_valid_result) {
                    char comp_unit_buf[128];
                    if (current_unit_sym[0] && format_compound_unit(current_evaluated_val, current_unit_sym, comp_unit_buf, sizeof(comp_unit_buf))) {
                        snprintf(result_buf, sizeof(result_buf), "%s", comp_unit_buf);
                    } else {
                        format_result(current_evaluated_val, result_buf, sizeof(result_buf));
                        if (current_unit_sym[0]) {
                            strncat(result_buf, " ", sizeof(result_buf) - strlen(result_buf) - 1);
                            strncat(result_buf, current_unit_sym, sizeof(result_buf) - strlen(result_buf) - 1);
                        }
                    }
                    format_computing_row(current_evaluated_val, output_mode,
                                         current_is_conv ? current_unit_sym : NULL,
                                         current_is_conv ? current_words_sfx : NULL,
                                         current_is_date, current_is_time,
                                         comp_buf, sizeof(comp_buf));
                    format_subtitle_preview(current_evaluated_val,
                                            current_is_conv ? current_unit_sym : NULL,
                                            current_is_conv ? current_words_sfx : NULL,
                                            current_is_date, current_is_time,
                                            words_buf, sizeof(words_buf));
                }
                XClearArea(dpy, win, 0, 0, 0, 0, True);
            } else {
                /* Click elsewhere deselects words */
                if (words_sel_start >= 0 || words_sel_end >= 0) {
                    words_sel_start = -1;
                    words_sel_end = -1;
                    XClearArea(dpy, win, 0, 0, 0, 0, True);
                }
            }
        } else if (ev.type == LeaveNotify) {
            XEvent discard_motion;
            while (XCheckTypedWindowEvent(dpy, win, MotionNotify, &discard_motion));
            if (g_hover_type != 0) {
                g_hover_type = 0;
                g_hover_idx = -1;
                g_hover_del = false;
                XClearArea(dpy, win, 0, 0, 0, 0, True);
            }
        } else if (ev.type == MotionNotify) {
            XEvent next_motion;
            while (XCheckTypedWindowEvent(dpy, win, MotionNotify, &next_motion)) {
                ev = next_motion;
            }
            int mx = ev.xmotion.x;
            int my = ev.xmotion.y;
            if (words_dragging && my >= 110 && my <= 138) {
                int idx = find_words_char_index(cr, words_buf, mx - 68.0, font_scale);
                if (idx != words_sel_end) {
                    words_sel_end = idx;
                    XClearArea(dpy, win, 0, 0, 0, 0, True);
                }
            }

            int new_hover_type = 0;
            int new_hover_idx = -1;
            bool new_hover_del = false;

            /* Check gear button */
            if (my >= 168 && my <= 204 && mx >= win_w - 55) {
                new_hover_type = 8;
            } else if (drawer_open && my >= 204) {
                if (my < 237) {
                    /* Line 1: Format pills */
                    if (mx >= 78 && mx <= 202) { new_hover_type = 1; new_hover_idx = 0; }
                    else if (mx >= 208 && mx <= 344) { new_hover_type = 1; new_hover_idx = 1; }
                    else if (mx >= 350 && mx <= 468) { new_hover_type = 1; new_hover_idx = 2; }
                } else if (my >= 237 && my < 267) {
                    /* Line 2: Decimal pills */
                    if (mx >= 78 && mx <= 116) { new_hover_type = 2; new_hover_idx = 0; }
                    else if (mx >= 122 && mx <= 146) { new_hover_type = 2; new_hover_idx = 1; }
                    else if (mx >= 152 && mx <= 176) { new_hover_type = 2; new_hover_idx = 2; }
                    else if (mx >= 182 && mx <= 206) { new_hover_type = 2; new_hover_idx = 3; }
                    else if (mx >= 212 && mx <= 236) { new_hover_type = 2; new_hover_idx = 4; }
                    else if (mx >= 242 && mx <= 266) { new_hover_type = 2; new_hover_idx = 5; }
                    else if (mx >= 272 && mx <= 296) { new_hover_type = 2; new_hover_idx = 6; }
                    else if (mx >= 302 && mx <= 326) { new_hover_type = 2; new_hover_idx = 7; }
                } else if (my >= 267 && my < 297) {
                    /* Line 3: Output mode pills */
                    if (mx >= 78 && mx <= 156) { new_hover_type = 3; new_hover_idx = 0; }
                    else if (mx >= 162 && mx <= 204) { new_hover_type = 3; new_hover_idx = 1; }
                    else if (mx >= 210 && mx <= 268) { new_hover_type = 3; new_hover_idx = 2; }
                } else if (my >= 297 && my <= 332) {
                    /* Line 4: Scale preset pills */
                    if (mx >= 78 && mx <= 114) { new_hover_type = 4; new_hover_idx = 0; }
                    else if (mx >= 120 && mx <= 162) { new_hover_type = 4; new_hover_idx = 1; }
                    else if (mx >= 168 && mx <= 210) { new_hover_type = 4; new_hover_idx = 2; }
                    else if (mx >= 216 && mx <= 258) { new_hover_type = 4; new_hover_idx = 3; }
                    else if (mx >= 264 && mx <= 306) { new_hover_type = 4; new_hover_idx = 4; }
                    else if (mx >= 312 && mx <= 354) { new_hover_type = 4; new_hover_idx = 5; }
                } else if (my >= 332 && my < 366) {
                    /* Line 5: Currency refresh button */
                    if (mx >= 78 && mx <= 176) { new_hover_type = 5; }
                } else if (my >= 366) {
                    /* Section 2: Saved Symbols & Functions */
                    if (g_drawer_clear_all_w > 0.0 &&
                        mx >= g_drawer_clear_all_x && mx <= g_drawer_clear_all_x + g_drawer_clear_all_w &&
                        my >= g_drawer_clear_all_y && my <= g_drawer_clear_all_y + g_drawer_clear_all_h) {
                        new_hover_type = 6;
                    } else {
                        for (int c = 0; c < g_drawer_chip_count; c++) {
                            DrawerChipHit *hit = &g_drawer_chips[c];
                            if (mx >= hit->x && mx <= hit->x + hit->w && my >= hit->y && my <= hit->y + hit->h) {
                                new_hover_type = 7;
                                new_hover_idx = c;
                                if (mx >= hit->del_x) new_hover_del = true;
                                break;
                            }
                        }
                    }
                }
            }

            if (new_hover_type != g_hover_type || new_hover_idx != g_hover_idx || new_hover_del != g_hover_del) {
                g_hover_type = new_hover_type;
                g_hover_idx = new_hover_idx;
                g_hover_del = new_hover_del;
                XClearArea(dpy, win, 0, 0, 0, 0, True);
            }
        } else if (ev.type == ButtonRelease) {
            if (words_dragging) {
                words_dragging = false;
                if (words_sel_start >= 0 && words_sel_end >= 0 && words_sel_start != words_sel_end) {
                    int s = words_sel_start < words_sel_end ? words_sel_start : words_sel_end;
                    int e = words_sel_start < words_sel_end ? words_sel_end : words_sel_start;
                    char sel_text[512];
                    int slen = e - s;
                    if (slen > (int)sizeof(sel_text) - 1) slen = (int)sizeof(sel_text) - 1;
                    memcpy(sel_text, &words_buf[s], slen);
                    sel_text[slen] = '\0';
                    copy_to_clipboard(dpy, win, sel_text);
                } else {
                    words_sel_start = -1;
                    words_sel_end = -1;
                    XClearArea(dpy, win, 0, 0, 0, 0, True);
                }
            }
        } else if (ev.type == KeyPress) {
            char keybuf[32];
            KeySym ksym;
            int len = XLookupString(&ev.xkey, keybuf, sizeof(keybuf) - 1, &ksym, NULL);
            keybuf[len] = '\0';

            if ((ev.xkey.state & ControlMask) && (ksym == XK_c || ksym == XK_C)) {
                if (words_sel_start >= 0 && words_sel_end >= 0 && words_sel_start != words_sel_end) {
                    int s = words_sel_start < words_sel_end ? words_sel_start : words_sel_end;
                    int e = words_sel_start < words_sel_end ? words_sel_end : words_sel_start;
                    char sel_text[512];
                    int slen = e - s;
                    if (slen > (int)sizeof(sel_text) - 1) slen = (int)sizeof(sel_text) - 1;
                    memcpy(sel_text, &words_buf[s], slen);
                    sel_text[slen] = '\0';
                    copy_to_clipboard(dpy, win, sel_text);
                } else if (has_valid_result && result_buf[0]) {
                    copy_to_clipboard(dpy, win, result_buf);
                }
                continue;
            }

            if ((ev.xkey.state & ControlMask) && !(ev.xkey.state & ShiftMask) && (ksym == XK_z || ksym == XK_Z)) {
                if (retro_state.active) retro_paren_cancel(&retro_state);
                if (undo_stack_undo(&undo_stack, input_buf, sizeof(input_buf), &cursor_pos)) {
                    input_len = strlen(input_buf);
                }
                words_sel_start = -1;
                words_sel_end = -1;
                goto perform_live_eval;
            } else if (((ev.xkey.state & ControlMask) && (ksym == XK_y || ksym == XK_Y)) ||
                       ((ev.xkey.state & ControlMask) && (ev.xkey.state & ShiftMask) && (ksym == XK_z || ksym == XK_Z))) {
                if (retro_state.active) retro_paren_cancel(&retro_state);
                if (undo_stack_redo(&undo_stack, input_buf, sizeof(input_buf), &cursor_pos)) {
                    input_len = strlen(input_buf);
                }
                words_sel_start = -1;
                words_sel_end = -1;
                goto perform_live_eval;
            }

            if ((ev.xkey.state & ControlMask) && (ksym == XK_equal || ksym == XK_plus || ksym == XK_KP_Add)) {
                font_scale += 0.1;
                if (font_scale > 1.8) font_scale = 1.8;
                save_settings(comma_fmt, decimal_places, font_scale, output_mode);
                XClearArea(dpy, win, 0, 0, 0, 0, True);
                continue;
            } else if ((ev.xkey.state & ControlMask) && (ksym == XK_minus || ksym == XK_underscore || ksym == XK_KP_Subtract)) {
                font_scale -= 0.1;
                if (font_scale < 0.8) font_scale = 0.8;
                save_settings(comma_fmt, decimal_places, font_scale, output_mode);
                XClearArea(dpy, win, 0, 0, 0, 0, True);
                continue;
            } else if ((ev.xkey.state & ControlMask) && (ksym == XK_0 || ksym == XK_KP_0)) {
                font_scale = 1.0;
                save_settings(comma_fmt, decimal_places, font_scale, output_mode);
                XClearArea(dpy, win, 0, 0, 0, 0, True);
                continue;
            } else if ((ev.xkey.state & ControlMask) && (ksym == XK_a || ksym == XK_A)) {
                if (retro_state.active) retro_paren_cancel(&retro_state);
                cursor_pos = 0;
                words_sel_start = -1;
                words_sel_end = -1;
                XClearArea(dpy, win, 0, 0, 0, 0, True);
                continue;
            } else if ((ev.xkey.state & ControlMask) && (ksym == XK_e || ksym == XK_E)) {
                if (retro_state.active) retro_paren_cancel(&retro_state);
                cursor_pos = input_len;
                words_sel_start = -1;
                words_sel_end = -1;
                XClearArea(dpy, win, 0, 0, 0, 0, True);
                continue;
            } else if ((ev.xkey.state & ControlMask) && (ksym == XK_b || ksym == XK_B)) {
                if (retro_state.active) retro_paren_cancel(&retro_state);
                if (cursor_pos > 0) cursor_pos--;
                words_sel_start = -1;
                words_sel_end = -1;
                XClearArea(dpy, win, 0, 0, 0, 0, True);
                continue;
            } else if ((ev.xkey.state & ControlMask) && (ksym == XK_f || ksym == XK_F)) {
                if (retro_state.active) retro_paren_cancel(&retro_state);
                if (cursor_pos < input_len) cursor_pos++;
                words_sel_start = -1;
                words_sel_end = -1;
                XClearArea(dpy, win, 0, 0, 0, 0, True);
                continue;
            } else if ((ev.xkey.state & Mod1Mask) && (ksym == XK_b || ksym == XK_B)) {
                if (retro_state.active) retro_paren_cancel(&retro_state);
                cursor_pos = jump_word_backward(input_buf, cursor_pos);
                words_sel_start = -1;
                words_sel_end = -1;
                XClearArea(dpy, win, 0, 0, 0, 0, True);
                continue;
            } else if ((ev.xkey.state & Mod1Mask) && (ksym == XK_f || ksym == XK_F)) {
                if (retro_state.active) retro_paren_cancel(&retro_state);
                cursor_pos = jump_word_forward(input_buf, input_len, cursor_pos);
                words_sel_start = -1;
                words_sel_end = -1;
                XClearArea(dpy, win, 0, 0, 0, 0, True);
                continue;
            } else if ((ev.xkey.state & ControlMask) && (ksym == XK_u || ksym == XK_U)) {
                if (retro_state.active) retro_paren_cancel(&retro_state);
                if (cursor_pos > 0) {
                    memmove(&input_buf[0], &input_buf[cursor_pos], input_len - cursor_pos + 1);
                    input_len -= cursor_pos;
                    cursor_pos = 0;
                    undo_stack_push(&undo_stack, input_buf, cursor_pos, UNDO_ACTION_DELETE);
                }
            } else if ((ev.xkey.state & ControlMask) && (ksym == XK_k || ksym == XK_K)) {
                if (retro_state.active) retro_paren_cancel(&retro_state);
                input_buf[cursor_pos] = '\0';
                input_len = cursor_pos;
                undo_stack_push(&undo_stack, input_buf, cursor_pos, UNDO_ACTION_DELETE);
            } else if ((ev.xkey.state & ControlMask) && (ksym == XK_w || ksym == XK_W)) {
                if (retro_state.active) retro_paren_cancel(&retro_state);
                if (cursor_pos > 0) {
                    size_t i = jump_word_backward(input_buf, cursor_pos);
                    size_t removed = cursor_pos - i;
                    memmove(&input_buf[i], &input_buf[cursor_pos], input_len - cursor_pos + 1);
                    input_len -= removed;
                    cursor_pos = i;
                    words_sel_start = -1;
                    words_sel_end = -1;
                    undo_stack_push(&undo_stack, input_buf, cursor_pos, UNDO_ACTION_DELETE);
                }
            } else if ((ev.xkey.state & ControlMask) && (ksym == XK_d || ksym == XK_D)) {
                if (retro_state.active) {
                    retro_paren_cancel(&retro_state);
                } else {
                    smart_delete_paren(input_buf, &cursor_pos, &input_len);
                    undo_stack_push(&undo_stack, input_buf, cursor_pos, UNDO_ACTION_DELETE);
                }
            } else if ((ev.xkey.state & ControlMask) && (ksym == XK_h || ksym == XK_H)) {
                if (retro_state.active) {
                    retro_paren_cancel(&retro_state);
                } else {
                    smart_backspace_paren(input_buf, &cursor_pos, &input_len);
                    undo_stack_push(&undo_stack, input_buf, cursor_pos, UNDO_ACTION_DELETE);
                }
            }

            if ((ev.xkey.state & ControlMask) && (ksym == XK_semicolon || ksym == XK_colon)) {
                drawer_open = !drawer_open;
                if (!drawer_open) {
                    g_drawer_scroll_y = 0.0;
                    g_hover_type = 0;
                    g_hover_idx = -1;
                    g_hover_del = false;
                }
                cur_h = drawer_open ? win_h_expanded : win_h_collapsed;
                XResizeWindow(dpy, win, win_w, cur_h);
                XSetWindowBackgroundPixmap(dpy, win, None);
                cairo_destroy(cr);
                cairo_surface_destroy(buf_surf);
                cairo_surface_destroy(xlib_surf);
                xlib_surf = cairo_xlib_surface_create(dpy, win, DefaultVisual(dpy, screen), win_w, cur_h);
                buf_surf = cairo_image_surface_create(CAIRO_FORMAT_RGB24, win_w, cur_h);
                cr = cairo_create(buf_surf);
                XClearArea(dpy, win, 0, 0, 0, 0, True);
                continue;
            }

            if (ksym != XK_Tab && ksym != XK_ISO_Left_Tab && ksym != XK_Right) {
                autocomp_just_cycled = false;
            }
            if (ksym != XK_Return && ksym != XK_KP_Enter) {
                just_evaluated_constant = false;
            }

            if (ksym == XK_Escape) {
                if (autocomp_state.active) {
                    autocomp_state.active = false;
                    autocomp_just_cycled = false;
                    XClearArea(dpy, win, 0, 0, 0, 0, True);
                    continue;
                }
                if (retro_state.active) {
                    retro_paren_cancel(&retro_state);
                    XClearArea(dpy, win, 0, 0, 0, 0, True);
                    continue;
                }
                if (drawer_open) {
                    drawer_open = false;
                    g_drawer_scroll_y = 0.0;
                    g_hover_type = 0;
                    g_hover_idx = -1;
                    g_hover_del = false;
                    cur_h = win_h_collapsed;
                    XResizeWindow(dpy, win, win_w, cur_h);
                    XSetWindowBackgroundPixmap(dpy, win, None);
                    cairo_destroy(cr);
                    cairo_surface_destroy(buf_surf);
                    cairo_surface_destroy(xlib_surf);
                    xlib_surf = cairo_xlib_surface_create(dpy, win, DefaultVisual(dpy, screen), win_w, cur_h);
                    buf_surf = cairo_image_surface_create(CAIRO_FORMAT_RGB24, win_w, cur_h);
                    cr = cairo_create(buf_surf);
                    XClearArea(dpy, win, 0, 0, 0, 0, True);
                    continue;
                }
                running = false;
                break;
            } else if (ksym == XK_Tab || ksym == XK_ISO_Left_Tab) {
                if (autocomp_state.active) {
                    if (autocomp_state.match_count > 1) {
                        bool backwards = (ksym == XK_ISO_Left_Tab) || (ev.xkey.state & ShiftMask);
                        if (!autocomp_just_cycled) {
                            /* First Tab press: commit candidate 0 and begin cycle tracking */
                            autocomp_prev_comp_len = strlen(autocomp_state.full_completion);
                            autocomplete_commit(input_buf, sizeof(input_buf), &cursor_pos, &input_len, &autocomp_state);
                            undo_stack_push(&undo_stack, input_buf, cursor_pos, UNDO_ACTION_OTHER);
                            autocomp_just_cycled = true;
                            autocomp_cycle_idx = 0;
                            autocomp_state.ghost_suffix[0] = '\0';
                        } else {
                            /* Repeated Tab press: cycle to next/previous candidate and replace */
                            if (backwards) {
                                autocomp_cycle_idx = (autocomp_cycle_idx + autocomp_state.match_count - 1) % autocomp_state.match_count;
                            } else {
                                autocomp_cycle_idx = (autocomp_cycle_idx + 1) % autocomp_state.match_count;
                            }
                            AutocompleteSuggestion next_sug = autocomp_state;
                            next_sug.current_index = autocomp_cycle_idx;
                            const char *cand_name = autocomp_state.candidate_names[autocomp_cycle_idx];
                            next_sug.is_func = autocomp_state.candidate_is_func[autocomp_cycle_idx];
                            if (next_sug.is_func) {
                                snprintf(next_sug.full_completion, sizeof(next_sug.full_completion), "%.31s()", cand_name);
                                next_sug.cursor_offset = strlen(cand_name) + 1;
                            } else {
                                snprintf(next_sug.full_completion, sizeof(next_sug.full_completion), "%.31s", cand_name);
                                next_sug.cursor_offset = strlen(cand_name);
                            }
                            autocomplete_cycle_replace(input_buf, sizeof(input_buf), &cursor_pos, &input_len, autocomp_prev_comp_len, &next_sug);
                            undo_stack_push(&undo_stack, input_buf, cursor_pos, UNDO_ACTION_OTHER);
                            autocomp_prev_comp_len = strlen(next_sug.full_completion);

                            /* Update preview bar */
                            char bar[256];
                            int n = snprintf(bar, sizeof(bar), "Tab: ");
                            size_t show_count = autocomp_state.match_count > 4 ? 4 : autocomp_state.match_count;
                            for (size_t i = 0; i < show_count && n < (int)sizeof(bar) - 1; i++) {
                                if (i > 0) n += snprintf(bar + n, sizeof(bar) - (size_t)n, "  •  ");
                                if (i == autocomp_cycle_idx) {
                                    n += snprintf(bar + n, sizeof(bar) - (size_t)n, "[%.31s%s]",
                                                  autocomp_state.candidate_names[i], autocomp_state.candidate_is_func[i] ? "()" : "");
                                } else {
                                    n += snprintf(bar + n, sizeof(bar) - (size_t)n, "%.31s%s",
                                                  autocomp_state.candidate_names[i], autocomp_state.candidate_is_func[i] ? "()" : "");
                                }
                            }
                            if (autocomp_state.match_count > show_count && n < (int)sizeof(bar) - 1) {
                                n += snprintf(bar + n, sizeof(bar) - (size_t)n, "  •  ...");
                            }
                            if (n < (int)sizeof(bar) - 1) {
                                snprintf(bar + n, sizeof(bar) - (size_t)n, " (%u/%u)",
                                         (unsigned int)(autocomp_cycle_idx + 1), (unsigned int)autocomp_state.match_count);
                            }
                            snprintf(autocomp_state.preview_bar, sizeof(autocomp_state.preview_bar), "%s", bar);
                            autocomp_state.ghost_suffix[0] = '\0';
                        }
                    } else {
                        autocomplete_commit(input_buf, sizeof(input_buf), &cursor_pos, &input_len, &autocomp_state);
                        undo_stack_push(&undo_stack, input_buf, cursor_pos, UNDO_ACTION_OTHER);
                        autocomp_state.active = false;
                        autocomp_just_cycled = false;
                        autocomp_state.ghost_suffix[0] = '\0';
                    }
                    goto perform_live_eval;
                } else if (retro_state.active) {
                    bracket_cycle_commit(&retro_state, input_buf, sizeof(input_buf), &cursor_pos, &input_len);
                    undo_stack_push(&undo_stack, input_buf, cursor_pos, UNDO_ACTION_OTHER);
                    goto perform_live_eval;
                } else {
                    XClearArea(dpy, win, 0, 0, 0, 0, True);
                    continue;
                }
            } else if (ksym == XK_Return || ksym == XK_KP_Enter) {
                if (retro_state.active) {
                    retro_paren_commit(&retro_state, input_buf, sizeof(input_buf), &cursor_pos, &input_len);
                }
                maybe_format_number_before_cursor(input_buf, sizeof(input_buf), &cursor_pos, &input_len, '\0');

                if (just_evaluated_constant && last_constant_op[0] != '\0') {
                    char chain_expr[256];
                    snprintf(chain_expr, sizeof(chain_expr), "ans %s", last_constant_op);
                    strncpy(input_buf, chain_expr, sizeof(input_buf) - 1);
                    input_buf[sizeof(input_buf) - 1] = '\0';
                    input_len = strlen(input_buf);
                    cursor_pos = input_len;
                }
                char eval_input_copy[256];
                strncpy(eval_input_copy, input_buf, sizeof(eval_input_copy) - 1);
                eval_input_copy[sizeof(eval_input_copy) - 1] = '\0';

                double final_res = 0.0;
                const char *err = NULL;
                double c_val = 0.0;
                char c_res[128];
                char c_unit[32] = "";
                char c_words_sfx[64];
                const char *c_err = NULL;
                bool is_conv = (input_len > 0 && try_convert_expression(input_buf, ans_val, &c_val, c_res, sizeof(c_res), c_unit, sizeof(c_unit), c_words_sfx, sizeof(c_words_sfx), &c_err));

                if (is_conv && c_err) {
                    just_evaluated_constant = false;
                    last_constant_op[0] = '\0';
                    continue;
                }

                bool is_date = false;
                bool is_time = false;
                char enter_unit[32] = "";
                if (is_conv || (input_len > 0 && evaluate_equation_ex(input_buf, ans_val, &final_res, enter_unit, sizeof(enter_unit), &is_date, &is_time, &err))) {
                    if (is_conv) {
                        final_res = c_val;
                        snprintf(result_buf, sizeof(result_buf), "%s", c_res);
                    } else if (is_time) {
                        format_time_result(final_res, false, result_buf, sizeof(result_buf));
                    } else if (is_date) {
                        format_date_result(final_res, result_buf, sizeof(result_buf));
                    } else {
                        char comp_unit_buf[128];
                        if (enter_unit[0] && format_compound_unit(final_res, enter_unit, comp_unit_buf, sizeof(comp_unit_buf))) {
                            snprintf(result_buf, sizeof(result_buf), "%s", comp_unit_buf);
                        } else {
                            format_result(final_res, result_buf, sizeof(result_buf));
                            if (enter_unit[0]) {
                                strncat(result_buf, " ", sizeof(result_buf) - strlen(result_buf) - 1);
                                strncat(result_buf, enter_unit, sizeof(result_buf) - strlen(result_buf) - 1);
                            }
                        }
                    }

                    /* Save to persistent equation history */
                    save_history_entry(input_buf);
                    if (hist_count < MAX_HISTORY) {
                        snprintf(history[hist_count++], sizeof(history[0]), "%s", input_buf);
                    } else {
                        memmove(history[0], history[1], (MAX_HISTORY - 1) * sizeof(history[0]));
                        snprintf(history[MAX_HISTORY - 1], sizeof(history[0]), "%s", input_buf);
                    }
                    hist_idx = hist_count;

                    /* Save to persistent ans memory */
                    ans_val = final_res;
                    save_ans(ans_val);

                    /* Print result to stdout for terminal/pipe use */
                    printf("%s\n", result_buf);
                    fflush(stdout);

                    /* Replace input with formatted answer */
                    snprintf(input_buf, sizeof(input_buf), "%s", result_buf);
                    input_len = strlen(input_buf);
                    cursor_pos = input_len;
                    undo_stack_push(&undo_stack, input_buf, cursor_pos, UNDO_ACTION_OTHER);

                    /* Update words and live evaluation */
                    has_valid_result = true;
                    current_evaluated_val = final_res;
                    current_is_conv = is_conv || (enter_unit[0] != '\0');
                    if (is_conv) {
                        strncpy(current_unit_sym, c_unit, sizeof(current_unit_sym) - 1);
                        current_unit_sym[sizeof(current_unit_sym) - 1] = '\0';
                        strncpy(current_words_sfx, c_words_sfx, sizeof(current_words_sfx) - 1);
                        current_words_sfx[sizeof(current_words_sfx) - 1] = '\0';
                    } else if (enter_unit[0]) {
                        strncpy(current_unit_sym, enter_unit, sizeof(current_unit_sym) - 1);
                        current_unit_sym[sizeof(current_unit_sym) - 1] = '\0';
                        const UnitItem *u = find_unit(enter_unit);
                        if (u && u->alias_count > 1) snprintf(current_words_sfx, sizeof(current_words_sfx), "%s", u->aliases[1]);
                        else if (u && u->alias_count > 0) snprintf(current_words_sfx, sizeof(current_words_sfx), "%s", u->aliases[0]);
                        else snprintf(current_words_sfx, sizeof(current_words_sfx), "%s", enter_unit);
                        if (current_words_sfx[0]) current_words_sfx[0] = (char)toupper((unsigned char)current_words_sfx[0]);
                    } else {
                        current_unit_sym[0] = '\0';
                        current_words_sfx[0] = '\0';
                    }
                    current_is_date = is_conv ? false : is_date;
                    current_is_time = is_conv ? false : is_time;
                    set_ans_date_time(current_is_date, current_is_time);

                    char trailing_op[128];
                    if (extract_trailing_binary_op(eval_input_copy, trailing_op, sizeof(trailing_op))) {
                        strncpy(last_constant_op, trailing_op, sizeof(last_constant_op) - 1);
                        last_constant_op[sizeof(last_constant_op) - 1] = '\0';
                        just_evaluated_constant = true;
                    } else {
                        just_evaluated_constant = false;
                        last_constant_op[0] = '\0';
                    }

                    format_computing_row(final_res, output_mode, current_unit_sym[0] ? current_unit_sym : NULL, current_words_sfx[0] ? current_words_sfx : NULL, current_is_date, current_is_time, comp_buf, sizeof(comp_buf));
                    format_subtitle_preview(final_res, current_unit_sym[0] ? current_unit_sym : NULL, current_words_sfx[0] ? current_words_sfx : NULL, current_is_date, current_is_time, words_buf, sizeof(words_buf));

                    XClearArea(dpy, win, 0, 0, 0, 0, True);
                } else {
                    just_evaluated_constant = false;
                    last_constant_op[0] = '\0';
                }
                continue;
            } else if (ksym == XK_Up) {
                if (retro_state.active) retro_paren_cancel(&retro_state);
                if (hist_count > 0 && hist_idx > 0) {
                    if (hist_idx == hist_count) {
                        strncpy(saved_draft, input_buf, sizeof(saved_draft) - 1);
                        saved_draft[sizeof(saved_draft) - 1] = '\0';
                    }
                    hist_idx--;
                    strncpy(input_buf, history[hist_idx], sizeof(input_buf) - 1);
                    input_buf[sizeof(input_buf) - 1] = '\0';
                    input_len = strlen(input_buf);
                    cursor_pos = input_len;
                    undo_stack_push(&undo_stack, input_buf, cursor_pos, UNDO_ACTION_OTHER);
                }
            } else if (ksym == XK_Down) {
                if (retro_state.active) retro_paren_cancel(&retro_state);
                if (hist_idx < hist_count) {
                    hist_idx++;
                    if (hist_idx == hist_count) {
                        strncpy(input_buf, saved_draft, sizeof(input_buf) - 1);
                        input_buf[sizeof(input_buf) - 1] = '\0';
                    } else {
                        strncpy(input_buf, history[hist_idx], sizeof(input_buf) - 1);
                        input_buf[sizeof(input_buf) - 1] = '\0';
                    }
                    input_len = strlen(input_buf);
                    cursor_pos = input_len;
                    undo_stack_push(&undo_stack, input_buf, cursor_pos, UNDO_ACTION_OTHER);
                }
            } else if (ksym == XK_Left) {
                if (retro_state.active) retro_paren_cancel(&retro_state);
                if (ev.xkey.state & (ControlMask | Mod1Mask)) {
                    cursor_pos = jump_word_backward(input_buf, cursor_pos);
                } else if (cursor_pos > 0) {
                    cursor_pos--;
                }
                words_sel_start = -1;
                words_sel_end = -1;
            } else if (ksym == XK_Right) {
                if (retro_state.active) retro_paren_cancel(&retro_state);
                if (autocomp_just_cycled) {
                    autocomp_just_cycled = false;
                    autocomp_state.active = false;
                } else if (autocomp_state.active && cursor_pos == input_len && !(ev.xkey.state & (ControlMask | Mod1Mask))) {
                    autocomplete_commit(input_buf, sizeof(input_buf), &cursor_pos, &input_len, &autocomp_state);
                    undo_stack_push(&undo_stack, input_buf, cursor_pos, UNDO_ACTION_OTHER);
                    autocomp_state.active = false;
                    goto perform_live_eval;
                } else if (ev.xkey.state & (ControlMask | Mod1Mask)) {
                    cursor_pos = jump_word_forward(input_buf, input_len, cursor_pos);
                } else if (cursor_pos < input_len) {
                    cursor_pos++;
                }
                words_sel_start = -1;
                words_sel_end = -1;
            } else if (ksym == XK_Home) {
                if (retro_state.active) retro_paren_cancel(&retro_state);
                cursor_pos = 0;
            } else if (ksym == XK_End) {
                if (retro_state.active) retro_paren_cancel(&retro_state);
                cursor_pos = input_len;
            } else if (ksym == XK_BackSpace) {
                if (retro_state.active) {
                    retro_paren_cancel(&retro_state);
                } else {
                    smart_backspace_paren(input_buf, &cursor_pos, &input_len);
                    undo_stack_push(&undo_stack, input_buf, cursor_pos, UNDO_ACTION_DELETE);
                }
            } else if (ksym == XK_Delete) {
                if (retro_state.active) {
                    retro_paren_cancel(&retro_state);
                } else {
                    smart_delete_paren(input_buf, &cursor_pos, &input_len);
                    undo_stack_push(&undo_stack, input_buf, cursor_pos, UNDO_ACTION_DELETE);
                }
            } else if (len > 0 && keybuf[0] == '(') {
                if (retro_state.active && retro_state.mode == BRACKET_CYCLE_PRO) {
                    bracket_cycle_step_pro(&retro_state, input_buf, input_len, cursor_pos);
                } else if (retro_state.active && retro_state.mode == BRACKET_CYCLE_RETRO) {
                    /* Typing suggested '(' confirms and commits the retroactive bracket suggestion */
                    bracket_cycle_commit(&retro_state, input_buf, sizeof(input_buf), &cursor_pos, &input_len);
                    undo_stack_push(&undo_stack, input_buf, cursor_pos, UNDO_ACTION_OTHER);
                } else {
                    if (has_unpaired_closing_bracket_after(input_buf, input_len, cursor_pos)) {
                        smart_insert_open_paren(input_buf, sizeof(input_buf), &cursor_pos, &input_len);
                        undo_stack_push(&undo_stack, input_buf, cursor_pos, UNDO_ACTION_OTHER);
                    } else if (cursor_pos >= input_len) {
                        smart_insert_open_paren(input_buf, sizeof(input_buf), &cursor_pos, &input_len);
                        undo_stack_push(&undo_stack, input_buf, cursor_pos, UNDO_ACTION_OTHER);
                    } else {
                        /* Check if there is only whitespace ahead */
                        size_t ahead = cursor_pos;
                        while (ahead < input_len && isspace((unsigned char)input_buf[ahead])) ahead++;
                        if (ahead >= input_len) {
                            smart_insert_open_paren(input_buf, sizeof(input_buf), &cursor_pos, &input_len);
                            undo_stack_push(&undo_stack, input_buf, cursor_pos, UNDO_ACTION_OTHER);
                        } else {
                            if (!bracket_cycle_step_pro(&retro_state, input_buf, input_len, cursor_pos)) {
                                smart_insert_open_paren(input_buf, sizeof(input_buf), &cursor_pos, &input_len);
                                undo_stack_push(&undo_stack, input_buf, cursor_pos, UNDO_ACTION_OTHER);
                            }
                        }
                    }
                }
            } else if (len > 0 && keybuf[0] == ')') {
                maybe_format_number_before_cursor(input_buf, sizeof(input_buf), &cursor_pos, &input_len, ')');
                if (retro_state.active && retro_state.mode == BRACKET_CYCLE_PRO) {
                    size_t cut = retro_state.cut_points[retro_state.current_idx];
                    bracket_cycle_commit(&retro_state, input_buf, sizeof(input_buf), &cursor_pos, &input_len);
                    cursor_pos = cut + 2;
                    undo_stack_push(&undo_stack, input_buf, cursor_pos, UNDO_ACTION_OTHER);
                } else if (cursor_pos < input_len && input_buf[cursor_pos] == ')') {
                    if (retro_state.active) bracket_cycle_cancel(&retro_state);
                    cursor_pos++;
                } else {
                    int unclosed_before = 0;
                    for (size_t i = 0; i < cursor_pos; i++) {
                        if (input_buf[i] == '(') unclosed_before++;
                        else if (input_buf[i] == ')') {
                            if (unclosed_before > 0) unclosed_before--;
                        }
                    }
                    if (unclosed_before > 0) {
                        if (retro_state.active) {
                            bracket_cycle_commit(&retro_state, input_buf, sizeof(input_buf), &cursor_pos, &input_len);
                            undo_stack_push(&undo_stack, input_buf, cursor_pos, UNDO_ACTION_OTHER);
                        }
                        if (input_len + 1 < sizeof(input_buf) - 1) {
                            memmove(&input_buf[cursor_pos + 1], &input_buf[cursor_pos], input_len - cursor_pos + 1);
                            input_buf[cursor_pos] = ')';
                            input_len++;
                            cursor_pos++;
                            undo_stack_push(&undo_stack, input_buf, cursor_pos, UNDO_ACTION_OTHER);
                        }
                    } else {
                        /* Retroactive cycling mode: cycles candidate cuts on repeated taps of ')' */
                        bracket_cycle_step_retro(&retro_state, input_buf, cursor_pos);
                    }
                }
            } else if (len > 0 && (unsigned char)keybuf[0] >= 32) {
                char ch = keybuf[0];
                if (retro_state.active) {
                    if (retro_state.mode == BRACKET_CYCLE_PRO && (ch == '*' || ch == '/' || ch == '^' || ch == '%')) {
                        size_t cut = retro_state.cut_points[retro_state.current_idx];
                        bracket_cycle_commit(&retro_state, input_buf, sizeof(input_buf), &cursor_pos, &input_len);
                        cursor_pos = cut + 2;
                    } else {
                        bracket_cycle_commit(&retro_state, input_buf, sizeof(input_buf), &cursor_pos, &input_len);
                    }
                    undo_stack_push(&undo_stack, input_buf, cursor_pos, UNDO_ACTION_OTHER);
                }
                if (ch == '+' || ch == '-' || ch == '*' || ch == '/' || ch == 'x' || ch == 'X' ||
                    ch == '^' || ch == '%' || ch == ' ') {
                    maybe_format_number_before_cursor(input_buf, sizeof(input_buf), &cursor_pos, &input_len, ch);
                }

                if (input_len == 0 && cursor_pos == 0 &&
                    (ch == '+' || ch == '*' || ch == '/' || ch == '^' || ch == '%')) {
                    const char *pfx = "ans ";
                    size_t pfx_len = strlen(pfx);
                    memcpy(input_buf, pfx, pfx_len);
                    input_buf[pfx_len] = '\0';
                    input_len = pfx_len;
                    cursor_pos = pfx_len;
                }

                if (input_len + len < sizeof(input_buf) - 1) {
                    memmove(&input_buf[cursor_pos + len], &input_buf[cursor_pos], input_len - cursor_pos + 1);
                    memcpy(&input_buf[cursor_pos], keybuf, len);
                    input_len += len;
                    cursor_pos += len;
                    UndoActionType act = (len == 1 && (isalnum((unsigned char)keybuf[0]) || keybuf[0] == '.')) ? UNDO_ACTION_TYPE : UNDO_ACTION_OTHER;
                    undo_stack_push(&undo_stack, input_buf, cursor_pos, act);
                }
            }

        perform_live_eval:;
            /* Live re-evaluation */
            double res_val = 0.0;
            const char *err = NULL;
            bool is_date = false;
            bool is_time = false;
            double c_val = 0.0;
            char c_res[128];
            char c_unit[32] = "";
            char c_words_sfx[64];
            const char *c_err = NULL;
            char hint_buf[256];

            /* Prepare effective buffer taking active ghost brackets into account */
            char effective_eval_buf[300];
            const char *eval_target = input_buf;
            size_t eval_target_len = input_len;
            if (retro_state.active && retro_state.cut_count > 0) {
                size_t c_pos = retro_state.cut_points[retro_state.current_idx];
                size_t anchor_pos = retro_state.anchor;
                size_t o_pos = anchor_pos < c_pos ? anchor_pos : c_pos;
                size_t cl_pos = anchor_pos < c_pos ? c_pos : anchor_pos;
                if (o_pos <= cl_pos && cl_pos <= input_len && input_len + 2 < sizeof(effective_eval_buf)) {
                    memcpy(effective_eval_buf, input_buf, o_pos);
                    effective_eval_buf[o_pos] = '(';
                    memcpy(&effective_eval_buf[o_pos + 1], &input_buf[o_pos], cl_pos - o_pos);
                    effective_eval_buf[cl_pos + 1] = ')';
                    memcpy(&effective_eval_buf[cl_pos + 2], &input_buf[cl_pos], input_len - cl_pos);
                    effective_eval_buf[input_len + 2] = '\0';
                    eval_target = effective_eval_buf;
                    eval_target_len = input_len + 2;
                }
            }

            if (eval_target_len > 0 && try_convert_expression(eval_target, ans_val, &c_val, c_res, sizeof(c_res), c_unit, sizeof(c_unit), c_words_sfx, sizeof(c_words_sfx), &c_err) && !c_err) {
                has_valid_result = true;
                is_showing_hint = false;
                current_evaluated_val = c_val;
                current_is_conv = true;
                strncpy(current_unit_sym, c_unit, sizeof(current_unit_sym) - 1);
                current_unit_sym[sizeof(current_unit_sym) - 1] = '\0';
                strncpy(current_words_sfx, c_words_sfx, sizeof(current_words_sfx) - 1);
                current_words_sfx[sizeof(current_words_sfx) - 1] = '\0';
                snprintf(result_buf, sizeof(result_buf), "%s", c_res);
                current_is_date = false;
                current_is_time = false;
                format_computing_row(c_val, output_mode, c_unit, c_words_sfx, false, false, comp_buf, sizeof(comp_buf));
                format_subtitle_preview(c_val, c_unit, c_words_sfx, false, false, words_buf, sizeof(words_buf));
            } else if (eval_target_len > 0 && evaluate_equation_ex(eval_target, ans_val, &res_val, current_unit_sym, sizeof(current_unit_sym), &is_date, &is_time, &err)) {
                has_valid_result = true;
                is_showing_hint = false;
                current_evaluated_val = res_val;
                current_is_conv = (current_unit_sym[0] != '\0');
                if (current_unit_sym[0]) {
                    const UnitItem *u = find_unit(current_unit_sym);
                    if (u && u->alias_count > 1) snprintf(current_words_sfx, sizeof(current_words_sfx), "%s", u->aliases[1]);
                    else if (u && u->alias_count > 0) snprintf(current_words_sfx, sizeof(current_words_sfx), "%s", u->aliases[0]);
                    else snprintf(current_words_sfx, sizeof(current_words_sfx), "%s", current_unit_sym);
                    if (current_words_sfx[0]) current_words_sfx[0] = (char)toupper((unsigned char)current_words_sfx[0]);
                } else {
                    current_words_sfx[0] = '\0';
                }
                if (is_time) {
                    format_time_result(res_val, false, result_buf, sizeof(result_buf));
                } else if (is_date) {
                    if (current_unit_sym[0] && is_valid_timezone_query(current_unit_sym)) {
                        TimezoneResult tz_res;
                        if (convert_to_timezone((time_t)round(res_val), current_unit_sym, &tz_res)) {
                            snprintf(result_buf, sizeof(result_buf), "%.96s %.16s", tz_res.time_str, tz_res.abbrev);
                        } else {
                            format_date_result(res_val, result_buf, sizeof(result_buf));
                        }
                    } else {
                        format_date_result(res_val, result_buf, sizeof(result_buf));
                    }
                } else {
                    char comp_unit_buf[128];
                    if (current_unit_sym[0] && format_compound_unit(res_val, current_unit_sym, comp_unit_buf, sizeof(comp_unit_buf))) {
                        snprintf(result_buf, sizeof(result_buf), "%s", comp_unit_buf);
                    } else {
                        format_result(res_val, result_buf, sizeof(result_buf));
                        if (current_unit_sym[0]) {
                            strncat(result_buf, " ", sizeof(result_buf) - strlen(result_buf) - 1);
                            strncat(result_buf, current_unit_sym, sizeof(result_buf) - strlen(result_buf) - 1);
                        }
                    }
                }
                current_is_date = is_date;
                current_is_time = is_time;
                format_computing_row(res_val, output_mode, current_unit_sym[0] ? current_unit_sym : NULL, current_words_sfx[0] ? current_words_sfx : NULL, is_date, is_time, comp_buf, sizeof(comp_buf));
                format_subtitle_preview(res_val, current_unit_sym[0] ? current_unit_sym : NULL, current_words_sfx[0] ? current_words_sfx : NULL, is_date, is_time, words_buf, sizeof(words_buf));
            } else if (eval_target_len > 0 && get_conversion_hints(eval_target, hint_buf, sizeof(hint_buf))) {
                has_valid_result = false;
                is_showing_hint = true;
                current_is_conv = false;
                result_buf[0] = '\0';
                comp_buf[0] = '\0';
                snprintf(words_buf, sizeof(words_buf), "%s", hint_buf);
            } else if (c_err) {
                has_valid_result = true;
                is_showing_hint = false;
                snprintf(result_buf, sizeof(result_buf), "%s", c_err);
                comp_buf[0] = '\0';
                words_buf[0] = '\0';
                current_is_conv = false;
            } else if (err) {
                has_valid_result = true;
                is_showing_hint = false;
                snprintf(result_buf, sizeof(result_buf), "%s", err);
                comp_buf[0] = '\0';
                words_buf[0] = '\0';
                current_is_conv = false;
            } else {
                has_valid_result = false;
                is_showing_hint = false;
                comp_buf[0] = '\0';
                words_buf[0] = '\0';
                current_is_conv = false;
            }
            words_sel_start = -1;
            words_sel_end = -1;

            /* Update Autocomplete Query */
            if (!drawer_open && !retro_state.active && cursor_pos == input_len) {
                if (!autocomp_just_cycled) {
                    autocomplete_query_cycle(input_buf, cursor_pos, 0, &autocomp_state);
                }
            } else {
                if (!autocomp_just_cycled) {
                    autocomp_state.active = false;
                }
            }

            /* Redraw */
            XClearArea(dpy, win, 0, 0, 0, 0, True);
        } else if (ev.type == SelectionRequest) {
            XSelectionRequestEvent *req = &ev.xselectionrequest;
            XSelectionEvent sel_ev = {
                .type = SelectionNotify,
                .display = req->display,
                .requestor = req->requestor,
                .selection = req->selection,
                .target = req->target,
                .property = req->property,
                .time = req->time
            };

            if (req->target == targets) {
                Atom supported[] = { utf8_string, XA_STRING };
                XChangeProperty(dpy, req->requestor, req->property, XA_ATOM, 32,
                                PropModeReplace, (unsigned char *)supported, 2);
            } else if (req->target == utf8_string || req->target == XA_STRING) {
                XChangeProperty(dpy, req->requestor, req->property, req->target, 8,
                                PropModeReplace, (unsigned char *)active_clipboard_buf, strlen(active_clipboard_buf));
            } else {
                sel_ev.property = None;
            }
            XSendEvent(dpy, req->requestor, False, 0, (XEvent *)&sel_ev);
        } else if (ev.type == ClientMessage) {
            if ((Atom)ev.xclient.data.l[0] == wm_delete) {
                running = false;
                break;
            } else {
                /* Currency background thread finished: trigger redraw */
                XClearArea(dpy, win, 0, 0, 0, 0, True);
            }
        }
    }

    /* Cleanup */
    cairo_destroy(cr);
    cairo_surface_destroy(buf_surf);
    cairo_surface_destroy(xlib_surf);
    XDestroyWindow(dpy, win);
    XCloseDisplay(dpy);
    cleanup_fonts();

    return 0;
}

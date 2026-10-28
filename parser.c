#define _GNU_SOURCE
#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L
#include "elephant.h"
#include "timezone.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdbool.h>
#include <math.h>
#include <ctype.h>
#include <time.h>
#include <stdint.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#ifndef M_E
#define M_E 2.71828182845904523536
#endif

/* ==========================================================================
 * Advanced Math Expression Parser
 * Supports: +, -, *, /, %, ^, sqrt, sin, cos, tan, abs, log, ln, exp, pi, e, tau, phi, ans
 * Programmer mode: 0x, 0b, 0o, &, |, ~, <<, >>, xor, mod
 * Date/time: now, today, YYYY-MM-DD, day, week, month, year...
 * Financial: emi, cagr, sip, compound, discount, markup
 * Variables: <name> = <expr>, dynamic symbol lookup
 * Contextual percentages: 500 - 20% = 400, 200 * 15% = 30, 50% = 0.5
 * ========================================================================== */

static CalcSymbol g_symbols[MAX_SYMBOLS];
static int g_symbol_count = 0;
static bool g_ans_is_date = false;
static bool g_ans_is_time = false;

void set_ans_date_time(bool is_date, bool is_time) {
    g_ans_is_date = is_date;
    g_ans_is_time = is_time;
}

typedef struct {
    char name[16];
    double val;
} LocalParam;

#define MAX_LOCAL_STACK 64
static LocalParam g_local_stack[MAX_LOCAL_STACK];
static int g_local_stack_top = 0;

static int g_eval_depth = 0;
static int g_func_call_depth = 0;

static bool is_reserved_name(const char *name) {
    static const char *reserved[] = {
        "sin", "cos", "tan", "asin", "acos", "atan",
        "sinh", "cosh", "tanh", "sqrt", "cbrt", "log", "ln",
        "abs", "exp", "ceil", "floor", "round",
        "pi", "e", "tau", "phi", "ans",
        "now", "today", "tomorrow", "yesterday", "next", "end", "start", "time",
        "day", "days", "week", "weeks", "hour", "hours", "hr", "hrs",
        "min", "mins", "minute", "minutes", "sec", "secs", "second", "seconds",
        "month", "months", "year", "years", "yr", "yrs",
        "jan", "january", "feb", "february", "mar", "march", "apr", "april",
        "may", "jun", "june", "jul", "july", "aug", "august",
        "sep", "sept", "september", "oct", "october", "nov", "november", "dec", "december",
        "xor", "mod", "pow",
        "emi", "cagr", "sip", "compound", "discount", "markup",
        "avg", "average", "median", "min", "max",
        "bday", "bdays", "bizday", "bizdays", "workday", "workdays",
        "until", "since", "before", "after", "between",
        "k", "thousand", "thousands", "million", "millions",
        "billion", "billions", "trillion", "trillions",
        "lakh", "lakhs", "lac", "lacs", "crore", "crores", "cr",
        "to", "in", "vars", "saved", "delete", "undef", "remove"
    };
    for (size_t i = 0; i < sizeof(reserved) / sizeof(reserved[0]); i++) {
        if (strcasecmp(name, reserved[i]) == 0) return true;
    }
    return false;
}

static bool is_reserved_var_name(const char *name) {
    if (is_reserved_name(name)) return true;
    if (find_unit(name) != NULL) return true;
    return false;
}

bool define_symbol_ex(const char *name, const char *expr, bool is_func, int param_count, const char params[][16],
                      bool persistent, double cached_val, const char *unit, bool is_date, bool is_time) {
    if (!name || !*name) return false;
    if (is_func ? is_reserved_name(name) : is_reserved_var_name(name)) return false;
    for (int i = 0; i < g_symbol_count; i++) {
        if (strcasecmp(g_symbols[i].name, name) == 0) {
            CalcSymbol *s = &g_symbols[i];
            s->is_func = is_func;
            s->param_count = param_count;
            if (is_func && params) {
                for (int p = 0; p < param_count; p++) {
                    snprintf(s->params[p], sizeof(s->params[p]), "%s", params[p]);
                }
            } else {
                memset(s->params, 0, sizeof(s->params));
            }
            if (expr) snprintf(s->expr, sizeof(s->expr), "%s", expr);
            else s->expr[0] = '\0';
            s->cached_val = cached_val;
            if (unit) snprintf(s->unit, sizeof(s->unit), "%s", unit);
            else s->unit[0] = '\0';
            s->is_date = is_date;
            s->is_time = is_time;
            s->is_persistent = persistent;
            return true;
        }
    }
    if (g_symbol_count >= MAX_SYMBOLS) return false;
    CalcSymbol *s = &g_symbols[g_symbol_count++];
    snprintf(s->name, sizeof(s->name), "%s", name);
    s->is_func = is_func;
    s->param_count = param_count;
    if (is_func && params) {
        for (int p = 0; p < param_count; p++) {
            snprintf(s->params[p], sizeof(s->params[p]), "%s", params[p]);
        }
    } else {
        memset(s->params, 0, sizeof(s->params));
    }
    if (expr) snprintf(s->expr, sizeof(s->expr), "%s", expr);
    else s->expr[0] = '\0';
    s->cached_val = cached_val;
    if (unit) snprintf(s->unit, sizeof(s->unit), "%s", unit);
    else s->unit[0] = '\0';
    s->is_date = is_date;
    s->is_time = is_time;
    s->is_persistent = persistent;
    return true;
}

bool define_symbol(const char *name, const char *expr, bool is_func, int param_count, const char params[][16], bool persistent) {
    double c_val = 0.0;
    char c_unit[32] = "";
    bool c_is_date = false, c_is_time = false;
    if (!is_func && expr && *expr) {
        evaluate_equation_ex(expr, 0.0, &c_val, c_unit, sizeof(c_unit), &c_is_date, &c_is_time, NULL);
    }
    return define_symbol_ex(name, expr, is_func, param_count, params, persistent, c_val, c_unit, c_is_date, c_is_time);
}

bool get_symbol(const char *name, CalcSymbol *out) {
    if (!name || !*name) return false;
    for (int i = 0; i < g_symbol_count; i++) {
        if (strcasecmp(g_symbols[i].name, name) == 0) {
            if (out) *out = g_symbols[i];
            return true;
        }
    }
    return false;
}

bool delete_symbol(const char *name) {
    if (!name || !*name) return false;
    for (int i = 0; i < g_symbol_count; i++) {
        if (strcasecmp(g_symbols[i].name, name) == 0) {
            for (int j = i; j < g_symbol_count - 1; j++) {
                g_symbols[j] = g_symbols[j + 1];
            }
            g_symbol_count--;
            return true;
        }
    }
    return false;
}

void clear_all_symbols(void) {
    g_symbol_count = 0;
}

int get_all_symbols(CalcSymbol out[], int max_count) {
    int cnt = g_symbol_count < max_count ? g_symbol_count : max_count;
    for (int i = 0; i < cnt; i++) {
        out[i] = g_symbols[i];
    }
    return cnt;
}

int format_saved_symbols_list(char *buf, size_t buf_sz) {
    if (!buf || buf_sz == 0) return 0;
    buf[0] = '\0';
    int written = 0;
    for (int i = 0; i < g_symbol_count; i++) {
        CalcSymbol *s = &g_symbols[i];
        char line[256];
        if (s->is_func) {
            char p_list[64] = "";
            for (int p = 0; p < s->param_count; p++) {
                if (p > 0) strncat(p_list, ", ", sizeof(p_list) - strlen(p_list) - 1);
                strncat(p_list, s->params[p], sizeof(p_list) - strlen(p_list) - 1);
            }
            snprintf(line, sizeof(line), "%s(%s) %s %s\n", s->name, p_list, s->is_persistent ? ":=" : "=", s->expr);
        } else if (s->expr[0] != '\0') {
            double v = 0.0;
            get_variable(s->name, &v);
            char v_str[128];
            if (s->is_date) {
                format_date_result(v, v_str, sizeof(v_str));
            } else if (s->is_time) {
                format_time_result(v, false, v_str, sizeof(v_str));
            } else {
                char comp_unit_buf[128];
                if (s->unit[0] && format_compound_unit(v, s->unit, comp_unit_buf, sizeof(comp_unit_buf))) {
                    snprintf(v_str, sizeof(v_str), "%s", comp_unit_buf);
                } else {
                    format_result(v, v_str, sizeof(v_str));
                    if (s->unit[0]) {
                        strncat(v_str, " ", sizeof(v_str) - strlen(v_str) - 1);
                        strncat(v_str, s->unit, sizeof(v_str) - strlen(v_str) - 1);
                    }
                }
            }
            snprintf(line, sizeof(line), "%s %s %s  ->  %s\n", s->name, s->is_persistent ? ":=" : "=", s->expr, v_str);
        } else {
            char v_str[128];
            if (s->is_date) {
                format_date_result(s->cached_val, v_str, sizeof(v_str));
            } else if (s->is_time) {
                format_time_result(s->cached_val, false, v_str, sizeof(v_str));
            } else {
                const UnitItem *tu = s->unit[0] ? find_unit(s->unit) : NULL;
                double disp_v = tu ? (s->cached_val / tu->to_base) : s->cached_val;
                char comp_unit_buf[128];
                if (s->unit[0] && format_compound_unit(disp_v, s->unit, comp_unit_buf, sizeof(comp_unit_buf))) {
                    snprintf(v_str, sizeof(v_str), "%s", comp_unit_buf);
                } else {
                    format_result(disp_v, v_str, sizeof(v_str));
                    if (s->unit[0]) {
                        strncat(v_str, " ", sizeof(v_str) - strlen(v_str) - 1);
                        strncat(v_str, s->unit, sizeof(v_str) - strlen(v_str) - 1);
                    }
                }
            }
            snprintf(line, sizeof(line), "%s %s %s\n", s->name, s->is_persistent ? ":=" : "=", v_str);
        }
        if (strlen(buf) + strlen(line) < buf_sz - 1) {
            strcat(buf, line);
            written++;
        }
    }
    return written;
}

bool get_variable(const char *name, double *val) {
    if (!name || !*name) return false;
    /* 1. Local stack frames */
    for (int i = g_local_stack_top - 1; i >= 0; i--) {
        if (strcasecmp(g_local_stack[i].name, name) == 0) {
            if (val) *val = g_local_stack[i].val;
            return true;
        }
    }
    /* 2. Global symbols */
    for (int i = 0; i < g_symbol_count; i++) {
        if (strcasecmp(g_symbols[i].name, name) == 0) {
            CalcSymbol *s = &g_symbols[i];
            if (s->is_func) return false;
            if (s->expr[0] != '\0') {
                if (g_eval_depth >= 8) return false;
                g_eval_depth++;
                double d_val = 0.0;
                char d_unit[32] = "";
                bool d_is_date = false, d_is_time = false;
                char conv_res[64], conv_sym[32], conv_sfx[64];
                const char *c_err = NULL;
                if (try_convert_expression(s->expr, 0.0, &d_val, conv_res, sizeof(conv_res),
                                           conv_sym, sizeof(conv_sym), conv_sfx, sizeof(conv_sfx), &c_err) && !c_err) {
                    const UnitItem *tu = conv_sym[0] ? find_unit(conv_sym) : NULL;
                    s->cached_val = d_val * (tu ? tu->to_base : 1.0);
                    snprintf(s->unit, sizeof(s->unit), "%s", conv_sym);
                    s->is_date = false;
                    s->is_time = false;
                } else if (evaluate_equation_ex(s->expr, 0.0, &d_val, d_unit, sizeof(d_unit), &d_is_date, &d_is_time, NULL)) {
                    s->cached_val = d_val;
                    snprintf(s->unit, sizeof(s->unit), "%s", d_unit);
                    s->is_date = d_is_date;
                    s->is_time = d_is_time;
                }
                g_eval_depth--;
            }
            if (val) {
                const UnitItem *tu = s->unit[0] ? find_unit(s->unit) : NULL;
                *val = tu ? (s->cached_val / tu->to_base) : s->cached_val;
            }
            return true;
        }
    }
    return false;
}

bool set_variable(const char *name, double val) {
    return define_symbol_ex(name, "", false, 0, NULL, false, val, "", false, false);
}

int get_all_variables(char names[][32], double *vals, int max_count) {
    int cnt = 0;
    for (int i = 0; i < g_symbol_count && cnt < max_count; i++) {
        if (!g_symbols[i].is_func) {
            snprintf(names[cnt], 32, "%.31s", g_symbols[i].name);
            double v = 0.0;
            get_variable(g_symbols[i].name, &v);
            vals[cnt] = v;
            cnt++;
        }
    }
    return cnt;
}

void clear_variables(void) {
    int i = 0;
    while (i < g_symbol_count) {
        if (!g_symbols[i].is_persistent) {
            for (int j = i; j < g_symbol_count - 1; j++) {
                g_symbols[j] = g_symbols[j + 1];
            }
            g_symbol_count--;
        } else {
            i++;
        }
    }
}

typedef struct {
    const char *src;
    size_t pos;
    double ans_val;
    bool has_error;
    const char *error_msg;
    int in_multi_arg_func;
} Parser;

static void skip_whitespace(Parser *p) {
    while (p->src[p->pos] && isspace((unsigned char)p->src[p->pos])) {
        p->pos++;
    }
}

static char peek(Parser *p) {
    skip_whitespace(p);
    return p->src[p->pos];
}

static char get_char(Parser *p) {
    skip_whitespace(p);
    return p->src[p->pos++];
}

typedef struct {
    double val;            /* Magnitude in base units (meters, seconds, grams, etc.) */
    Dimension dim;         /* DIM_UNKNOWN, DIM_LENGTH, DIM_MASS, etc. */
    const UnitItem *unit;  /* Preferred display unit */
    bool is_pct;
    bool is_date;
    bool is_time;
    bool is_bday;
    char tz[32];           /* Timezone abbreviation if evaluated from timezone */
} VNum;

static inline VNum vnum_scalar(double val) {
    VNum v;
    v.val = val;
    v.dim = DIM_UNKNOWN;
    v.unit = NULL;
    v.is_pct = false;
    v.is_date = false;
    v.is_time = false;
    v.is_bday = false;
    v.tz[0] = '\0';
    return v;
}

static inline VNum vnum_unit(double count, const UnitItem *u) {
    VNum v;
    v.dim = u ? u->dim : DIM_UNKNOWN;
    v.unit = u;
    v.val = count * (u ? u->to_base : 1.0);
    v.is_pct = false;
    v.is_date = false;
    v.is_time = false;
    v.is_bday = false;
    v.tz[0] = '\0';
    return v;
}

static bool get_variable_vnum(const char *name, VNum *out) {
    if (!name || !*name || !out) return false;
    /* 1. Local stack frames */
    for (int i = g_local_stack_top - 1; i >= 0; i--) {
        if (strcasecmp(g_local_stack[i].name, name) == 0) {
            *out = vnum_scalar(g_local_stack[i].val);
            return true;
        }
    }
    /* 2. Global symbols */
    for (int i = 0; i < g_symbol_count; i++) {
        if (strcasecmp(g_symbols[i].name, name) == 0) {
            CalcSymbol *s = &g_symbols[i];
            if (s->is_func) return false;
            if (s->expr[0] != '\0') {
                if (g_eval_depth >= 8) return false;
                g_eval_depth++;
                double d_val = 0.0;
                char d_unit[32] = "";
                bool d_is_date = false, d_is_time = false;
                char conv_res[64], conv_sym[32], conv_sfx[64];
                const char *c_err = NULL;
                if (try_convert_expression(s->expr, 0.0, &d_val, conv_res, sizeof(conv_res),
                                           conv_sym, sizeof(conv_sym), conv_sfx, sizeof(conv_sfx), &c_err) && !c_err) {
                    const UnitItem *tu = conv_sym[0] ? find_unit(conv_sym) : NULL;
                    s->cached_val = d_val * (tu ? tu->to_base : 1.0);
                    snprintf(s->unit, sizeof(s->unit), "%s", conv_sym);
                    s->is_date = false;
                    s->is_time = false;
                } else if (evaluate_equation_ex(s->expr, 0.0, &d_val, d_unit, sizeof(d_unit), &d_is_date, &d_is_time, NULL)) {
                    s->cached_val = d_val;
                    snprintf(s->unit, sizeof(s->unit), "%s", d_unit);
                    s->is_date = d_is_date;
                    s->is_time = d_is_time;
                }
                g_eval_depth--;
            }
            out->val = s->cached_val;
            out->is_pct = false;
            out->is_date = s->is_date;
            out->is_time = s->is_time;
            out->is_bday = false;
            if (s->is_date && s->unit[0]) {
                snprintf(out->tz, sizeof(out->tz), "%s", s->unit);
            } else {
                out->tz[0] = '\0';
            }
            if (s->unit[0] && !s->is_date) {
                const UnitItem *u = find_unit(s->unit);
                if (u) {
                    out->unit = u;
                    out->dim = u->dim;
                } else {
                    out->unit = NULL;
                    out->dim = DIM_UNKNOWN;
                }
            } else {
                out->unit = NULL;
                out->dim = DIM_UNKNOWN;
            }
            return true;
        }
    }
    return false;
}

static VNum parse_expression(Parser *p);

typedef struct {
    const char *name;
    int month;
} MonthEntry;

static const MonthEntry MONTH_TABLE[] = {
    { "january", 1 }, { "jan", 1 },
    { "february", 2 }, { "feb", 2 },
    { "march", 3 }, { "mar", 3 },
    { "april", 4 }, { "apr", 4 },
    { "may", 5 },
    { "june", 6 }, { "jun", 6 },
    { "july", 7 }, { "jul", 7 },
    { "august", 8 }, { "aug", 8 },
    { "september", 9 }, { "sept", 9 }, { "sep", 9 },
    { "october", 10 }, { "oct", 10 },
    { "november", 11 }, { "nov", 11 },
    { "december", 12 }, { "dec", 12 },
    { NULL, 0 }
};

static int match_month(const char *str, size_t *len_out) {
    if (!str) return 0;
    for (int i = 0; MONTH_TABLE[i].name != NULL; i++) {
        size_t nlen = strlen(MONTH_TABLE[i].name);
        if (strncasecmp(str, MONTH_TABLE[i].name, nlen) == 0) {
            char next = str[nlen];
            if (!isalpha((unsigned char)next)) {
                if (len_out) *len_out = nlen;
                return MONTH_TABLE[i].month;
            }
        }
    }
    return 0;
}

static bool read_year(const char *s, size_t *len_out, int *year_out) {
    if (!s) return false;
    if (isdigit((unsigned char)s[0]) && isdigit((unsigned char)s[1]) &&
        isdigit((unsigned char)s[2]) && isdigit((unsigned char)s[3]) &&
        !isdigit((unsigned char)s[4])) {
        *year_out = (s[0] - '0')*1000 + (s[1] - '0')*100 + (s[2] - '0')*10 + (s[3] - '0');
        *len_out = 4;
        return true;
    }
    if (isdigit((unsigned char)s[0]) && s[1] == ',' &&
        isdigit((unsigned char)s[2]) && isdigit((unsigned char)s[3]) && isdigit((unsigned char)s[4]) &&
        !isdigit((unsigned char)s[5])) {
        *year_out = (s[0] - '0')*1000 + (s[2] - '0')*100 + (s[3] - '0')*10 + (s[4] - '0');
        *len_out = 5;
        return true;
    }
    return false;
}

static bool try_parse_date_literal(Parser *p, double *val_out) {
    const char *s = &p->src[p->pos];
    int y = 0, m = 0, d = 0;
    size_t consumed = 0;

    size_t ylen = 0;
    int yval = 0;

    /* Format 1: YYYY-MM-DD or YYYY/MM/DD */
    if (read_year(s, &ylen, &yval) &&
        (s[ylen] == '-' || s[ylen] == '/') &&
        isdigit((unsigned char)s[ylen + 1]) && isdigit((unsigned char)s[ylen + 2]) &&
        (s[ylen + 3] == '-' || s[ylen + 3] == '/') &&
        isdigit((unsigned char)s[ylen + 4]) && isdigit((unsigned char)s[ylen + 5]) &&
        !isdigit((unsigned char)s[ylen + 6])) {
        y = yval;
        m = (s[ylen + 1] - '0')*10 + (s[ylen + 2] - '0');
        d = (s[ylen + 4] - '0')*10 + (s[ylen + 5] - '0');
        consumed = ylen + 6;
    }
    /* Format 2: YYYY-Mon-DD (e.g. 2026-Sep-10, 2026-Sept-10) */
    else if (read_year(s, &ylen, &yval) &&
             (s[ylen] == '-' || s[ylen] == '/' || s[ylen] == ' ')) {
        size_t mlen = 0;
        int mon = match_month(&s[ylen + 1], &mlen);
        if (mon > 0 && (s[ylen + 1 + mlen] == '-' || s[ylen + 1 + mlen] == '/' || s[ylen + 1 + mlen] == ' ')) {
            size_t dpos = ylen + 1 + mlen + 1;
            if (isdigit((unsigned char)s[dpos])) {
                int day_val = s[dpos] - '0';
                size_t dlen = 1;
                if (isdigit((unsigned char)s[dpos + 1])) {
                    day_val = day_val * 10 + (s[dpos + 1] - '0');
                    dlen = 2;
                }
                if (!isalnum((unsigned char)s[dpos + dlen])) {
                    y = yval;
                    m = mon;
                    d = day_val;
                    consumed = dpos + dlen;
                }
            }
        }
    }
    /* Format 3: DD-Mon-YYYY or DD Mon YYYY (e.g. 10-Sep-2026, 10-Sept-2026, 10 Sept 2026) */
    else if (isdigit((unsigned char)s[0])) {
        int day_val = s[0] - '0';
        size_t dlen = 1;
        if (isdigit((unsigned char)s[1])) {
            day_val = day_val * 10 + (s[1] - '0');
            dlen = 2;
        }
        if (day_val >= 1 && day_val <= 31 && (s[dlen] == '-' || s[dlen] == '/' || s[dlen] == ' ')) {
            size_t mpos = dlen + 1;
            while (s[mpos] == ' ') mpos++;
            size_t mlen = 0;
            int mon = match_month(&s[mpos], &mlen);
            if (mon > 0 && (s[mpos + mlen] == '-' || s[mpos + mlen] == '/' || s[mpos + mlen] == ' ' || s[mpos + mlen] == ',')) {
                size_t ypos = mpos + mlen + 1;
                while (s[ypos] == ' ' || s[ypos] == ',') ypos++;
                size_t yl = 0;
                int yv = 0;
                if (read_year(&s[ypos], &yl, &yv)) {
                    y = yv;
                    m = mon;
                    d = day_val;
                    consumed = ypos + yl;
                }
            }
        }
    }
    /* Format 4: Mon DD YYYY or Mon DD, YYYY (e.g. Sep 10 2026, Sept 10, 2026) */
    else if (isalpha((unsigned char)s[0])) {
        size_t mlen = 0;
        int mon = match_month(s, &mlen);
        if (mon > 0 && (s[mlen] == ' ' || s[mlen] == '-')) {
            size_t dpos = mlen + 1;
            while (s[dpos] == ' ') dpos++;
            if (isdigit((unsigned char)s[dpos])) {
                int day_val = s[dpos] - '0';
                size_t dlen = 1;
                if (isdigit((unsigned char)s[dpos + 1])) {
                    day_val = day_val * 10 + (s[dpos + 1] - '0');
                    dlen = 2;
                }
                if (day_val >= 1 && day_val <= 31 && (s[dpos + dlen] == ' ' || s[dpos + dlen] == ',' || s[dpos + dlen] == '-')) {
                    size_t ypos = dpos + dlen + 1;
                    while (s[ypos] == ' ' || s[ypos] == ',') ypos++;
                    size_t yl = 0;
                    int yv = 0;
                    if (read_year(&s[ypos], &yl, &yv)) {
                        y = yv;
                        m = mon;
                        d = day_val;
                        consumed = ypos + yl;
                    }
                }
            }
        }
    }

    if (consumed == 0 || m < 1 || m > 12 || d < 1 || d > 31 || y < 1970 || y > 2100) {
        return false;
    }

    struct tm tm_d;
    memset(&tm_d, 0, sizeof(tm_d));
    tm_d.tm_year = y - 1900;
    tm_d.tm_mon = m - 1;
    tm_d.tm_mday = d;
    tm_d.tm_hour = 0;
    tm_d.tm_min = 0;
    tm_d.tm_sec = 0;
    tm_d.tm_isdst = -1;

    /* Optional Time HH:MM[:SS] */
    if (s[consumed] == ' ' || s[consumed] == 'T') {
        size_t tpos = consumed + 1;
        while (s[tpos] == ' ') tpos++;
        if (isdigit((unsigned char)s[tpos]) && isdigit((unsigned char)s[tpos+1]) &&
            s[tpos+2] == ':' &&
            isdigit((unsigned char)s[tpos+3]) && isdigit((unsigned char)s[tpos+4])) {
            int hh = (s[tpos] - '0')*10 + (s[tpos+1] - '0');
            int mm = (s[tpos+3] - '0')*10 + (s[tpos+4] - '0');
            int ss = 0;
            tpos += 5;
            if (s[tpos] == ':' && isdigit((unsigned char)s[tpos+1]) && isdigit((unsigned char)s[tpos+2])) {
                ss = (s[tpos+1] - '0')*10 + (s[tpos+2] - '0');
                tpos += 3;
            }
            if (hh >= 0 && hh <= 23 && mm >= 0 && mm <= 59 && ss >= 0 && ss <= 60) {
                tm_d.tm_hour = hh;
                tm_d.tm_min = mm;
                tm_d.tm_sec = ss;
                consumed = tpos;
            }
        }
    }

    time_t epoch = mktime(&tm_d);
    *val_out = (double)epoch;
    p->pos += consumed;
    return true;
}

static bool try_parse_time_literal(Parser *p, double *val_out) {
    const char *s = &p->src[p->pos];
    if (!isdigit((unsigned char)*s)) return false;

    size_t i = 0;
    int num1 = 0;
    while (isdigit((unsigned char)s[i])) {
        num1 = num1 * 10 + (s[i] - '0');
        i++;
        if (i > 4) return false;
    }

    int hh = -1;
    int mm = 0;
    int ss = 0;
    bool is_12h = false;
    bool is_pm = false;

    if (s[i] == ':') {
        hh = num1;
        i++; /* skip ':' */
        if (!isdigit((unsigned char)s[i])) return false;
        size_t m_start = i;
        while (isdigit((unsigned char)s[i])) {
            mm = mm * 10 + (s[i] - '0');
            i++;
        }
        if (i - m_start < 1 || i - m_start > 2) return false;

        /* Optional seconds :SS */
        if (s[i] == ':') {
            i++;
            if (!isdigit((unsigned char)s[i])) return false;
            size_t s_start = i;
            while (isdigit((unsigned char)s[i])) {
                ss = ss * 10 + (s[i] - '0');
                i++;
            }
            if (i - s_start < 1 || i - s_start > 2) return false;
        }

        /* Optional trailing am/pm */
        size_t sp = i;
        while (s[sp] == ' ') sp++;
        if ((s[sp] == 'a' || s[sp] == 'A') && (s[sp+1] == 'm' || s[sp+1] == 'M')) {
            if (!isalnum((unsigned char)s[sp+2])) {
                is_12h = true;
                is_pm = false;
                i = sp + 2;
            }
        } else if ((s[sp] == 'p' || s[sp] == 'P') && (s[sp+1] == 'm' || s[sp+1] == 'M')) {
            if (!isalnum((unsigned char)s[sp+2])) {
                is_12h = true;
                is_pm = true;
                i = sp + 2;
            }
        }
    } else {
        /* No colon: check for am / pm directly or with space e.g. 5pm, 5 pm, 10am */
        size_t sp = i;
        while (s[sp] == ' ') sp++;
        if ((s[sp] == 'a' || s[sp] == 'A') && (s[sp+1] == 'm' || s[sp+1] == 'M')) {
            if (!isalnum((unsigned char)s[sp+2])) {
                hh = num1;
                is_12h = true;
                is_pm = false;
                i = sp + 2;
            }
        } else if ((s[sp] == 'p' || s[sp] == 'P') && (s[sp+1] == 'm' || s[sp+1] == 'M')) {
            if (!isalnum((unsigned char)s[sp+2])) {
                hh = num1;
                is_12h = true;
                is_pm = true;
                i = sp + 2;
            }
        }
    }

    if (hh < 0) return false;

    if (is_12h) {
        if (hh < 1 || hh > 12) return false;
        if (mm < 0 || mm > 59) return false;
        if (ss < 0 || ss > 59) return false;
        if (hh == 12) {
            hh = is_pm ? 12 : 0;
        } else if (is_pm) {
            hh += 12;
        }
    } else {
        if (hh < 0 || hh > 23) return false;
        if (mm < 0 || mm > 59) return false;
        if (ss < 0 || ss > 59) return false;
    }

    /* Convert to today's local epoch time */
    time_t now = time(NULL);
    struct tm tm_l;
    localtime_r(&now, &tm_l);
    tm_l.tm_hour = hh;
    tm_l.tm_min = mm;
    tm_l.tm_sec = ss;
    tm_l.tm_isdst = -1;

    time_t epoch = mktime(&tm_l);
    if (epoch == (time_t)-1) return false;

    *val_out = (double)epoch;
    p->pos += i;
    return true;
}

static bool is_multi_arg_function_name(const char *name) {
    if (!name) return false;
    if (strcasecmp(name, "avg") == 0 || strcasecmp(name, "average") == 0 ||
        strcasecmp(name, "median") == 0 || strcasecmp(name, "min") == 0 ||
        strcasecmp(name, "max") == 0 || strcasecmp(name, "pow") == 0 ||
        strcasecmp(name, "mod") == 0 || strcasecmp(name, "xor") == 0 ||
        strcasecmp(name, "round") == 0 || strcasecmp(name, "emi") == 0 ||
        strcasecmp(name, "sip") == 0 || strcasecmp(name, "cagr") == 0 ||
        strcasecmp(name, "compound") == 0 || strcasecmp(name, "discount") == 0 ||
        strcasecmp(name, "markup") == 0) {
        return true;
    }
    for (int i = 0; i < g_symbol_count; i++) {
        if (g_symbols[i].is_func && strcasecmp(g_symbols[i].name, name) == 0) {
            return (g_symbols[i].param_count > 1);
        }
    }
    return false;
}

static bool is_valid_thousands_separator(const char *src, size_t pos) {
    if (src[pos] != ',') return false;
    if (!isdigit((unsigned char)src[pos + 1])) return false;

    size_t k = pos + 1;
    size_t d_count = 0;
    while (isdigit((unsigned char)src[k + d_count])) d_count++;

    if (d_count == 3) {
        char next_ch = src[k + 3];
        if (next_ch == ',' || next_ch == '.' || next_ch == '\0' ||
            isspace((unsigned char)next_ch) ||
            next_ch == ')' || next_ch == '+' || next_ch == '-' ||
            next_ch == '*' || next_ch == '/' || next_ch == '^' || next_ch == '%') {
            return true;
        }
    } else if (d_count == 2) {
        if (src[k + 2] == ',') {
            size_t next_k = k + 3;
            size_t next_d = 0;
            while (isdigit((unsigned char)src[next_k + next_d])) next_d++;
            if (next_d == 3 || next_d == 2) {
                return true;
            }
        }
    }
    return false;
}

static VNum parse_factor(Parser *p) {
    skip_whitespace(p);
    char c = peek(p);

    if (c == '+') {
        get_char(p);
        return parse_factor(p);
    }
    if (c == '-') {
        get_char(p);
        VNum sub = parse_factor(p);
        sub.val = -sub.val;
        return sub;
    }
    if (c == '~') {
        get_char(p);
        VNum sub = parse_factor(p);
        if (sub.dim != DIM_UNKNOWN) {
            p->has_error = true;
            p->error_msg = "bitwise op on unit not supported";
            return sub;
        }
        sub.val = (double)(~(int64_t)round(sub.val));
        return sub;
    }

    double date_lit_val = 0.0;
    if (try_parse_date_literal(p, &date_lit_val)) {
        VNum v = vnum_scalar(date_lit_val);
        v.is_date = true;
        return v;
    }

    double time_lit_val = 0.0;
    if (try_parse_time_literal(p, &time_lit_val)) {
        VNum v = vnum_scalar(time_lit_val);
        v.is_time = true;
        return v;
    }

    VNum val = vnum_scalar(0.0);

    if (c == '(') {
        get_char(p);
        /* Check if parenthesized content is a unit conversion */
        size_t start_pos = p->pos;
        int depth = 1;
        size_t end_pos = start_pos;
        while (p->src[end_pos] && depth > 0) {
            if (p->src[end_pos] == '(') depth++;
            else if (p->src[end_pos] == ')') depth--;
            if (depth == 0) break;
            end_pos++;
        }

        bool handled_as_conv = false;
        if (depth == 0 && end_pos > start_pos) {
            size_t sub_len = end_pos - start_pos;
            if (sub_len < 256) {
                char sub[256];
                memcpy(sub, p->src + start_pos, sub_len);
                sub[sub_len] = '\0';
                if (strstr(sub, " to ") || strstr(sub, " TO ") || strstr(sub, " To ") ||
                    strstr(sub, " in ") || strstr(sub, " IN ") || strstr(sub, " In ") ||
                    strcasestr(sub, " until ") || strcasestr(sub, " since ") ||
                    strcasestr(sub, " before ") || strcasestr(sub, " after ") ||
                    strcasestr(sub, " between ")) {
                    double conv_val = 0.0;
                    char dummy_res[64];
                    char dummy_unit[32];
                    char dummy_sfx[64];
                    const char *conv_err = NULL;
                    if (try_convert_expression(sub, p->ans_val, &conv_val, dummy_res, sizeof(dummy_res),
                                               dummy_unit, sizeof(dummy_unit),
                                               dummy_sfx, sizeof(dummy_sfx), &conv_err) && !conv_err) {
                        val = vnum_scalar(conv_val);
                        if (dummy_unit[0]) {
                            const UnitItem *cu = find_unit(dummy_unit);
                            if (cu) {
                                val.dim = cu->dim;
                                val.unit = cu;
                                val.val = conv_val * cu->to_base;
                            } else if (is_valid_timezone_query(dummy_unit) || conv_val > 100000000.0) {
                                val.is_date = true;
                                snprintf(val.tz, sizeof(val.tz), "%s", dummy_unit);
                            }
                        } else if (conv_val > 100000000.0) {
                            val.is_date = true;
                        }
                        p->pos = end_pos + 1; /* consume past matching ')' */
                        handled_as_conv = true;
                    }
                }
            }
        }

        if (!handled_as_conv) {
            val = parse_expression(p);
            skip_whitespace(p);
            if (peek(p) == ')') {
                get_char(p);
            } else if (peek(p) == '\0') {
                /* Intelligent bracket auto-close at end of expression */
            } else {
                p->has_error = true;
                p->error_msg = "unclosed (";
            }
        }
    } else if (isalpha((unsigned char)c)) {
        /* Read identifier (function, constant, variable, magnitude, time unit) */
        char name[32];
        size_t nlen = 0;
        while ((isalnum((unsigned char)p->src[p->pos]) || p->src[p->pos] == '_') && nlen < sizeof(name) - 1) {
            name[nlen++] = (char)tolower((unsigned char)p->src[p->pos++]);
        }
        name[nlen] = '\0';

        if (strcmp(name, "pi") == 0) {
            val = vnum_scalar(M_PI);
        } else if (strcmp(name, "e") == 0) {
            val = vnum_scalar(M_E);
        } else if (strcmp(name, "tau") == 0) {
            val = vnum_scalar(2.0 * M_PI);
        } else if (strcmp(name, "phi") == 0) {
            val = vnum_scalar(1.61803398874989484820);
        } else if (strcmp(name, "ans") == 0) {
            val = vnum_scalar(p->ans_val);
            if (g_ans_is_date) val.is_date = true;
            if (g_ans_is_time) val.is_time = true;
        } else if (strcmp(name, "now") == 0) {
            val = vnum_scalar((double)time(NULL));
            val.is_date = true;
        } else if (strcmp(name, "time") == 0) {
            val = vnum_scalar((double)time(NULL));
            val.is_time = true;
        } else if (strcmp(name, "today") == 0) {
            time_t t = time(NULL);
            struct tm tm_local;
            localtime_r(&t, &tm_local);
            tm_local.tm_hour = 0;
            tm_local.tm_min = 0;
            tm_local.tm_sec = 0;
            tm_local.tm_isdst = -1;
            val = vnum_scalar((double)mktime(&tm_local));
            val.is_date = true;
        } else if (strcmp(name, "tomorrow") == 0) {
            time_t t = time(NULL);
            struct tm tm_local;
            localtime_r(&t, &tm_local);
            tm_local.tm_hour = 0;
            tm_local.tm_min = 0;
            tm_local.tm_sec = 0;
            tm_local.tm_isdst = -1;
            tm_local.tm_mday += 1;
            val = vnum_scalar((double)mktime(&tm_local));
            val.is_date = true;
        } else if (strcmp(name, "yesterday") == 0) {
            time_t t = time(NULL);
            struct tm tm_local;
            localtime_r(&t, &tm_local);
            tm_local.tm_hour = 0;
            tm_local.tm_min = 0;
            tm_local.tm_sec = 0;
            tm_local.tm_isdst = -1;
            tm_local.tm_mday -= 1;
            val = vnum_scalar((double)mktime(&tm_local));
            val.is_date = true;
        } else if (strcmp(name, "next") == 0) {
            skip_whitespace(p);
            time_t t = time(NULL);
            struct tm tm_local;
            localtime_r(&t, &tm_local);
            tm_local.tm_hour = 0;
            tm_local.tm_min = 0;
            tm_local.tm_sec = 0;
            tm_local.tm_isdst = -1;
            if (strncasecmp(&p->src[p->pos], "month", 5) == 0 &&
                !isalnum((unsigned char)p->src[p->pos + 5]) && p->src[p->pos + 5] != '_') {
                p->pos += 5;
                tm_local.tm_mon += 1;
                tm_local.tm_mday = 1;
                val = vnum_scalar((double)mktime(&tm_local));
                val.is_date = true;
            } else if (strncasecmp(&p->src[p->pos], "year", 4) == 0 &&
                       !isalnum((unsigned char)p->src[p->pos + 4]) && p->src[p->pos + 4] != '_') {
                p->pos += 4;
                tm_local.tm_year += 1;
                tm_local.tm_mon = 0;
                tm_local.tm_mday = 1;
                val = vnum_scalar((double)mktime(&tm_local));
                val.is_date = true;
            } else if (strncasecmp(&p->src[p->pos], "week", 4) == 0 &&
                       !isalnum((unsigned char)p->src[p->pos + 4]) && p->src[p->pos + 4] != '_') {
                p->pos += 4;
                tm_local.tm_mday += 7;
                val = vnum_scalar((double)mktime(&tm_local));
                val.is_date = true;
            } else {
                p->has_error = true;
                p->error_msg = "expected 'month', 'year', or 'week' after 'next'";
                return vnum_scalar(0.0);
            }
        } else if (strcmp(name, "end") == 0) {
            skip_whitespace(p);
            if (strncasecmp(&p->src[p->pos], "of", 2) == 0 &&
                !isalnum((unsigned char)p->src[p->pos + 2]) && p->src[p->pos + 2] != '_') {
                p->pos += 2;
                skip_whitespace(p);
            }
            time_t t = time(NULL);
            struct tm tm_local;
            localtime_r(&t, &tm_local);
            tm_local.tm_hour = 0;
            tm_local.tm_min = 0;
            tm_local.tm_sec = 0;
            tm_local.tm_isdst = -1;
            if (strncasecmp(&p->src[p->pos], "month", 5) == 0 &&
                !isalnum((unsigned char)p->src[p->pos + 5]) && p->src[p->pos + 5] != '_') {
                p->pos += 5;
                tm_local.tm_mon += 1;
                tm_local.tm_mday = 0; /* Last day of current month */
                val = vnum_scalar((double)mktime(&tm_local));
                val.is_date = true;
            } else if (strncasecmp(&p->src[p->pos], "year", 4) == 0 &&
                       !isalnum((unsigned char)p->src[p->pos + 4]) && p->src[p->pos + 4] != '_') {
                p->pos += 4;
                tm_local.tm_mon = 11;
                tm_local.tm_mday = 31;
                val = vnum_scalar((double)mktime(&tm_local));
                val.is_date = true;
            } else {
                p->has_error = true;
                p->error_msg = "expected 'month' or 'year' after 'end of'";
                return vnum_scalar(0.0);
            }
        } else if (strcmp(name, "start") == 0) {
            skip_whitespace(p);
            if (strncasecmp(&p->src[p->pos], "of", 2) == 0 &&
                !isalnum((unsigned char)p->src[p->pos + 2]) && p->src[p->pos + 2] != '_') {
                p->pos += 2;
                skip_whitespace(p);
            }
            time_t t = time(NULL);
            struct tm tm_local;
            localtime_r(&t, &tm_local);
            tm_local.tm_hour = 0;
            tm_local.tm_min = 0;
            tm_local.tm_sec = 0;
            tm_local.tm_isdst = -1;
            if (strncasecmp(&p->src[p->pos], "month", 5) == 0 &&
                !isalnum((unsigned char)p->src[p->pos + 5]) && p->src[p->pos + 5] != '_') {
                p->pos += 5;
                tm_local.tm_mday = 1;
                val = vnum_scalar((double)mktime(&tm_local));
                val.is_date = true;
            } else if (strncasecmp(&p->src[p->pos], "year", 4) == 0 &&
                       !isalnum((unsigned char)p->src[p->pos + 4]) && p->src[p->pos + 4] != '_') {
                p->pos += 4;
                tm_local.tm_mon = 0;
                tm_local.tm_mday = 1;
                val = vnum_scalar((double)mktime(&tm_local));
                val.is_date = true;
            } else {
                p->has_error = true;
                p->error_msg = "expected 'month' or 'year' after 'start of'";
                return vnum_scalar(0.0);
            }
        } else if (strcmp(name, "k") == 0 || strcmp(name, "thousand") == 0 || strcmp(name, "thousands") == 0) {
            val = vnum_scalar(1e3);
        } else if (strcmp(name, "million") == 0 || strcmp(name, "millions") == 0) {
            val = vnum_scalar(1e6);
        } else if (strcmp(name, "billion") == 0 || strcmp(name, "billions") == 0) {
            val = vnum_scalar(1e9);
        } else if (strcmp(name, "trillion") == 0 || strcmp(name, "trillions") == 0) {
            val = vnum_scalar(1e12);
        } else if (strcmp(name, "lakh") == 0 || strcmp(name, "lakhs") == 0 || strcmp(name, "lac") == 0 || strcmp(name, "lacs") == 0) {
            val = vnum_scalar(1e5);
        } else if (strcmp(name, "crore") == 0 || strcmp(name, "crores") == 0 || strcmp(name, "cr") == 0) {
            val = vnum_scalar(1e7);
        } else if (get_variable_vnum(name, &val)) {
            /* val is already assigned with magnitude, unit, date, or time */
        } else if (strcmp(name, "bday") == 0 || strcmp(name, "bdays") == 0 ||
                   strcmp(name, "bizday") == 0 || strcmp(name, "bizdays") == 0 ||
                   strcmp(name, "workday") == 0 || strcmp(name, "workdays") == 0) {
            val = vnum_scalar(1.0);
            val.is_bday = true;
            val.dim = DIM_TIME;
        } else if (strcmp(name, "business") == 0 || strcmp(name, "work") == 0) {
            skip_whitespace(p);
            if (strncasecmp(&p->src[p->pos], "days", 4) == 0 && !isalnum((unsigned char)p->src[p->pos+4]) && p->src[p->pos+4] != '_') {
                p->pos += 4;
                val = vnum_scalar(1.0);
                val.is_bday = true;
                val.dim = DIM_TIME;
            } else if (strncasecmp(&p->src[p->pos], "day", 3) == 0 && !isalnum((unsigned char)p->src[p->pos+3]) && p->src[p->pos+3] != '_') {
                p->pos += 3;
                val = vnum_scalar(1.0);
                val.is_bday = true;
                val.dim = DIM_TIME;
            } else {
                p->has_error = true;
                p->error_msg = "unknown symbol";
                return vnum_scalar(0.0);
            }
        } else {
            skip_whitespace(p);
            bool is_func_call = (peek(p) == '(');
            const UnitItem *u = is_func_call ? NULL : find_unit(name);
            if (u) {
                val.val = u->to_base;
                val.dim = u->dim;
                val.unit = u;
                val.is_pct = false;
                val.is_date = false;
                val.is_time = false;
            } else {
                /* Function call with 1 to 64 arguments */
                if (peek(p) != '(') {
                    p->has_error = true;
                    p->error_msg = "unknown symbol";
                    return vnum_scalar(0.0);
                }
                get_char(p); /* consume '(' */
                VNum v_args[64];
                double args[64];
                int argc = 0;
                bool is_multi = is_multi_arg_function_name(name);
                skip_whitespace(p);
                if (peek(p) != ')') {
                    while (1) {
                        if (argc >= 64) {
                            p->has_error = true;
                            p->error_msg = "too many args";
                            return vnum_scalar(0.0);
                        }
                        if (is_multi) p->in_multi_arg_func++;
                        VNum arg_v = parse_expression(p);
                        if (is_multi) p->in_multi_arg_func--;
                        v_args[argc] = arg_v;
                        args[argc] = arg_v.val;
                        argc++;
                        skip_whitespace(p);
                        if (peek(p) == ',') {
                            get_char(p);
                            skip_whitespace(p);
                        } else {
                            break;
                        }
                    }
                }
            skip_whitespace(p);
            if (peek(p) == ')') {
                get_char(p);
            } else if (peek(p) == '\0') {
                /* Intelligent bracket auto-close at end of expression */
            } else {
                p->has_error = true;
                p->error_msg = "unclosed (";
                return vnum_scalar(0.0);
            }

            double f_val = 0.0;
            if (strcmp(name, "sqrt") == 0) {
                if (argc != 1) { p->has_error = true; p->error_msg = "sqrt needs 1 arg"; return vnum_scalar(0.0); }
                if (args[0] < 0.0) { p->has_error = true; p->error_msg = "sqrt neg"; return vnum_scalar(0.0); }
                f_val = sqrt(args[0]);
            } else if (strcmp(name, "cbrt") == 0) {
                if (argc != 1) { p->has_error = true; p->error_msg = "cbrt needs 1 arg"; return vnum_scalar(0.0); }
                f_val = cbrt(args[0]);
            } else if (strcmp(name, "sin") == 0) {
                if (argc != 1) { p->has_error = true; p->error_msg = "sin needs 1 arg"; return vnum_scalar(0.0); }
                f_val = sin(args[0]);
            } else if (strcmp(name, "cos") == 0) {
                if (argc != 1) { p->has_error = true; p->error_msg = "cos needs 1 arg"; return vnum_scalar(0.0); }
                f_val = cos(args[0]);
            } else if (strcmp(name, "tan") == 0) {
                if (argc != 1) { p->has_error = true; p->error_msg = "tan needs 1 arg"; return vnum_scalar(0.0); }
                f_val = tan(args[0]);
            } else if (strcmp(name, "asin") == 0) {
                if (argc != 1) { p->has_error = true; p->error_msg = "asin needs 1 arg"; return vnum_scalar(0.0); }
                f_val = asin(args[0]);
            } else if (strcmp(name, "acos") == 0) {
                if (argc != 1) { p->has_error = true; p->error_msg = "acos needs 1 arg"; return vnum_scalar(0.0); }
                f_val = acos(args[0]);
            } else if (strcmp(name, "atan") == 0) {
                if (argc != 1) { p->has_error = true; p->error_msg = "atan needs 1 arg"; return vnum_scalar(0.0); }
                f_val = atan(args[0]);
            } else if (strcmp(name, "sinh") == 0) {
                if (argc != 1) { p->has_error = true; p->error_msg = "sinh needs 1 arg"; return vnum_scalar(0.0); }
                f_val = sinh(args[0]);
            } else if (strcmp(name, "cosh") == 0) {
                if (argc != 1) { p->has_error = true; p->error_msg = "cosh needs 1 arg"; return vnum_scalar(0.0); }
                f_val = cosh(args[0]);
            } else if (strcmp(name, "tanh") == 0) {
                if (argc != 1) { p->has_error = true; p->error_msg = "tanh needs 1 arg"; return vnum_scalar(0.0); }
                f_val = tanh(args[0]);
            } else if (strcmp(name, "abs") == 0) {
                if (argc != 1) { p->has_error = true; p->error_msg = "abs needs 1 arg"; return vnum_scalar(0.0); }
                f_val = fabs(args[0]);
            } else if (strcmp(name, "ceil") == 0) {
                if (argc != 1) { p->has_error = true; p->error_msg = "ceil needs 1 arg"; return vnum_scalar(0.0); }
                f_val = ceil(args[0]);
            } else if (strcmp(name, "floor") == 0) {
                if (argc != 1) { p->has_error = true; p->error_msg = "floor needs 1 arg"; return vnum_scalar(0.0); }
                f_val = floor(args[0]);
            } else if (strcmp(name, "round") == 0) {
                if (argc < 1 || argc > 2) { p->has_error = true; p->error_msg = "round needs 1 or 2 args"; return vnum_scalar(0.0); }
                if (argc == 1) {
                    f_val = round(args[0]);
                } else {
                    double factor = pow(10.0, args[1]);
                    f_val = round(args[0] * factor) / factor;
                }
            } else if (strcmp(name, "log") == 0) {
                if (argc != 1) { p->has_error = true; p->error_msg = "log needs 1 arg"; return vnum_scalar(0.0); }
                if (args[0] <= 0.0) { p->has_error = true; p->error_msg = "log non-pos"; return vnum_scalar(0.0); }
                f_val = log10(args[0]);
            } else if (strcmp(name, "ln") == 0) {
                if (argc != 1) { p->has_error = true; p->error_msg = "ln needs 1 arg"; return vnum_scalar(0.0); }
                if (args[0] <= 0.0) { p->has_error = true; p->error_msg = "ln non-pos"; return vnum_scalar(0.0); }
                f_val = log(args[0]);
            } else if (strcmp(name, "exp") == 0) {
                if (argc != 1) { p->has_error = true; p->error_msg = "exp needs 1 arg"; return vnum_scalar(0.0); }
                f_val = exp(args[0]);
            } else if (strcmp(name, "xor") == 0) {
                if (argc != 2) { p->has_error = true; p->error_msg = "xor(a, b) needs 2 args"; return vnum_scalar(0.0); }
                f_val = (double)((int64_t)round(args[0]) ^ (int64_t)round(args[1]));
            } else if (strcmp(name, "mod") == 0) {
                if (argc != 2) { p->has_error = true; p->error_msg = "mod(a, b) needs 2 args"; return vnum_scalar(0.0); }
                if (args[1] == 0.0) { p->has_error = true; p->error_msg = "mod by 0"; return vnum_scalar(0.0); }
                f_val = fmod(args[0], args[1]);
            } else if (strcmp(name, "pow") == 0) {
                if (argc != 2) { p->has_error = true; p->error_msg = "pow(a, b) needs 2 args"; return vnum_scalar(0.0); }
                f_val = pow(args[0], args[1]);
            } else if (strcmp(name, "emi") == 0) {
                if (argc != 3) { p->has_error = true; p->error_msg = "emi(P, r, n) needs 3 args"; return vnum_scalar(0.0); }
                double P = args[0];
                double r = args[1];
                if (r > 1.0) r /= 100.0;
                double n = args[2];
                if (n <= 0.0) { p->has_error = true; p->error_msg = "tenure n must be > 0"; return vnum_scalar(0.0); }
                double R = r / 12.0;
                if (R == 0.0) {
                    f_val = P / n;
                } else {
                    double factor = pow(1.0 + R, n);
                    f_val = P * R * factor / (factor - 1.0);
                }
            } else if (strcmp(name, "cagr") == 0) {
                if (argc != 3) { p->has_error = true; p->error_msg = "cagr(v_i, v_f, y) needs 3 args"; return vnum_scalar(0.0); }
                double vi = args[0];
                double vf = args[1];
                double y = args[2];
                if (vi <= 0.0 || vf <= 0.0 || y <= 0.0) { p->has_error = true; p->error_msg = "cagr args must be > 0"; return vnum_scalar(0.0); }
                f_val = (pow(vf / vi, 1.0 / y) - 1.0) * 100.0;
            } else if (strcmp(name, "sip") == 0) {
                if (argc != 3) { p->has_error = true; p->error_msg = "sip(P, r, y) needs 3 args"; return vnum_scalar(0.0); }
                double P = args[0];
                double r = args[1];
                if (r > 1.0) r /= 100.0;
                double y = args[2];
                double i = r / 12.0;
                double n = y * 12.0;
                if (i == 0.0) {
                    f_val = P * n;
                } else {
                    f_val = P * ((pow(1.0 + i, n) - 1.0) / i) * (1.0 + i);
                }
            } else if (strcmp(name, "compound") == 0) {
                if (argc < 3 || argc > 4) { p->has_error = true; p->error_msg = "compound(P, r, y[, n]) needs 3 or 4 args"; return vnum_scalar(0.0); }
                double P = args[0];
                double r = args[1];
                if (r > 1.0) r /= 100.0;
                double y = args[2];
                double n = (argc == 4) ? args[3] : 1.0;
                if (n <= 0.0) { p->has_error = true; p->error_msg = "frequency n must be > 0"; return vnum_scalar(0.0); }
                f_val = P * pow(1.0 + r / n, n * y);
            } else if (strcmp(name, "discount") == 0) {
                if (argc != 2) { p->has_error = true; p->error_msg = "discount(price, pct) needs 2 args"; return vnum_scalar(0.0); }
                double price = args[0];
                double pct = args[1];
                if (pct > 1.0) pct /= 100.0;
                f_val = price * (1.0 - pct);
            } else if (strcmp(name, "markup") == 0) {
                if (argc != 2) { p->has_error = true; p->error_msg = "markup(cost, margin) needs 2 args"; return vnum_scalar(0.0); }
                double cost = args[0];
                double margin = args[1];
                if (margin > 1.0) margin /= 100.0;
                f_val = cost * (1.0 + margin);
            } else if (strcmp(name, "avg") == 0 || strcmp(name, "average") == 0) {
                if (argc < 1) { p->has_error = true; p->error_msg = "avg needs at least 1 arg"; return vnum_scalar(0.0); }
                double sum = 0.0;
                for (int i = 0; i < argc; i++) sum += args[i];
                f_val = sum / (double)argc;
            } else if (strcmp(name, "median") == 0) {
                if (argc < 1) { p->has_error = true; p->error_msg = "median needs at least 1 arg"; return vnum_scalar(0.0); }
                double sorted[64];
                for (int i = 0; i < argc; i++) sorted[i] = args[i];
                for (int i = 0; i < argc - 1; i++) {
                    for (int j = 0; j < argc - 1 - i; j++) {
                        if (sorted[j] > sorted[j + 1]) {
                            double tmp = sorted[j];
                            sorted[j] = sorted[j + 1];
                            sorted[j + 1] = tmp;
                        }
                    }
                }
                if (argc % 2 == 1) {
                    f_val = sorted[argc / 2];
                } else {
                    f_val = (sorted[argc / 2 - 1] + sorted[argc / 2]) / 2.0;
                }
            } else if (strcmp(name, "min") == 0) {
                if (argc < 1) { p->has_error = true; p->error_msg = "min needs at least 1 arg"; return vnum_scalar(0.0); }
                double min_v = args[0];
                for (int i = 1; i < argc; i++) {
                    if (args[i] < min_v) min_v = args[i];
                }
                f_val = min_v;
            } else if (strcmp(name, "max") == 0) {
                if (argc < 1) { p->has_error = true; p->error_msg = "max needs at least 1 arg"; return vnum_scalar(0.0); }
                double max_v = args[0];
                for (int i = 1; i < argc; i++) {
                    if (args[i] > max_v) max_v = args[i];
                }
                f_val = max_v;
            } else {
                CalcSymbol *user_fn = NULL;
                for (int fi = 0; fi < g_symbol_count; fi++) {
                    if (g_symbols[fi].is_func && strcasecmp(g_symbols[fi].name, name) == 0) {
                        user_fn = &g_symbols[fi];
                        break;
                    }
                }
                if (user_fn) {
                    if (argc != user_fn->param_count) {
                        p->has_error = true;
                        static char err_buf[64];
                        snprintf(err_buf, sizeof(err_buf), "%s requires %d arguments", name, user_fn->param_count);
                        p->error_msg = err_buf;
                        return vnum_scalar(0.0);
                    }
                    if (g_func_call_depth >= 8) {
                        p->has_error = true;
                        p->error_msg = "maximum function recursion depth exceeded";
                        return vnum_scalar(0.0);
                    }
                    if (g_local_stack_top + argc > MAX_LOCAL_STACK) {
                        p->has_error = true;
                        p->error_msg = "call stack overflow";
                        return vnum_scalar(0.0);
                    }
                    int prev_top = g_local_stack_top;
                    for (int i = 0; i < argc; i++) {
                        snprintf(g_local_stack[g_local_stack_top].name, sizeof(g_local_stack[g_local_stack_top].name), "%s", user_fn->params[i]);
                        g_local_stack[g_local_stack_top].val = args[i];
                        g_local_stack_top++;
                    }
                    g_func_call_depth++;
                    double fn_res = 0.0;
                    const char *fn_err = NULL;
                    bool ok = evaluate_equation_ex(user_fn->expr, 0.0, &fn_res, NULL, 0, NULL, NULL, &fn_err);
                    g_func_call_depth--;
                    g_local_stack_top = prev_top;
                    if (!ok) {
                        p->has_error = true;
                        p->error_msg = fn_err ? fn_err : "error evaluating function body";
                        return vnum_scalar(0.0);
                    }
                    f_val = fn_res;
                } else {
                    p->has_error = true;
                    p->error_msg = "unknown func";
                    return vnum_scalar(0.0);
                }
            }
            if (argc > 0 && v_args[0].unit != NULL) {
                val = v_args[0];
                val.val = f_val;
            } else {
                val = vnum_scalar(f_val);
            }
        }
    }
} else if (isdigit((unsigned char)c) || c == '.') {
        double d_val = 0.0;
        /* 1. Radix Literals: 0x, 0b, 0o */
        if (p->src[p->pos] == '0' && (p->src[p->pos+1] == 'x' || p->src[p->pos+1] == 'X')) {
            p->pos += 2;
            char *endptr = NULL;
            unsigned long long uval = strtoull(&p->src[p->pos], &endptr, 16);
            if (endptr == &p->src[p->pos]) {
                p->has_error = true;
                p->error_msg = "invalid hex";
                return vnum_scalar(0.0);
            }
            p->pos += (size_t)(endptr - &p->src[p->pos]);
            d_val = (double)uval;
        } else if (p->src[p->pos] == '0' && (p->src[p->pos+1] == 'b' || p->src[p->pos+1] == 'B')) {
            p->pos += 2;
            char *endptr = NULL;
            unsigned long long uval = strtoull(&p->src[p->pos], &endptr, 2);
            if (endptr == &p->src[p->pos]) {
                p->has_error = true;
                p->error_msg = "invalid bin";
                return vnum_scalar(0.0);
            }
            p->pos += (size_t)(endptr - &p->src[p->pos]);
            d_val = (double)uval;
        } else if (p->src[p->pos] == '0' && (p->src[p->pos+1] == 'o' || p->src[p->pos+1] == 'O')) {
            p->pos += 2;
            char *endptr = NULL;
            unsigned long long uval = strtoull(&p->src[p->pos], &endptr, 8);
            if (endptr == &p->src[p->pos]) {
                p->has_error = true;
                p->error_msg = "invalid oct";
                return vnum_scalar(0.0);
            }
            p->pos += (size_t)(endptr - &p->src[p->pos]);
            d_val = (double)uval;
        } else {
            /* 2. Decimal number */
            char num_clean[64];
            size_t n_idx = 0;
            bool has_dot = false;
            bool has_exp = false;

            while (p->src[p->pos]) {
                char ch = p->src[p->pos];
                if (isdigit((unsigned char)ch)) {
                    if (n_idx < sizeof(num_clean) - 1) num_clean[n_idx++] = ch;
                    p->pos++;
                } else if (ch == ',' && p->in_multi_arg_func == 0 && is_valid_thousands_separator(p->src, p->pos)) {
                    /* Ignore comma between digits for verified thousands separator */
                    p->pos++;
                } else if (ch == '.' && !has_dot && !has_exp) {
                    has_dot = true;
                    if (n_idx < sizeof(num_clean) - 1) num_clean[n_idx++] = ch;
                    p->pos++;
                } else if ((ch == 'e' || ch == 'E') && !has_exp && n_idx > 0) {
                    char next1 = p->src[p->pos + 1];
                    char next2 = next1 ? p->src[p->pos + 2] : '\0';
                    if (isdigit((unsigned char)next1) ||
                        ((next1 == '+' || next1 == '-') && isdigit((unsigned char)next2))) {
                        has_exp = true;
                        if (n_idx < sizeof(num_clean) - 1) num_clean[n_idx++] = ch;
                        p->pos++;
                        if (next1 == '+' || next1 == '-') {
                            if (n_idx < sizeof(num_clean) - 1) num_clean[n_idx++] = next1;
                            p->pos++;
                        }
                    } else {
                        break;
                    }
                } else {
                    break;
                }
            }
            num_clean[n_idx] = '\0';
            char *endptr = NULL;
            d_val = strtod(num_clean, &endptr);
            if (endptr == num_clean) {
                p->has_error = true;
                return vnum_scalar(0.0);
            }
        }
        val = vnum_scalar(d_val);
    } else {
        p->has_error = true;
        return vnum_scalar(0.0);
    }

    /* Check for trailing '%' */
    skip_whitespace(p);
    if (peek(p) == '%') {
        size_t next_idx = p->pos + 1;
        while (p->src[next_idx] && isspace((unsigned char)p->src[next_idx])) next_idx++;
        char after_pct = p->src[next_idx];
        if (!isalnum((unsigned char)after_pct) && after_pct != '(' && after_pct != '~') {
            get_char(p);
            val.val = val.val / 100.0;
            val.is_pct = true;
        }
    }

    /* Implicit multiplication: 2pi, 3(4+5), 2ans, 3km */
    skip_whitespace(p);
    char next_c = peek(p);
    if (next_c == '(' || isalpha((unsigned char)next_c)) {
        /* Do not perform implicit multiplication if word is infix operator: "xor", "to" */
        if (strncasecmp(&p->src[p->pos], "xor", 3) == 0 && !isalnum((unsigned char)p->src[p->pos + 3]) && p->src[p->pos + 3] != '_') {
            /* Infix xor */
        } else if (strncasecmp(&p->src[p->pos], "to", 2) == 0 && !isalnum((unsigned char)p->src[p->pos + 2]) && p->src[p->pos + 2] != '_') {
            /* Infix to */
        } else if (strncasecmp(&p->src[p->pos], "before", 6) == 0 && !isalnum((unsigned char)p->src[p->pos + 6]) && p->src[p->pos + 6] != '_') {
            /* Infix before */
        } else if (strncasecmp(&p->src[p->pos], "after", 5) == 0 && !isalnum((unsigned char)p->src[p->pos + 5]) && p->src[p->pos + 5] != '_') {
            /* Infix after */
        } else if (strncasecmp(&p->src[p->pos], "from", 4) == 0 && !isalnum((unsigned char)p->src[p->pos + 4]) && p->src[p->pos + 4] != '_') {
            /* Infix from */
        } else if (strncasecmp(&p->src[p->pos], "until", 5) == 0 && !isalnum((unsigned char)p->src[p->pos + 5]) && p->src[p->pos + 5] != '_') {
            /* Infix until */
        } else if (strncasecmp(&p->src[p->pos], "since", 5) == 0 && !isalnum((unsigned char)p->src[p->pos + 5]) && p->src[p->pos + 5] != '_') {
            /* Infix since */
        } else if (strncasecmp(&p->src[p->pos], "between", 7) == 0 && !isalnum((unsigned char)p->src[p->pos + 7]) && p->src[p->pos + 7] != '_') {
            /* Infix between */
        } else {
            VNum right = parse_factor(p);
            if (val.dim != DIM_UNKNOWN && right.dim != DIM_UNKNOWN) {
                p->has_error = true;
                p->error_msg = "cannot multiply two dimensional quantities";
                return val;
            }
            if (right.dim != DIM_UNKNOWN) {
                val.dim = right.dim;
                val.unit = right.unit;
            }
            val.val *= right.val;
            val.is_date = false;
            val.is_time = false;
            if (right.is_bday) val.is_bday = true;
        }
    }

    return val;
}

static VNum parse_power(Parser *p) {
    VNum left = parse_factor(p);
    skip_whitespace(p);
    if (peek(p) == '^') {
        get_char(p);
        VNum right = parse_power(p); /* right-associative */
        if (left.dim != DIM_UNKNOWN || right.dim != DIM_UNKNOWN) {
            p->has_error = true;
            p->error_msg = "power on unit not supported";
            return left;
        }
        left.val = pow(left.val, right.val);
        left.is_pct = false;
        left.is_date = false;
        left.is_time = false;
    }
    return left;
}

static VNum parse_term(Parser *p) {
    VNum left = parse_power(p);
    while (1) {
        char op = peek(p);
        if (op == '*' || ((op == 'x' || op == 'X') && !isalpha((unsigned char)p->src[p->pos + 1]) && p->src[p->pos + 1] != '_')) {
            get_char(p);
            VNum right = parse_power(p);
            if (left.dim != DIM_UNKNOWN && right.dim != DIM_UNKNOWN) {
                p->has_error = true;
                p->error_msg = "cannot multiply two dimensional quantities";
                return left;
            }
            if (right.dim != DIM_UNKNOWN) {
                left.dim = right.dim;
                left.unit = right.unit;
            }
            left.val *= right.val;
            left.is_pct = false;
            left.is_date = false;
            left.is_time = false;
        } else if (op == '/') {
            get_char(p);
            VNum right = parse_power(p);
            if (right.val == 0.0) {
                p->has_error = true;
                p->error_msg = "div by 0";
                return left;
            }
            if (left.dim != DIM_UNKNOWN && right.dim != DIM_UNKNOWN) {
                if (left.dim == right.dim) {
                    /* Unit cancellation: 10km / 2km = 5 (dimensionless) */
                    left.dim = DIM_UNKNOWN;
                    left.unit = NULL;
                    left.val /= right.val;
                } else {
                    p->has_error = true;
                    p->error_msg = "dimension mismatch in division";
                    return left;
                }
            } else if (left.dim == DIM_UNKNOWN && right.dim != DIM_UNKNOWN) {
                p->has_error = true;
                p->error_msg = "cannot divide scalar by unit";
                return left;
            } else {
                left.val /= right.val;
            }
            left.is_pct = false;
            left.is_date = false;
            left.is_time = false;
        } else if (op == '%') {
            get_char(p);
            VNum right = parse_power(p);
            if (left.dim != DIM_UNKNOWN || right.dim != DIM_UNKNOWN) {
                p->has_error = true;
                p->error_msg = "modulo on unit not supported";
                return left;
            }
            if (right.val == 0.0) {
                p->has_error = true;
                p->error_msg = "mod by 0";
                return left;
            }
            left.val = fmod(left.val, right.val);
            left.is_pct = false;
            left.is_date = false;
            left.is_time = false;
        } else {
            break;
        }
    }
    return left;
}

static VNum parse_add_sub(Parser *p) {
    VNum left = parse_term(p);
    while (1) {
        char op = peek(p);
        if (op == '+') {
            get_char(p);
            VNum right = parse_term(p);
            if (right.is_pct) {
                right.val = left.val * right.val; /* e.g. 500 + 20% -> 500 + 100 */
                right.dim = left.dim;
                right.unit = left.unit;
            }
            if (left.is_date && right.is_bday) {
                char p_tz[32] = "";
                if (left.tz[0]) snprintf(p_tz, sizeof(p_tz), "%s", left.tz);
                left.val = (double)add_business_days((time_t)round(left.val), (int)round(right.val));
                left.is_date = true;
                left.is_time = false;
                left.dim = DIM_UNKNOWN;
                left.unit = NULL;
                snprintf(left.tz, sizeof(left.tz), "%s", p_tz);
            } else if (left.is_bday && right.is_date) {
                char p_tz[32] = "";
                if (right.tz[0]) snprintf(p_tz, sizeof(p_tz), "%s", right.tz);
                left.val = (double)add_business_days((time_t)round(right.val), (int)round(left.val));
                left.is_date = true;
                left.is_time = false;
                left.dim = DIM_UNKNOWN;
                left.unit = NULL;
                snprintf(left.tz, sizeof(left.tz), "%s", p_tz);
            } else if (left.is_date || right.is_date) {
                char p_tz[32] = "";
                if (left.tz[0]) snprintf(p_tz, sizeof(p_tz), "%s", left.tz);
                else if (right.tz[0]) snprintf(p_tz, sizeof(p_tz), "%s", right.tz);
                left.val += right.val;
                left.is_date = true;
                left.is_time = false;
                left.dim = DIM_UNKNOWN;
                left.unit = NULL;
                snprintf(left.tz, sizeof(left.tz), "%s", p_tz);
            } else if (left.is_time || right.is_time) {
                left.val += right.val;
                left.is_time = true;
                left.is_date = false;
                left.dim = DIM_UNKNOWN;
                left.unit = NULL;
            } else if (left.dim != DIM_UNKNOWN || right.dim != DIM_UNKNOWN) {
                if (left.dim != right.dim) {
                    p->has_error = true;
                    p->error_msg = "dimension mismatch";
                    return left;
                }
                left.val += right.val;
                /* left.unit is preserved as the preferred display unit */
            } else {
                left.val += right.val;
            }
        } else if (op == '-') {
            get_char(p);
            VNum right = parse_term(p);
            if (right.is_pct) {
                right.val = left.val * right.val; /* e.g. 500 - 20% -> 500 - 100 */
                right.dim = left.dim;
                right.unit = left.unit;
            }
            if (left.is_date && right.is_bday) {
                char p_tz[32] = "";
                if (left.tz[0]) snprintf(p_tz, sizeof(p_tz), "%s", left.tz);
                left.val = (double)add_business_days((time_t)round(left.val), -(int)round(right.val));
                left.is_date = true;
                left.is_time = false;
                left.dim = DIM_UNKNOWN;
                left.unit = NULL;
                snprintf(left.tz, sizeof(left.tz), "%s", p_tz);
            } else if (left.is_date && right.is_date) {
                left.val -= right.val;
                left.is_date = false;
                left.is_time = false;
                left.dim = DIM_UNKNOWN;
                left.unit = NULL;
                left.tz[0] = '\0';
            } else if (left.is_time && right.is_time) {
                left.val -= right.val;
                left.is_date = false;
                left.is_time = false;
                left.dim = DIM_UNKNOWN;
                left.unit = NULL;
                left.tz[0] = '\0';
            } else if (left.is_date && !right.is_date) {
                char p_tz[32] = "";
                if (left.tz[0]) snprintf(p_tz, sizeof(p_tz), "%s", left.tz);
                left.val -= right.val;
                left.is_date = true;
                left.is_time = false;
                left.dim = DIM_UNKNOWN;
                left.unit = NULL;
                snprintf(left.tz, sizeof(left.tz), "%s", p_tz);
            } else if (left.is_time && !right.is_time) {
                left.val -= right.val;
                left.is_time = true;
                left.is_date = false;
                left.dim = DIM_UNKNOWN;
                left.unit = NULL;
            } else if (left.dim != DIM_UNKNOWN || right.dim != DIM_UNKNOWN) {
                if (left.dim != right.dim) {
                    p->has_error = true;
                    p->error_msg = "dimension mismatch";
                    return left;
                }
                left.val -= right.val;
                /* left.unit is preserved as the preferred display unit */
            } else {
                left.val -= right.val;
            }
        } else {
            /* Support adjacent compound units (time, imperial length, imperial mass): e.g. "1 hr 15 min", "5 ft 9 in", "8 lb 8 oz" without explicit '+' */
            if (left.dim != DIM_UNKNOWN && left.unit != NULL && !p->has_error) {
                skip_whitespace(p);
                size_t saved_pos = p->pos;
                char nc = peek(p);
                if (isdigit((unsigned char)nc)) {
                    VNum next_term = parse_term(p);
                    if (!p->has_error && next_term.dim == left.dim && next_term.unit != NULL &&
                        are_units_same_compound_system(left.unit->symbol, next_term.unit->symbol)) {
                        left.val += next_term.val;
                        continue;
                    } else {
                        /* Not same compound system, rollback cleanly */
                        p->pos = saved_pos;
                        p->has_error = false;
                        p->error_msg = NULL;
                    }
                }
            }
            break;
        }
    }
    return left;
}

static VNum parse_shift(Parser *p) {
    VNum left = parse_add_sub(p);
    while (1) {
        skip_whitespace(p);
        if (p->src[p->pos] == '<' && p->src[p->pos+1] == '<') {
            p->pos += 2;
            VNum right = parse_add_sub(p);
            if (left.dim != DIM_UNKNOWN || right.dim != DIM_UNKNOWN) {
                p->has_error = true;
                p->error_msg = "bitwise op on unit not supported";
                return left;
            }
            left.val = (double)((int64_t)round(left.val) << (int64_t)round(right.val));
            left.is_pct = false;
            left.is_date = false;
            left.is_time = false;
        } else if (p->src[p->pos] == '>' && p->src[p->pos+1] == '>') {
            p->pos += 2;
            VNum right = parse_add_sub(p);
            if (left.dim != DIM_UNKNOWN || right.dim != DIM_UNKNOWN) {
                p->has_error = true;
                p->error_msg = "bitwise op on unit not supported";
                return left;
            }
            left.val = (double)((int64_t)round(left.val) >> (int64_t)round(right.val));
            left.is_pct = false;
            left.is_date = false;
            left.is_time = false;
        } else {
            break;
        }
    }
    return left;
}

static VNum parse_and(Parser *p) {
    VNum left = parse_shift(p);
    while (1) {
        skip_whitespace(p);
        if (p->src[p->pos] == '&' && p->src[p->pos+1] != '&') {
            p->pos++;
            VNum right = parse_shift(p);
            if (left.dim != DIM_UNKNOWN || right.dim != DIM_UNKNOWN) {
                p->has_error = true;
                p->error_msg = "bitwise op on unit not supported";
                return left;
            }
            left.val = (double)((int64_t)round(left.val) & (int64_t)round(right.val));
            left.is_pct = false;
            left.is_date = false;
            left.is_time = false;
        } else {
            break;
        }
    }
    return left;
}

static VNum parse_xor(Parser *p) {
    VNum left = parse_and(p);
    while (1) {
        skip_whitespace(p);
        if ((p->src[p->pos] == 'x' || p->src[p->pos] == 'X') &&
            (p->src[p->pos+1] == 'o' || p->src[p->pos+1] == 'O') &&
            (p->src[p->pos+2] == 'r' || p->src[p->pos+2] == 'R') &&
            !isalnum((unsigned char)p->src[p->pos+3]) && p->src[p->pos+3] != '_') {
            p->pos += 3;
            VNum right = parse_and(p);
            if (left.dim != DIM_UNKNOWN || right.dim != DIM_UNKNOWN) {
                p->has_error = true;
                p->error_msg = "bitwise op on unit not supported";
                return left;
            }
            left.val = (double)((int64_t)round(left.val) ^ (int64_t)round(right.val));
            left.is_pct = false;
            left.is_date = false;
            left.is_time = false;
        } else {
            break;
        }
    }
    return left;
}

static VNum parse_expression(Parser *p) {
    VNum left = parse_xor(p);
    while (1) {
        skip_whitespace(p);
        if (p->src[p->pos] == '|' && p->src[p->pos+1] != '|') {
            p->pos++;
            VNum right = parse_xor(p);
            if (left.dim != DIM_UNKNOWN || right.dim != DIM_UNKNOWN) {
                p->has_error = true;
                p->error_msg = "bitwise op on unit not supported";
                return left;
            }
            left.val = (double)((int64_t)round(left.val) | (int64_t)round(right.val));
            left.is_pct = false;
            left.is_date = false;
            left.is_time = false;
        } else {
            break;
        }
    }
    return left;
}

bool evaluate_equation_ex(const char *input, double ans_val, double *result,
                          char *unit_symbol, size_t unit_sz,
                          bool *is_date_out, bool *is_time_out, const char **err_msg) {
    if (unit_symbol && unit_sz > 0) unit_symbol[0] = '\0';
    if (is_date_out) *is_date_out = false;
    if (is_time_out) *is_time_out = false;
    if (!input || strlen(input) == 0) return false;

    /* 1. In-session commands: vars / saved */
    if (strcasecmp(input, "vars") == 0 || strcasecmp(input, "saved") == 0) {
        char list_buf[1024] = "";
        int cnt = format_saved_symbols_list(list_buf, sizeof(list_buf));
        *result = (double)cnt;
        if (unit_symbol && unit_sz > 0) {
            if (cnt == 0) snprintf(unit_symbol, unit_sz, "no saved variables or functions");
            else snprintf(unit_symbol, unit_sz, "%s", list_buf);
        }
        return true;
    }

    /* 2. Deletion: delete <name> / undef <name> / remove <name> */
    if (strncasecmp(input, "delete ", 7) == 0 || strncasecmp(input, "undef ", 6) == 0 || strncasecmp(input, "remove ", 7) == 0) {
        const char *p_name = input + (input[0] == 'u' || input[0] == 'U' ? 6 : 7);
        while (*p_name && isspace((unsigned char)*p_name)) p_name++;
        char target[32];
        size_t t_len = 0;
        while (p_name[t_len] && !isspace((unsigned char)p_name[t_len]) && t_len < 31) {
            target[t_len] = p_name[t_len];
            t_len++;
        }
        target[t_len] = '\0';
        if (delete_symbol(target)) {
            save_saved_symbols();
            *result = 0.0;
            if (unit_symbol && unit_sz > 0) snprintf(unit_symbol, unit_sz, "deleted %s", target);
            return true;
        } else {
            if (err_msg) *err_msg = "symbol not found";
            return false;
        }
    }

    /* 3. Check for assignment: := (persistent) or = (session) */
    bool is_persistent = false;
    const char *p_assign = NULL;
    size_t op_len = 0;
    int p_depth = 0;
    for (const char *s = input; *s; s++) {
        if (*s == '(') p_depth++;
        else if (*s == ')') { if (p_depth > 0) p_depth--; }
        else if (p_depth == 0) {
            if (*s == ':' && *(s + 1) == '=') {
                p_assign = s;
                op_len = 2;
                is_persistent = true;
                break;
            } else if (*s == '=' && (s == input || (*(s - 1) != '!' && *(s - 1) != '<' && *(s - 1) != '>' && *(s - 1) != ':')) && *(s + 1) != '=') {
                if (!p_assign) {
                    p_assign = s;
                    op_len = 1;
                    is_persistent = false;
                }
            }
        }
    }

    if (p_assign && p_assign > input) {
        char lhs[64];
        size_t lhs_len = (size_t)(p_assign - input);
        if (lhs_len >= sizeof(lhs)) lhs_len = sizeof(lhs) - 1;
        memcpy(lhs, input, lhs_len);
        lhs[lhs_len] = '\0';

        char *lhs_start = lhs;
        while (*lhs_start && isspace((unsigned char)*lhs_start)) lhs_start++;
        char *lhs_end = lhs_start + strlen(lhs_start);
        while (lhs_end > lhs_start && isspace((unsigned char)*(lhs_end - 1))) lhs_end--;
        *lhs_end = '\0';

        const char *rhs = p_assign + op_len;
        while (*rhs && isspace((unsigned char)*rhs)) rhs++;
        if (*rhs == '\0') {
            if (err_msg) *err_msg = "missing expression after assignment";
            return false;
        }

        /* Check if LHS is a function definition: func(p1, p2, ...) */
        char *p_open = strchr(lhs_start, '(');
        char *p_close = strrchr(lhs_start, ')');
        if (p_open && p_close && p_close > p_open) {
            *p_open = '\0';
            char *fn_name = lhs_start;
            while (*fn_name && isspace((unsigned char)*fn_name)) fn_name++;
            char *fn_end = fn_name + strlen(fn_name);
            while (fn_end > fn_name && isspace((unsigned char)*(fn_end - 1))) fn_end--;
            *fn_end = '\0';

            if (!isalpha((unsigned char)fn_name[0])) {
                if (err_msg) *err_msg = "function name must start with a letter";
                return false;
            }
            for (size_t i = 0; fn_name[i]; i++) {
                if (!isalnum((unsigned char)fn_name[i]) && fn_name[i] != '_') {
                    if (err_msg) *err_msg = "invalid character in function name";
                    return false;
                }
            }
            if (is_reserved_name(fn_name)) {
                if (err_msg) *err_msg = "cannot assign to reserved name";
                return false;
            }

            char *param_str = p_open + 1;
            *p_close = '\0';
            char param_display[64];
            snprintf(param_display, sizeof(param_display), "%s", param_str);

            char params[MAX_PARAMS][16];
            int param_count = 0;
            char *tok = strtok(param_str, ",");
            while (tok && param_count < MAX_PARAMS) {
                while (*tok && isspace((unsigned char)*tok)) tok++;
                char *tok_end = tok + strlen(tok);
                while (tok_end > tok && isspace((unsigned char)*(tok_end - 1))) tok_end--;
                *tok_end = '\0';
                if (*tok) {
                    if (!isalpha((unsigned char)*tok)) {
                        if (err_msg) *err_msg = "parameter must start with a letter";
                        return false;
                    }
                    snprintf(params[param_count++], 16, "%s", tok);
                }
                tok = strtok(NULL, ",");
            }
            if (param_count == 0) {
                if (err_msg) *err_msg = "function must have at least 1 parameter";
                return false;
            }

            /* Test-parse body with dummy parameters to validate syntax */
            int prev_stack_top = g_local_stack_top;
            for (int pi = 0; pi < param_count; pi++) {
                snprintf(g_local_stack[g_local_stack_top].name, sizeof(g_local_stack[g_local_stack_top].name), "%.15s", params[pi]);
                g_local_stack[g_local_stack_top].val = 1.0;
                g_local_stack_top++;
            }
            double dummy_res = 0.0;
            const char *dummy_err = NULL;
            bool syntax_ok = evaluate_equation_ex(rhs, 0.0, &dummy_res, NULL, 0, NULL, NULL, &dummy_err);
            g_local_stack_top = prev_stack_top;

            if (!syntax_ok) {
                if (err_msg) *err_msg = dummy_err ? dummy_err : "syntax error in function body";
                return false;
            }

            define_symbol(fn_name, rhs, true, param_count, (const char (*)[16])params, is_persistent);
            if (is_persistent) save_saved_symbols();

            *result = 0.0;
            if (unit_symbol && unit_sz > 0) {
                snprintf(unit_symbol, unit_sz, "%s(%s)", fn_name, param_display);
            }
            return true;
        }

        /* Otherwise LHS is a variable name */
        char *var_name = lhs_start;
        if (!isalpha((unsigned char)var_name[0])) {
            if (err_msg) *err_msg = "variable name must start with a letter";
            return false;
        }
        for (size_t i = 0; var_name[i]; i++) {
            if (!isalnum((unsigned char)var_name[i]) && var_name[i] != '_') {
                if (err_msg) *err_msg = "invalid character in variable name";
                return false;
            }
        }
        if (is_reserved_var_name(var_name)) {
            if (err_msg) *err_msg = "cannot assign to reserved name";
            return false;
        }

        /* Evaluate RHS expression (could be conversion or math/date) */
        double rhs_val = 0.0;
        char conv_res[64], conv_sym[32], conv_sfx[64];
        const char *c_err = NULL;
        if (try_convert_expression(rhs, ans_val, &rhs_val, conv_res, sizeof(conv_res),
                                   conv_sym, sizeof(conv_sym), conv_sfx, sizeof(conv_sfx), &c_err) && !c_err) {
            const UnitItem *tu = conv_sym[0] ? find_unit(conv_sym) : NULL;
            double base_val = rhs_val * (tu ? tu->to_base : 1.0);
            define_symbol_ex(var_name, rhs, false, 0, NULL, is_persistent, base_val, conv_sym, false, false);
            if (is_persistent) save_saved_symbols();
            *result = rhs_val;
            if (unit_symbol && unit_sz > 0) snprintf(unit_symbol, unit_sz, "%s", conv_sym);
            return true;
        }

        const char *m_err = NULL;
        bool rhs_is_date = false;
        bool rhs_is_time = false;
        char rhs_unit[32] = "";
        if (evaluate_equation_ex(rhs, ans_val, &rhs_val, rhs_unit, sizeof(rhs_unit), &rhs_is_date, &rhs_is_time, &m_err)) {
            define_symbol_ex(var_name, rhs, false, 0, NULL, is_persistent, rhs_val, rhs_unit, rhs_is_date, rhs_is_time);
            if (is_persistent) save_saved_symbols();
            *result = rhs_val;
            if (unit_symbol && unit_sz > 0) snprintf(unit_symbol, unit_sz, "%s", rhs_unit);
            if (is_date_out) *is_date_out = rhs_is_date;
            if (is_time_out) *is_time_out = rhs_is_time;
            return true;
        } else {
            if (err_msg) *err_msg = m_err ? m_err : "syntax error in expression";
            return false;
        }
    }

    Parser p = { .src = input, .pos = 0, .ans_val = ans_val, .has_error = false, .error_msg = NULL, .in_multi_arg_func = 0 };
    VNum res = parse_expression(&p);
    skip_whitespace(&p);
    if (p.src[p.pos] != '\0') {
        p.has_error = true;
    }
    if (p.has_error) {
        if (err_msg && p.error_msg) *err_msg = p.error_msg;
        return false;
    }

    if (res.unit != NULL && res.unit->to_base != 0.0) {
        *result = res.val / res.unit->to_base;
        if (unit_symbol && unit_sz > 0) {
            snprintf(unit_symbol, unit_sz, "%s", res.unit->symbol);
        }
    } else {
        *result = res.val;
        if (unit_symbol && unit_sz > 0) {
            if (res.tz[0]) snprintf(unit_symbol, unit_sz, "%s", res.tz);
            else unit_symbol[0] = '\0';
        }
    }

    if (is_date_out) *is_date_out = res.is_date;
    if (is_time_out) *is_time_out = res.is_time;
    return true;
}

bool evaluate_equation(const char *input, double ans_val, double *result, const char **err_msg) {
    return evaluate_equation_ex(input, ans_val, result, NULL, 0, NULL, NULL, err_msg);
}

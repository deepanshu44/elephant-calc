#define _POSIX_C_SOURCE 200809L
#include "autocomplete.h"
#include "elephant.h"
#include "timezone.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>

typedef enum {
    CAT_FUNC = 0,         /* Math / financial / aggregate function: sqrt, sin, emi */
    CAT_CALENDAR_OP,      /* Date operators: until, before, after, since, between */
    CAT_ANCHOR,           /* Date/time anchors: now, today, tomorrow, yesterday, bday */
    CAT_UNIT,             /* Physical units with dimension: m, ft, kg, s, etc. */
    CAT_TIMEZONE,         /* Cities & timezone abbrevs: tokyo, london, utc, ist */
    CAT_SCALE,            /* Number scales: crore, lakh, million, billion */
    CAT_CONSTANT          /* Core constants: ans */
} CompletionCategory;

typedef struct {
    const char *name;
    bool is_func;
    CompletionCategory category;
    Dimension dim;
    const char *signature_hint;
} CompletionEntry;

static const CompletionEntry BUILTIN_COMPLETIONS[] = {
    /* Math & Scientific Functions */
    { "sqrt",     true,  CAT_FUNC, DIM_UNKNOWN, "sqrt(x) - Square root" },
    { "cbrt",     true,  CAT_FUNC, DIM_UNKNOWN, "cbrt(x) - Cube root" },
    { "sin",      true,  CAT_FUNC, DIM_UNKNOWN, "sin(x) - Sine" },
    { "cos",      true,  CAT_FUNC, DIM_UNKNOWN, "cos(x) - Cosine" },
    { "tan",      true,  CAT_FUNC, DIM_UNKNOWN, "tan(x) - Tangent" },
    { "asin",     true,  CAT_FUNC, DIM_UNKNOWN, "asin(x) - Arc sine" },
    { "acos",     true,  CAT_FUNC, DIM_UNKNOWN, "acos(x) - Arc cosine" },
    { "atan",     true,  CAT_FUNC, DIM_UNKNOWN, "atan(x) - Arc tangent" },
    { "sinh",     true,  CAT_FUNC, DIM_UNKNOWN, "sinh(x) - Hyperbolic sine" },
    { "cosh",     true,  CAT_FUNC, DIM_UNKNOWN, "cosh(x) - Hyperbolic cosine" },
    { "tanh",     true,  CAT_FUNC, DIM_UNKNOWN, "tanh(x) - Hyperbolic tangent" },
    { "log",      true,  CAT_FUNC, DIM_UNKNOWN, "log(x) - Logarithm base 10" },
    { "ln",       true,  CAT_FUNC, DIM_UNKNOWN, "ln(x) - Natural logarithm" },
    { "exp",      true,  CAT_FUNC, DIM_UNKNOWN, "exp(x) - Exponential e^x" },
    { "abs",      true,  CAT_FUNC, DIM_UNKNOWN, "abs(x) - Absolute value" },
    { "ceil",     true,  CAT_FUNC, DIM_UNKNOWN, "ceil(x) - Ceiling" },
    { "floor",    true,  CAT_FUNC, DIM_UNKNOWN, "floor(x) - Floor" },
    { "round",    true,  CAT_FUNC, DIM_UNKNOWN, "round(x) - Round to nearest integer" },
    { "pow",      true,  CAT_FUNC, DIM_UNKNOWN, "pow(base, exp) - Power" },
    { "mod",      true,  CAT_FUNC, DIM_UNKNOWN, "mod(a, b) - Modulo remainder" },

    /* Aggregate Functions */
    { "avg",      true,  CAT_FUNC, DIM_UNKNOWN, "avg(a, b, ...) - Average" },
    { "median",   true,  CAT_FUNC, DIM_UNKNOWN, "median(a, b, ...) - Median value" },
    { "min",      true,  CAT_FUNC, DIM_UNKNOWN, "min(a, b, ...) - Minimum" },
    { "max",      true,  CAT_FUNC, DIM_UNKNOWN, "max(a, b, ...) - Maximum" },

    /* Financial & Loan Functions */
    { "emi",      true,  CAT_FUNC, DIM_UNKNOWN, "emi(principal, rate%, months)" },
    { "sip",      true,  CAT_FUNC, DIM_UNKNOWN, "sip(monthly_amount, rate%, years)" },
    { "cagr",     true,  CAT_FUNC, DIM_UNKNOWN, "cagr(start_val, end_val, years)" },
    { "compound", true,  CAT_FUNC, DIM_UNKNOWN, "compound(P, rate%, periods, freq)" },
    { "discount", true,  CAT_FUNC, DIM_UNKNOWN, "discount(price, pct%)" },
    { "markup",   true,  CAT_FUNC, DIM_UNKNOWN, "markup(cost, pct%)" },

    /* Date, Time & Calendar Operators */
    { "until",     false, CAT_CALENDAR_OP, DIM_UNKNOWN, "Date duration until target date" },
    { "before",    false, CAT_CALENDAR_OP, DIM_UNKNOWN, "Relative date offset before anchor" },
    { "after",     false, CAT_CALENDAR_OP, DIM_UNKNOWN, "Relative date offset after anchor" },
    { "since",     false, CAT_CALENDAR_OP, DIM_UNKNOWN, "Date duration since past date" },
    { "between",   false, CAT_CALENDAR_OP, DIM_UNKNOWN, "Date duration between two dates" },
    { "now",       false, CAT_ANCHOR,      DIM_TIME,    "Current timestamp" },
    { "today",     false, CAT_ANCHOR,      DIM_TIME,    "Current date" },
    { "tomorrow",  false, CAT_ANCHOR,      DIM_TIME,    "Next day's date" },
    { "yesterday", false, CAT_ANCHOR,      DIM_TIME,    "Previous day's date" },
    { "bday",      false, CAT_ANCHOR,      DIM_TIME,    "Business day" },
    { "bizday",    false, CAT_ANCHOR,      DIM_TIME,    "Business day" },
    { "workday",   false, CAT_ANCHOR,      DIM_TIME,    "Working day" },

    /* Physical Units - Time & Duration */
    { "days",      false, CAT_UNIT, DIM_TIME, "Time duration unit (days)" },
    { "day",       false, CAT_UNIT, DIM_TIME, "Time duration unit (day)" },
    { "weeks",     false, CAT_UNIT, DIM_TIME, "Time duration unit (weeks)" },
    { "week",      false, CAT_UNIT, DIM_TIME, "Time duration unit (week)" },
    { "months",    false, CAT_UNIT, DIM_TIME, "Time duration unit (months)" },
    { "month",     false, CAT_UNIT, DIM_TIME, "Time duration unit (month)" },
    { "years",     false, CAT_UNIT, DIM_TIME, "Time duration unit (years)" },
    { "year",      false, CAT_UNIT, DIM_TIME, "Time duration unit (year)" },
    { "hours",     false, CAT_UNIT, DIM_TIME, "Time duration unit (hours)" },
    { "hour",      false, CAT_UNIT, DIM_TIME, "Time duration unit (hour)" },
    { "minutes",   false, CAT_UNIT, DIM_TIME, "Time duration unit (minutes)" },
    { "minute",    false, CAT_UNIT, DIM_TIME, "Time duration unit (minute)" },
    { "seconds",   false, CAT_UNIT, DIM_TIME, "Time duration unit (seconds)" },
    { "second",    false, CAT_UNIT, DIM_TIME, "Time duration unit (second)" },

    /* Core Constants & Memory */
    { "ans",       false, CAT_CONSTANT, DIM_UNKNOWN, "Previous calculation answer" },

    /* Physical Units - Temperature */
    { "celsius",    false, CAT_UNIT, DIM_TEMP, "Celsius temperature" },
    { "fahrenheit", false, CAT_UNIT, DIM_TEMP, "Fahrenheit temperature" },
    { "kelvin",     false, CAT_UNIT, DIM_TEMP, "Kelvin temperature" },

    /* Physical Units - Distance & Length */
    { "kilometers",     false, CAT_UNIT, DIM_LENGTH, "Metric distance" },
    { "kilometer",      false, CAT_UNIT, DIM_LENGTH, "Metric distance" },
    { "meters",         false, CAT_UNIT, DIM_LENGTH, "Metric length" },
    { "meter",          false, CAT_UNIT, DIM_LENGTH, "Metric length" },
    { "centimeters",    false, CAT_UNIT, DIM_LENGTH, "Metric length" },
    { "centimeter",     false, CAT_UNIT, DIM_LENGTH, "Metric length" },
    { "millimeters",    false, CAT_UNIT, DIM_LENGTH, "Metric length" },
    { "millimeter",     false, CAT_UNIT, DIM_LENGTH, "Metric length" },
    { "miles",          false, CAT_UNIT, DIM_LENGTH, "Imperial distance" },
    { "mile",           false, CAT_UNIT, DIM_LENGTH, "Imperial distance" },
    { "yards",          false, CAT_UNIT, DIM_LENGTH, "Imperial distance" },
    { "yard",           false, CAT_UNIT, DIM_LENGTH, "Imperial distance" },
    { "feet",           false, CAT_UNIT, DIM_LENGTH, "Imperial length" },
    { "foot",           false, CAT_UNIT, DIM_LENGTH, "Imperial length" },
    { "inches",         false, CAT_UNIT, DIM_LENGTH, "Imperial length" },
    { "inch",           false, CAT_UNIT, DIM_LENGTH, "Imperial length" },
    { "nautical_miles", false, CAT_UNIT, DIM_LENGTH, "Nautical distance" },

    /* Physical Units - Mass & Weight */
    { "kilograms",  false, CAT_UNIT, DIM_MASS, "Metric mass" },
    { "kilogram",   false, CAT_UNIT, DIM_MASS, "Metric mass" },
    { "grams",      false, CAT_UNIT, DIM_MASS, "Metric mass" },
    { "gram",       false, CAT_UNIT, DIM_MASS, "Metric mass" },
    { "milligrams", false, CAT_UNIT, DIM_MASS, "Metric mass" },
    { "milligram",  false, CAT_UNIT, DIM_MASS, "Metric mass" },
    { "pounds",     false, CAT_UNIT, DIM_MASS, "Imperial weight" },
    { "pound",      false, CAT_UNIT, DIM_MASS, "Imperial weight" },
    { "ounces",     false, CAT_UNIT, DIM_MASS, "Imperial weight" },
    { "ounce",      false, CAT_UNIT, DIM_MASS, "Imperial weight" },

    /* Currency Units */
    { "inr",      false, CAT_UNIT, DIM_CURRENCY, "Indian Rupee" },
    { "usd",      false, CAT_UNIT, DIM_CURRENCY, "US Dollar" },
    { "eur",      false, CAT_UNIT, DIM_CURRENCY, "Euro" },
    { "gbp",      false, CAT_UNIT, DIM_CURRENCY, "British Pound" },
    { "jpy",      false, CAT_UNIT, DIM_CURRENCY, "Japanese Yen" },
    { "aud",      false, CAT_UNIT, DIM_CURRENCY, "Australian Dollar" },
    { "cad",      false, CAT_UNIT, DIM_CURRENCY, "Canadian Dollar" },
    { "chf",      false, CAT_UNIT, DIM_CURRENCY, "Swiss Franc" },
    { "cny",      false, CAT_UNIT, DIM_CURRENCY, "Chinese Yuan" },

    /* Number Words & Scales */
    { "crore",    false, CAT_SCALE, DIM_UNKNOWN, "10,000,000" },
    { "lakh",     false, CAT_SCALE, DIM_UNKNOWN, "100,000" },
    { "million",  false, CAT_SCALE, DIM_UNKNOWN, "1,000,000" },
    { "billion",  false, CAT_SCALE, DIM_UNKNOWN, "1,000,000,000" },
    { "trillion", false, CAT_SCALE, DIM_UNKNOWN, "1,000,000,000,000" },

    /* Data & Speed */
    { "gigabytes", false, CAT_UNIT, DIM_DATA, "Data storage" },
    { "megabytes", false, CAT_UNIT, DIM_DATA, "Data storage" },
    { "terabytes", false, CAT_UNIT, DIM_DATA, "Data storage" },
    { "kilobytes", false, CAT_UNIT, DIM_DATA, "Data storage" },
    { "mbps",      false, CAT_UNIT, DIM_DATA_RATE, "Megabits per second" },
    { "gbps",      false, CAT_UNIT, DIM_DATA_RATE, "Gigabits per second" },
    { "kbps",      false, CAT_UNIT, DIM_DATA_RATE, "Kilobits per second" },

    /* Global Timezone Cities */
    { "tokyo",       false, CAT_TIMEZONE, DIM_TIME, "Tokyo, Japan (JST)" },
    { "london",      false, CAT_TIMEZONE, DIM_TIME, "London, UK (GMT/BST)" },
    { "new_york",    false, CAT_TIMEZONE, DIM_TIME, "New York, USA (EST/EDT)" },
    { "paris",       false, CAT_TIMEZONE, DIM_TIME, "Paris, France (CET/CEST)" },
    { "berlin",      false, CAT_TIMEZONE, DIM_TIME, "Berlin, Germany (CET/CEST)" },
    { "sydney",      false, CAT_TIMEZONE, DIM_TIME, "Sydney, Australia (AEST/AEDT)" },
    { "dubai",       false, CAT_TIMEZONE, DIM_TIME, "Dubai, UAE (GST)" },
    { "singapore",   false, CAT_TIMEZONE, DIM_TIME, "Singapore (SGT)" },
    { "hong_kong",   false, CAT_TIMEZONE, DIM_TIME, "Hong Kong (HKT)" },
    { "bangalore",   false, CAT_TIMEZONE, DIM_TIME, "Bangalore, India (IST)" },
    { "mumbai",      false, CAT_TIMEZONE, DIM_TIME, "Mumbai, India (IST)" },
    { "delhi",       false, CAT_TIMEZONE, DIM_TIME, "Delhi, India (IST)" },
    { "los_angeles", false, CAT_TIMEZONE, DIM_TIME, "Los Angeles, USA (PST/PDT)" },
    { "chicago",     false, CAT_TIMEZONE, DIM_TIME, "Chicago, USA (CST/CDT)" },
    { "toronto",     false, CAT_TIMEZONE, DIM_TIME, "Toronto, Canada (EST/EDT)" },

    /* Standard Timezone Abbreviations */
    { "utc", false, CAT_TIMEZONE, DIM_TIME, "Coordinated Universal Time" },
    { "gmt", false, CAT_TIMEZONE, DIM_TIME, "Greenwich Mean Time" },
    { "ist", false, CAT_TIMEZONE, DIM_TIME, "India Standard Time" },
    { "est", false, CAT_TIMEZONE, DIM_TIME, "Eastern Standard Time" },
    { "cst", false, CAT_TIMEZONE, DIM_TIME, "Central Standard Time" },
    { "mst", false, CAT_TIMEZONE, DIM_TIME, "Mountain Standard Time" },
    { "pst", false, CAT_TIMEZONE, DIM_TIME, "Pacific Standard Time" }
};

static const size_t NUM_BUILTIN_COMPLETIONS = sizeof(BUILTIN_COMPLETIONS) / sizeof(BUILTIN_COMPLETIONS[0]);

typedef enum {
    CTX_NEUTRAL = 0,
    CTX_CONVERT_TARGET,   /* After "in", "to", "as" */
    CTX_AFTER_DURATION,   /* After duration words: days, weeks, hours, min, etc. */
    CTX_AFTER_NUMBER,     /* Preceded by digits */
    CTX_EXPR_START_OR_OP  /* At start of expr or after '+', '-', '*', '/', '(', ',' */
} AutocompleteContextType;

typedef struct {
    AutocompleteContextType type;
    Dimension target_dim;      /* Target dimension if converting */
    bool is_target_timezone;   /* If conversion target expects a timezone */
} AutocompleteContext;

static AutocompleteContext detect_autocomplete_context(const char *input, size_t prefix_start) {
    AutocompleteContext ctx = { CTX_NEUTRAL, DIM_UNKNOWN, false };
    if (!input || prefix_start == 0) {
        ctx.type = CTX_EXPR_START_OR_OP;
        return ctx;
    }

    /* Scan backwards past whitespace to find previous non-space character */
    size_t p = prefix_start;
    while (p > 0 && isspace((unsigned char)input[p - 1])) {
        p--;
    }
    if (p == 0) {
        ctx.type = CTX_EXPR_START_OR_OP;
        return ctx;
    }

    char prev_ch = input[p - 1];

    /* Check if preceded by an arithmetic operator or open bracket */
    if (prev_ch == '+' || prev_ch == '-' || prev_ch == '*' || prev_ch == '/' ||
        prev_ch == '^' || prev_ch == '(' || prev_ch == ',' || prev_ch == '=' ||
        prev_ch == '%' || prev_ch == '&' || prev_ch == '|') {
        ctx.type = CTX_EXPR_START_OR_OP;
        return ctx;
    }

    /* Check if immediately preceded by a digit */
    if (isdigit((unsigned char)prev_ch)) {
        ctx.type = CTX_AFTER_NUMBER;
        return ctx;
    }

    /* Find the word ending at p */
    size_t w_end = p;
    size_t w_start = w_end;
    while (w_start > 0 && (isalnum((unsigned char)input[w_start - 1]) || input[w_start - 1] == '_')) {
        w_start--;
    }
    size_t w_len = w_end - w_start;
    if (w_len == 0) {
        return ctx;
    }

    char word[32];
    if (w_len >= sizeof(word)) w_len = sizeof(word) - 1;
    memcpy(word, input + w_start, w_len);
    word[w_len] = '\0';

    /* Case A: Conversion operator ("in", "to", "as") */
    if (strcasecmp(word, "in") == 0 || strcasecmp(word, "to") == 0 || strcasecmp(word, "as") == 0) {
        ctx.type = CTX_CONVERT_TARGET;

        /* Extract the expression before the conversion keyword */
        size_t left_end = w_start;
        while (left_end > 0 && isspace((unsigned char)input[left_end - 1])) {
            left_end--;
        }
        if (left_end > 0) {
            char left_expr[128];
            size_t le_len = left_end >= sizeof(left_expr) ? sizeof(left_expr) - 1 : left_end;
            memcpy(left_expr, input, le_len);
            left_expr[le_len] = '\0';

            /* Check if left is a date/time anchor or timezone query */
            if (strcasecmp(left_expr, "now") == 0 || strcasecmp(left_expr, "today") == 0 ||
                strcasecmp(left_expr, "tomorrow") == 0 || strcasecmp(left_expr, "yesterday") == 0 ||
                is_valid_timezone_query(left_expr)) {
                ctx.is_target_timezone = true;
                ctx.target_dim = DIM_TIME;
            } else {
                /* Evaluate left to find its unit / dimension */
                double val = 0.0;
                char u_sym[32] = "";
                bool is_d = false, is_t = false;
                const char *err = NULL;
                if (evaluate_equation_ex(left_expr, 0.0, &val, u_sym, sizeof(u_sym), &is_d, &is_t, &err)) {
                    if (is_d || is_t || val > 100000000.0) {
                        ctx.is_target_timezone = true;
                        ctx.target_dim = DIM_TIME;
                    } else if (u_sym[0] != '\0') {
                        const UnitItem *ui = find_unit(u_sym);
                        if (ui) {
                            ctx.target_dim = ui->dim;
                        }
                    }
                } else {
                    /* Fallback suffix check */
                    const UnitItem *ui = find_unit(left_expr);
                    if (ui) ctx.target_dim = ui->dim;
                }
            }
        }
        return ctx;
    }

    /* Case B: Preceded by a duration token */
    if (strcasecmp(word, "days") == 0 || strcasecmp(word, "day") == 0 ||
        strcasecmp(word, "weeks") == 0 || strcasecmp(word, "week") == 0 ||
        strcasecmp(word, "months") == 0 || strcasecmp(word, "month") == 0 ||
        strcasecmp(word, "years") == 0 || strcasecmp(word, "year") == 0 ||
        strcasecmp(word, "hours") == 0 || strcasecmp(word, "hour") == 0 ||
        strcasecmp(word, "minutes") == 0 || strcasecmp(word, "minute") == 0 ||
        strcasecmp(word, "min") == 0 || strcasecmp(word, "seconds") == 0 ||
        strcasecmp(word, "second") == 0 || strcasecmp(word, "s") == 0 ||
        strcasecmp(word, "bday") == 0 || strcasecmp(word, "bizday") == 0 ||
        strcasecmp(word, "workday") == 0) {
        ctx.type = CTX_AFTER_DURATION;
        return ctx;
    }

    /* Case C: Check if preceding word is a number (e.g. "100 ") */
    bool word_all_digits = true;
    for (size_t i = 0; i < w_len; i++) {
        if (!isdigit((unsigned char)word[i]) && word[i] != '.') {
            word_all_digits = false;
            break;
        }
    }
    if (word_all_digits) {
        ctx.type = CTX_AFTER_NUMBER;
        return ctx;
    }

    return ctx;
}

typedef struct {
    char name[32];
    bool is_func;
    CompletionCategory category;
    Dimension dim;
    int score;
    size_t orig_idx;
} Cand;

static int score_candidate(const Cand *c, const AutocompleteContext *ctx, const char *prefix) {
    int score = 100;
    (void)prefix;

    if (ctx->type == CTX_CONVERT_TARGET) {
        if (ctx->is_target_timezone) {
            if (c->category == CAT_TIMEZONE) score += 350;
            else if (c->category == CAT_UNIT && c->dim == DIM_TIME) score += 200;
            else if (c->category == CAT_ANCHOR) score += 50;
            else if (c->category == CAT_FUNC) score -= 250;
            else score -= 100;
        } else if (ctx->target_dim != DIM_UNKNOWN) {
            if (c->category == CAT_UNIT && c->dim == ctx->target_dim) score += 400;
            else if (c->category == CAT_FUNC) score -= 250;
            else score -= 150;
        } else {
            if (c->category == CAT_UNIT || c->category == CAT_TIMEZONE) score += 200;
            else if (c->category == CAT_FUNC) score -= 150;
        }
    } else if (ctx->type == CTX_AFTER_DURATION) {
        if (c->category == CAT_CALENDAR_OP) score += 350;
        else if (c->category == CAT_TIMEZONE || c->category == CAT_FUNC) score -= 150;
    } else if (ctx->type == CTX_AFTER_NUMBER) {
        if (c->category == CAT_SCALE) score += 300;
        else if (c->category == CAT_UNIT) score += 200;
        else if (c->category == CAT_FUNC) score -= 100;
    } else if (ctx->type == CTX_EXPR_START_OR_OP) {
        if (c->category == CAT_FUNC) score += 250;
        else if (c->category == CAT_CONSTANT || c->category == CAT_ANCHOR) score += 200;
        else if (c->category == CAT_CALENDAR_OP || c->category == CAT_TIMEZONE) score -= 150;
        else if (c->category == CAT_UNIT) score -= 100;
    }

    return score;
}

static int compare_candidates(const void *a, const void *b) {
    const Cand *ca = (const Cand *)a;
    const Cand *cb = (const Cand *)b;
    if (ca->score != cb->score) {
        return cb->score - ca->score; /* descending score */
    }
    return (ca->orig_idx > cb->orig_idx) - (ca->orig_idx < cb->orig_idx);
}

bool autocomplete_query_cycle(const char *input, size_t cursor_pos, size_t cycle_idx, AutocompleteSuggestion *out) {
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    if (!input || cursor_pos == 0) return false;

    /* 1. Extract prefix touching cursor */
    size_t start = cursor_pos;
    while (start > 0 && (isalnum((unsigned char)input[start - 1]) || input[start - 1] == '_')) {
        start--;
    }
    size_t prefix_len = cursor_pos - start;
    if (prefix_len < 1) return false;

    char prefix[64];
    if (prefix_len >= sizeof(prefix)) return false;
    memcpy(prefix, input + start, prefix_len);
    prefix[prefix_len] = '\0';

    /* Avoid autocompleting pure numbers */
    bool all_digits = true;
    for (size_t i = 0; i < prefix_len; i++) {
        if (!isdigit((unsigned char)prefix[i])) {
            all_digits = false;
            break;
        }
    }
    if (all_digits) return false;

    /* Detect context based on preceding tokens */
    AutocompleteContext ctx = detect_autocomplete_context(input, start);

    /* Collect all matching candidates */
    Cand cands[MAX_AUTOCOMP_MATCHES];
    size_t match_count = 0;

    /* 2. Check user-defined dynamic symbols first */
    CalcSymbol user_syms[MAX_SYMBOLS];
    int sym_cnt = get_all_symbols(user_syms, MAX_SYMBOLS);
    for (int i = 0; i < sym_cnt; i++) {
        const char *name = user_syms[i].name;
        size_t n_len = strlen(name);
        if (n_len > prefix_len && strncasecmp(name, prefix, prefix_len) == 0) {
            if (match_count < MAX_AUTOCOMP_MATCHES) {
                snprintf(cands[match_count].name, sizeof(cands[match_count].name), "%.31s", name);
                cands[match_count].is_func = user_syms[i].is_func;
                cands[match_count].category = user_syms[i].is_func ? CAT_FUNC : CAT_CONSTANT;
                cands[match_count].dim = DIM_UNKNOWN;
                cands[match_count].orig_idx = match_count;
                cands[match_count].score = score_candidate(&cands[match_count], &ctx, prefix);
                match_count++;
            }
        }
    }

    /* 3. Check built-in table */
    for (size_t i = 0; i < NUM_BUILTIN_COMPLETIONS; i++) {
        const CompletionEntry *entry = &BUILTIN_COMPLETIONS[i];
        size_t n_len = strlen(entry->name);
        if (n_len > prefix_len && strncasecmp(entry->name, prefix, prefix_len) == 0) {
            /* Avoid duplicates with dynamic symbols */
            bool dup = false;
            for (size_t d = 0; d < match_count; d++) {
                if (strcasecmp(cands[d].name, entry->name) == 0) {
                    dup = true;
                    break;
                }
            }
            if (!dup && match_count < MAX_AUTOCOMP_MATCHES) {
                snprintf(cands[match_count].name, sizeof(cands[match_count].name), "%s", entry->name);
                cands[match_count].is_func = entry->is_func;
                cands[match_count].category = entry->category;
                cands[match_count].dim = entry->dim;
                cands[match_count].orig_idx = match_count;
                cands[match_count].score = score_candidate(&cands[match_count], &ctx, prefix);
                match_count++;
            }
        }
    }

    if (match_count == 0) return false;

    /* Sort candidates by context score descending */
    qsort(cands, match_count, sizeof(Cand), compare_candidates);

    /* Select candidate according to cycle_idx */
    size_t sel_idx = cycle_idx % match_count;
    out->match_count = match_count;
    out->current_index = sel_idx;
    out->prefix_len = prefix_len;
    out->is_func = cands[sel_idx].is_func;
    out->active = true;

    for (size_t i = 0; i < match_count; i++) {
        snprintf(out->candidate_names[i], sizeof(out->candidate_names[i]), "%.31s", cands[i].name);
        out->candidate_is_func[i] = cands[i].is_func;
    }

    const char *sel_name = cands[sel_idx].name;
    size_t sel_len = strlen(sel_name);
    if (out->is_func) {
        snprintf(out->full_completion, sizeof(out->full_completion), "%.31s()", sel_name);
        snprintf(out->ghost_suffix, sizeof(out->ghost_suffix), "%.31s()", sel_name + prefix_len);
        out->cursor_offset = sel_len + 1; /* inside name(|) */
    } else {
        snprintf(out->full_completion, sizeof(out->full_completion), "%.31s", sel_name);
        snprintf(out->ghost_suffix, sizeof(out->ghost_suffix), "%.31s", sel_name + prefix_len);
        out->cursor_offset = sel_len;
    }

    /* Build preview bar if multiple candidates */
    if (match_count > 1) {
        char bar[256];
        int n = snprintf(bar, sizeof(bar), "Tab: ");
        size_t show_count = match_count > 4 ? 4 : match_count;
        for (size_t i = 0; i < show_count && n < (int)sizeof(bar) - 1; i++) {
            if (i > 0) {
                n += snprintf(bar + n, sizeof(bar) - (size_t)n, "  •  ");
            }
            if (i == sel_idx) {
                n += snprintf(bar + n, sizeof(bar) - (size_t)n, "[%.31s%s]",
                              cands[i].name, cands[i].is_func ? "()" : "");
            } else {
                n += snprintf(bar + n, sizeof(bar) - (size_t)n, "%.31s%s",
                              cands[i].name, cands[i].is_func ? "()" : "");
            }
        }
        if (match_count > show_count && n < (int)sizeof(bar) - 1) {
            n += snprintf(bar + n, sizeof(bar) - (size_t)n, "  •  ...");
        }
        if (n < (int)sizeof(bar) - 1) {
            snprintf(bar + n, sizeof(bar) - (size_t)n, " (%u/%u)",
                     (unsigned int)(sel_idx + 1), (unsigned int)match_count);
        }
        snprintf(out->preview_bar, sizeof(out->preview_bar), "%s", bar);
    } else {
        out->preview_bar[0] = '\0';
    }

    return true;
}

bool autocomplete_query(const char *input, size_t cursor_pos, AutocompleteSuggestion *out) {
    return autocomplete_query_cycle(input, cursor_pos, 0, out);
}

bool autocomplete_commit(char *buf, size_t buf_sz, size_t *cursor_pos, size_t *input_len, const AutocompleteSuggestion *sug) {
    if (!sug || !sug->active || !buf || !cursor_pos || !input_len) return false;
    size_t c_pos = *cursor_pos;
    size_t p_len = sug->prefix_len;
    if (c_pos < p_len) return false;

    size_t start_idx = c_pos - p_len;
    size_t comp_len = strlen(sug->full_completion);
    size_t tail_len = *input_len - c_pos;

    if (start_idx + comp_len + tail_len + 1 > buf_sz) return false;

    /* Move tail */
    memmove(buf + start_idx + comp_len, buf + c_pos, tail_len);
    /* Copy full completion over the prefix */
    memcpy(buf + start_idx, sug->full_completion, comp_len);

    *input_len = start_idx + comp_len + tail_len;
    buf[*input_len] = '\0';

    *cursor_pos = start_idx + sug->cursor_offset;
    return true;
}

bool autocomplete_cycle_replace(char *buf, size_t buf_sz, size_t *cursor_pos, size_t *input_len,
                                size_t prev_comp_len, const AutocompleteSuggestion *sug) {
    if (!sug || !sug->active || !buf || !cursor_pos || !input_len) return false;
    size_t c_pos = *cursor_pos;
    if (c_pos < prev_comp_len) return false;

    size_t start_idx = c_pos - prev_comp_len;
    size_t comp_len = strlen(sug->full_completion);
    size_t tail_len = *input_len - c_pos;

    if (start_idx + comp_len + tail_len + 1 > buf_sz) return false;

    memmove(buf + start_idx + comp_len, buf + c_pos, tail_len);
    memcpy(buf + start_idx, sug->full_completion, comp_len);

    *input_len = start_idx + comp_len + tail_len;
    buf[*input_len] = '\0';

    *cursor_pos = start_idx + sug->cursor_offset;
    return true;
}

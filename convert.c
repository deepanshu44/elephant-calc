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
#include <ctype.h>
#include <math.h>
#include <stdint.h>
#include <time.h>

#define MAX_UNITS 256
static UnitItem g_units[MAX_UNITS];
static int g_unit_count = 0;

static const char *DEFAULT_UNITS_CONF =
"# ==============================================================================\n"
"# Elephant Calc - Unit Definitions\n"
"# Format: <symbol> = <factor_to_base>, <alias1>, <alias2>...\n"
"# ==============================================================================\n\n"
"[length] # Base: meter (m)\n"
"m      = 1.0           , meter, meters\n"
"km     = 1000.0        , kilometer, kilometers\n"
"cm     = 0.01          , centimeter, centimeters\n"
"mm     = 0.001         , millimeter, millimeters\n"
"mi     = 1609.344      , mile, miles\n"
"yd     = 0.9144        , yard, yards\n"
"ft     = 0.3048        , foot, feet\n"
"in     = 0.0254        , inch, inches\n\n"
"[mass] # Base: gram (g)\n"
"g      = 1.0           , gram, grams\n"
"kg     = 1000.0        , kilogram, kilograms\n"
"mg     = 0.001         , milligram, milligrams\n"
"lb     = 453.59237     , pound, pounds, lbs\n"
"oz     = 28.349523125  , ounce, ounces\n"
"ton    = 1000000.0     , metric ton, tons\n\n"
"[time] # Base: second (s)\n"
"s      = 1.0           , second, seconds, sec\n"
"min    = 60.0          , minute, minutes\n"
"h      = 3600.0        , hour, hours, hr\n"
"d      = 86400.0       , day, days\n"
"week   = 604800.0      , week, weeks\n"
"month  = 2592000.0     , month, months\n"
"year   = 31536000.0    , year, years\n\n"
"[data] # Base: byte (B)\n"
"b      = 1.0           , byte, bytes\n"
"kb     = 1000.0        , kilobyte, kilobytes\n"
"mb     = 1000000.0     , megabyte, megabytes\n"
"gb     = 1000000000.0  , gigabyte, gigabytes\n"
"tb     = 1000000000000.0, terabyte, terabytes\n"
"kib    = 1024.0        , kibibyte\n"
"mib    = 1048576.0     , mebibyte\n"
"gib    = 1073741824.0  , gibibyte\n"
"tib    = 1099511627776.0, tebibyte\n\n"
"[temperature] # Affine conversion\n"
"c      = celsius, centigrade\n"
"f      = fahrenheit\n"
"k      = kelvin\n\n"
"[currency] # Base: USD (Rates per 1 USD)\n"
"usd    = 1.0           , dollar, dollars, $\n"
"inr    = 96.8672       , rupee, rupees, inr, ₹\n"
"eur    = 0.8924        , euro, euros, €\n"
"gbp    = 0.7558        , pound, pounds, £\n"
"jpy    = 158.2858      , yen, ¥\n"
"cad    = 1.4253        , canadian dollar\n"
"aud    = 1.4330        , australian dollar\n"
"cny    = 6.7065        , yuan, rmb\n"
"chf    = 0.8307        , franc, swiss franc\n"
"aed    = 3.6725        , dirham\n"
"sgd    = 1.2807        , singapore dollar\n\n"
"[speed] # Base: meter per second (m/s)\n"
"m/s    = 1.0           , mps, meter/second, meters/second\n"
"km/h   = 0.2777777777777778, kmh, kph, kilometer/hour, kilometers/hour\n"
"mph    = 0.44704       , mi/h, mile/hour, miles/hour\n"
"knot   = 0.5144444444444445, knots, kt\n"
"ft/s   = 0.3048        , fps, foot/second, feet/second\n\n"
"[area] # Base: square meter (sqm)\n"
"sqm    = 1.0           , m2, m^2, square meter, square meters\n"
"sqkm   = 1000000.0     , km2, km^2, square kilometer, square kilometers\n"
"sqft   = 0.09290304    , ft2, ft^2, square foot, square feet, sq ft\n"
"sqin   = 0.00064516    , in2, in^2, square inch, square inches, sq in\n"
"sqyd   = 0.83612736    , yd2, yd^2, square yard, square yards, sq yd\n"
"acre   = 4046.8564224  , acres\n"
"hectare= 10000.0       , hectares, ha\n"
"sqmi   = 2589988.110336, mi2, mi^2, square mile, square miles, sq mi\n\n"
"[data_rate] # Base: bit per second (bps)\n"
"bps    = 1.0           , bit/s, bits/s\n"
"kbps   = 1000.0        , kbit/s, kilobit/s\n"
"mbps   = 1000000.0     , mbit/s, megabyte/second, megabit/s\n"
"gbps   = 1000000000.0  , gbit/s, gigabit/s\n"
"tbps   = 1000000000000.0, tbit/s, terabit/s\n"
"B/s    = 8.0           , byte/s, bytes/s\n"
"KB/s   = 8000.0        , kB/s, kbyte/s, kilobytes/second\n"
"MB/s   = 8000000.0     , mB/s, mbyte/s, megabytes/second\n"
"GB/s   = 8000000000.0  , gB/s, gbyte/s, gigabytes/second\n\n"
"[pressure] # Base: Pascal (Pa)\n"
"pa     = 1.0           , pascal, pascals\n"
"kpa    = 1000.0        , kilopascal, kilopascals\n"
"bar    = 100000.0      , bars\n"
"psi    = 6894.757293168, pound/sq in\n"
"atm    = 101325.0      , atmosphere, atmospheres\n";

static void trim_str(char *s) {
    if (!s) return;
    char *p = s;
    while (*p && isspace((unsigned char)*p)) p++;
    if (p != s) memmove(s, p, strlen(p) + 1);
    size_t len = strlen(s);
    while (len > 0 && isspace((unsigned char)s[len - 1])) {
        s[--len] = '\0';
    }
}

static bool str_case_eq(const char *a, const char *b) {
    if (!a || !b) return false;
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return false;
        a++;
        b++;
    }
    return (*a == '\0' && *b == '\0');
}

static void add_unit(Dimension dim, const char *symbol, double factor, char aliases[MAX_ALIASES][32], int alias_cnt) {
    for (int i = 0; i < g_unit_count; i++) {
        if (str_case_eq(g_units[i].symbol, symbol)) {
            g_units[i].dim = dim;
            g_units[i].to_base = factor;
            g_units[i].alias_count = alias_cnt > MAX_ALIASES ? MAX_ALIASES : alias_cnt;
            for (int a = 0; a < g_units[i].alias_count; a++) {
                snprintf(g_units[i].aliases[a], sizeof(g_units[i].aliases[a]), "%.31s", aliases[a]);
            }
            return;
        }
    }
    if (g_unit_count >= MAX_UNITS) return;
    UnitItem *u = &g_units[g_unit_count++];
    snprintf(u->symbol, sizeof(u->symbol), "%.31s", symbol);
    u->dim = dim;
    u->to_base = factor;
    u->alias_count = alias_cnt > MAX_ALIASES ? MAX_ALIASES : alias_cnt;
    for (int i = 0; i < u->alias_count; i++) {
        snprintf(u->aliases[i], sizeof(u->aliases[i]), "%.31s", aliases[i]);
    }
}

static void load_units_from_stream(FILE *f) {
    char line[256];
    Dimension cur_dim = DIM_UNKNOWN;

    while (fgets(line, sizeof(line), f)) {
        trim_str(line);
        if (line[0] == '\0' || line[0] == '#') continue;

        if (line[0] == '[') {
            char *close = strchr(line, ']');
            if (close) {
                *close = '\0';
                char *sec = line + 1;
                trim_str(sec);
                if (str_case_eq(sec, "length")) cur_dim = DIM_LENGTH;
                else if (str_case_eq(sec, "mass")) cur_dim = DIM_MASS;
                else if (str_case_eq(sec, "time")) cur_dim = DIM_TIME;
                else if (str_case_eq(sec, "data")) cur_dim = DIM_DATA;
                else if (str_case_eq(sec, "temperature")) cur_dim = DIM_TEMP;
                else if (str_case_eq(sec, "currency")) cur_dim = DIM_CURRENCY;
                else if (str_case_eq(sec, "speed")) cur_dim = DIM_SPEED;
                else if (str_case_eq(sec, "area")) cur_dim = DIM_AREA;
                else if (str_case_eq(sec, "data_rate") || str_case_eq(sec, "datarate")) cur_dim = DIM_DATA_RATE;
                else if (str_case_eq(sec, "pressure")) cur_dim = DIM_PRESSURE;
                else cur_dim = DIM_UNKNOWN;
            }
            continue;
        }

        if (cur_dim == DIM_UNKNOWN) continue;

        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        char *sym = line;
        char *rest = eq + 1;
        trim_str(sym);
        trim_str(rest);
        if (sym[0] == '\0' || rest[0] == '\0') continue;

        double factor = 1.0;
        char aliases[MAX_ALIASES][32];
        int alias_cnt = 0;

        if (cur_dim == DIM_TEMP) {
            /* Temp format: c = celsius, centigrade */
            char *tok = strtok(rest, ",");
            while (tok && alias_cnt < MAX_ALIASES) {
                trim_str(tok);
                if (tok[0]) snprintf(aliases[alias_cnt++], sizeof(aliases[0]), "%s", tok);
                tok = strtok(NULL, ",");
            }
        } else {
            /* Format: km = 1000.0, kilometer, kilometers */
            char *tok = strtok(rest, ",");
            if (!tok) continue;
            trim_str(tok);
            factor = strtod(tok, NULL);
            if (factor <= 0.0) factor = 1.0;

            tok = strtok(NULL, ",");
            while (tok && alias_cnt < MAX_ALIASES) {
                trim_str(tok);
                if (tok[0]) snprintf(aliases[alias_cnt++], sizeof(aliases[0]), "%s", tok);
                tok = strtok(NULL, ",");
            }
        }

        add_unit(cur_dim, sym, factor, aliases, alias_cnt);
    }
}

void init_units(void) {
    if (g_unit_count > 0) return;

    /* Always load default built-in units first */
    FILE *def_f = fmemopen((void *)DEFAULT_UNITS_CONF, strlen(DEFAULT_UNITS_CONF), "r");
    if (def_f) {
        load_units_from_stream(def_f);
        fclose(def_f);
    }

    char path[512];
    get_elephant_file_path("units.conf", path, sizeof(path));

    FILE *f = fopen(path, "r");
    if (!f) {
        /* Create default units.conf */
        FILE *out = fopen(path, "w");
        if (out) {
            fputs(DEFAULT_UNITS_CONF, out);
            fclose(out);
        }
    } else {
        /* User config exists: load overrides/custom units */
        load_units_from_stream(f);
        fclose(f);
    }

    load_cached_currency_rates();
}

void load_cached_currency_rates(void) {
    char path[512];
    get_elephant_file_path("currency_rates.conf", path, sizeof(path));
    FILE *f = fopen(path, "r");
    if (!f) return;
    load_units_from_stream(f);
    fclose(f);
}

bool update_currency_rate(const char *symbol, double rate_per_usd) {
    if (!symbol || !*symbol || rate_per_usd <= 0.0) return false;
    for (int i = 0; i < g_unit_count; i++) {
        if (g_units[i].dim == DIM_CURRENCY && str_case_eq(g_units[i].symbol, symbol)) {
            g_units[i].to_base = rate_per_usd;
            return true;
        }
    }
    char aliases[MAX_ALIASES][32] = {{0}};
    add_unit(DIM_CURRENCY, symbol, rate_per_usd, aliases, 0);
    return true;
}

const UnitItem *find_unit(const char *name) {
    if (!name || !*name) return NULL;
    for (int i = 0; i < g_unit_count; i++) {
        if (str_case_eq(g_units[i].symbol, name)) return &g_units[i];
        for (int a = 0; a < g_units[i].alias_count; a++) {
            if (str_case_eq(g_units[i].aliases[a], name)) return &g_units[i];
        }
    }
    return NULL;
}

static const UnitItem *extract_unit_suffix(const char *str, size_t *unit_start_idx) {
    if (!str || !*str) return NULL;
    size_t len = strlen(str);
    while (len > 0 && isspace((unsigned char)str[len - 1])) len--;
    if (len == 0) return NULL;

    size_t max_test_len = len < 32 ? len : 32;
    for (size_t slen = max_test_len; slen >= 1; slen--) {
        size_t start = len - slen;
        if (start > 0) {
            char prev = str[start - 1];
            char first = str[start];
            if (isalpha((unsigned char)prev) && isalpha((unsigned char)first)) {
                continue;
            }
        }

        char candidate[64];
        memcpy(candidate, str + start, slen);
        candidate[slen] = '\0';
        trim_str(candidate);

        const UnitItem *u = find_unit(candidate);
        if (u) {
            if (unit_start_idx) *unit_start_idx = start;
            return u;
        }
    }
    return NULL;
}

const char *dim_name(Dimension d) {
    switch (d) {
        case DIM_LENGTH: return "length";
        case DIM_MASS: return "mass";
        case DIM_TIME: return "time";
        case DIM_DATA: return "data";
        case DIM_TEMP: return "temperature";
        case DIM_CURRENCY: return "currency";
        case DIM_SPEED: return "speed";
        case DIM_AREA: return "area";
        case DIM_DATA_RATE: return "data rate";
        case DIM_PRESSURE: return "pressure";
        default: return "unknown";
    }
}

static double convert_temp(double val, const char *from_sym, const char *to_sym) {
    /* Convert from source to Celsius */
    double c_val = val;
    if (str_case_eq(from_sym, "f")) {
        c_val = (val - 32.0) / 1.8;
    } else if (str_case_eq(from_sym, "k")) {
        c_val = val - 273.15;
    }

    /* Convert Celsius to target */
    if (str_case_eq(to_sym, "f")) {
        return c_val * 1.8 + 32.0;
    } else if (str_case_eq(to_sym, "k")) {
        return c_val + 273.15;
    }
    return c_val;
}

static bool has_outer_matching_brackets(const char *s, size_t len) {
    if (len < 2 || s[0] != '(' || s[len - 1] != ')') return false;
    int depth = 0;
    for (size_t i = 0; i < len - 1; i++) {
        if (s[i] == '(') depth++;
        else if (s[i] == ')') {
            depth--;
            if (depth == 0) return false;
        }
    }
    return depth == 1;
}

static bool extract_source_timezone(const char *left_expr, char *base_expr, size_t base_sz,
                                    char *src_tz, size_t tz_sz) {
    if (!left_expr || !*left_expr) return false;
    size_t len = strlen(left_expr);

    for (int words = 3; words >= 1; words--) {
        size_t p = len;
        int count = 0;
        while (p > 0) {
            while (p > 0 && isspace((unsigned char)left_expr[p - 1])) p--;
            if (p == 0) break;
            while (p > 0 && !isspace((unsigned char)left_expr[p - 1])) p--;
            count++;
            if (count == words) {
                const char *candidate = left_expr + p;
                while (*candidate && isspace((unsigned char)*candidate)) candidate++;
                if (is_valid_timezone_query(candidate)) {
                    size_t pre_len = p;
                    while (pre_len > 0 && isspace((unsigned char)left_expr[pre_len - 1])) pre_len--;
                    if (pre_len > 0) {
                        /* Strip trailing " in" or " at" keyword if present (e.g. "5pm in est" -> "5pm") */
                        if (pre_len >= 3 && strncasecmp(left_expr + pre_len - 3, " in", 3) == 0) {
                            pre_len -= 3;
                            while (pre_len > 0 && isspace((unsigned char)left_expr[pre_len - 1])) pre_len--;
                        } else if (pre_len >= 3 && strncasecmp(left_expr + pre_len - 3, " at", 3) == 0) {
                            pre_len -= 3;
                            while (pre_len > 0 && isspace((unsigned char)left_expr[pre_len - 1])) pre_len--;
                        }
                        if (pre_len > 0 && base_sz > pre_len) {
                            memcpy(base_expr, left_expr, pre_len);
                            base_expr[pre_len] = '\0';
                        } else if (pre_len == 0 && base_sz > 0) {
                            base_expr[0] = '\0';
                        } else {
                            return false;
                        }
                        snprintf(src_tz, tz_sz, "%s", candidate);
                        return true;
                    }
                }
                break;
            }
        }
    }
    return false;
}

static bool split_timezone_with_math(const char *right, char *tz_out, size_t tz_sz,
                                     char *math_out, size_t math_sz) {
    if (!right || !*right) return false;
    size_t rlen = strlen(right);

    const char *best_op = NULL;
    char best_tz[64] = "";

    for (size_t i = 0; i < rlen; i++) {
        if (right[i] == '+' || right[i] == '-') {
            char prefix[64];
            if (i >= sizeof(prefix)) continue;
            memcpy(prefix, right, i);
            prefix[i] = '\0';
            trim_str(prefix);
            if (prefix[0] != '\0' && is_valid_timezone_query(prefix)) {
                snprintf(best_tz, sizeof(best_tz), "%s", prefix);
                best_op = right + i;
            }
        }
    }

    if (best_op && best_tz[0]) {
        snprintf(tz_out, tz_sz, "%s", best_tz);
        snprintf(math_out, math_sz, "%s", best_op);
        trim_str(math_out);
        return true;
    }
    return false;
}

static inline bool is_business_days_unit(const char *str) {
    if (!str || !*str) return false;
    if (strcasestr(str, "business") || strcasestr(str, "work")) return true;
    if (strcasecmp(str, "bday") == 0 || strcasecmp(str, "bdays") == 0 ||
        strcasecmp(str, "bizday") == 0 || strcasecmp(str, "bizdays") == 0 ||
        strcasecmp(str, "workday") == 0 || strcasecmp(str, "workdays") == 0) return true;
    return false;
}

static bool try_process_between_expression(const char *clean_input, double ans_val,
                                           double *converted_val, char *result_buf, size_t res_sz,
                                           char *unit_symbol, size_t unit_sz,
                                           char *words_suffix, size_t suffix_sz, const char **err_msg) {
    size_t in_len = strlen(clean_input);
    const char *b_ptr = NULL;
    int depth = 0;
    for (size_t i = 0; i + 7 <= in_len; i++) {
        if (clean_input[i] == '(') depth++;
        else if (clean_input[i] == ')') { if (depth > 0) depth--; }
        if (depth == 0) {
            if ((i == 0 || isspace((unsigned char)clean_input[i - 1])) &&
                strncasecmp(&clean_input[i], "between", 7) == 0 &&
                isspace((unsigned char)clean_input[i + 7])) {
                b_ptr = clean_input + i;
                break;
            }
        }
    }
    if (!b_ptr) return false;

    char prefix[256] = "";
    size_t pre_len = (size_t)(b_ptr - clean_input);
    if (pre_len >= sizeof(prefix)) pre_len = sizeof(prefix) - 1;
    memcpy(prefix, clean_input, pre_len);
    prefix[pre_len] = '\0';
    trim_str(prefix);

    const char *rest = b_ptr + 7;
    while (*rest && isspace((unsigned char)*rest)) rest++;

    /* Find " and " in rest at depth 0 */
    const char *and_ptr = NULL;
    depth = 0;
    size_t rest_len = strlen(rest);
    for (size_t i = 0; i + 4 <= rest_len; i++) {
        if (rest[i] == '(') depth++;
        else if (rest[i] == ')') { if (depth > 0) depth--; }
        if (depth == 0) {
            if (isspace((unsigned char)rest[i]) &&
                strncasecmp(&rest[i + 1], "and", 3) == 0 &&
                isspace((unsigned char)rest[i + 4])) {
                and_ptr = rest + i + 1;
                break;
            }
        }
    }
    if (!and_ptr) return false;

    char d1_str[256] = "";
    size_t d1_len = (size_t)(and_ptr - rest);
    if (d1_len >= sizeof(d1_str)) d1_len = sizeof(d1_str) - 1;
    memcpy(d1_str, rest, d1_len);
    d1_str[d1_len] = '\0';
    trim_str(d1_str);

    char d2_str[256] = "";
    snprintf(d2_str, sizeof(d2_str), "%s", and_ptr + 3);
    trim_str(d2_str);

    if (d1_str[0] == '\0' || d2_str[0] == '\0') return false;

    double d1_val = 0.0, d2_val = 0.0;
    bool d1_date = false, d1_time = false;
    bool d2_date = false, d2_time = false;
    const char *d1_err = NULL, *d2_err = NULL;

    if (!evaluate_equation_ex(d1_str, ans_val, &d1_val, NULL, 0, &d1_date, &d1_time, &d1_err)) {
        if (err_msg) *err_msg = d1_err ? d1_err : "invalid first date in between";
        return true;
    }
    if (!evaluate_equation_ex(d2_str, ans_val, &d2_val, NULL, 0, &d2_date, &d2_time, &d2_err)) {
        if (err_msg) *err_msg = d2_err ? d2_err : "invalid second date in between";
        return true;
    }

    if (is_business_days_unit(prefix)) {
        int b_days = count_business_days((time_t)round(d1_val), (time_t)round(d2_val));
        *converted_val = (double)b_days;
        snprintf(result_buf, res_sz, "%d %s", b_days, abs(b_days) == 1 ? "business day" : "business days");
        if (unit_symbol && unit_sz > 0) snprintf(unit_symbol, unit_sz, "business days");
        if (words_suffix && suffix_sz > 0) snprintf(words_suffix, suffix_sz, "Business Days");
        if (err_msg) *err_msg = NULL;
        return true;
    }

    const UnitItem *tu = prefix[0] ? find_unit(prefix) : find_unit("days");
    if (!tu || tu->dim != DIM_TIME) tu = find_unit("days");

    double diff_sec = (d2_val - d1_val);
    double res_val = diff_sec / (tu ? tu->to_base : 86400.0);
    if (tu && (strcmp(tu->symbol, "d") == 0 || strcmp(tu->symbol, "day") == 0)) {
        res_val = round(res_val);
    }
    *converted_val = res_val;

    const char *sym = tu ? tu->symbol : "days";
    if (tu && tu->alias_count > 0) {
        if (fabs(res_val) == 1.0) sym = tu->aliases[0];
        else if (tu->alias_count > 1) sym = tu->aliases[1];
        else sym = tu->aliases[0];
    }

    char comp_buf[128];
    const char *tu_sym = tu ? tu->symbol : "d";
    if (format_compound_unit_downward(res_val, tu_sym, comp_buf, sizeof(comp_buf))) {
        snprintf(result_buf, res_sz, "%s", comp_buf);
    } else {
        char num_str[64];
        format_result_ex(res_val, comma_fmt, num_str, sizeof(num_str));
        snprintf(result_buf, res_sz, "%s %s", num_str, sym);
    }
    if (unit_symbol && unit_sz > 0) snprintf(unit_symbol, unit_sz, "%s", tu ? tu->symbol : "days");
    if (words_suffix && suffix_sz > 0) snprintf(words_suffix, suffix_sz, "%s", sym);
    if (err_msg) *err_msg = NULL;
    return true;
}

static bool try_process_until_since_expression(const char *clean_input, double ans_val,
                                               double *converted_val, char *result_buf, size_t res_sz,
                                               char *unit_symbol, size_t unit_sz,
                                               char *words_suffix, size_t suffix_sz, const char **err_msg) {
    size_t in_len = strlen(clean_input);
    const char *op_ptr = NULL;
    bool is_until = true;
    size_t op_len = 0;
    int depth = 0;

    for (size_t i = 0; i < in_len; i++) {
        if (clean_input[i] == '(') depth++;
        else if (clean_input[i] == ')') { if (depth > 0) depth--; }
        if (depth == 0) {
            if (i + 5 <= in_len && (i == 0 || isspace((unsigned char)clean_input[i - 1])) &&
                strncasecmp(&clean_input[i], "until", 5) == 0 &&
                (i + 5 == in_len || isspace((unsigned char)clean_input[i + 5]))) {
                op_ptr = clean_input + i;
                op_len = 5;
                is_until = true;
                break;
            } else if (i + 5 <= in_len && (i == 0 || isspace((unsigned char)clean_input[i - 1])) &&
                       strncasecmp(&clean_input[i], "since", 5) == 0 &&
                       (i + 5 == in_len || isspace((unsigned char)clean_input[i + 5]))) {
                op_ptr = clean_input + i;
                op_len = 5;
                is_until = false;
                break;
            }
        }
    }
    if (!op_ptr) return false;

    char prefix[256] = "";
    size_t pre_len = (size_t)(op_ptr - clean_input);
    if (pre_len >= sizeof(prefix)) pre_len = sizeof(prefix) - 1;
    memcpy(prefix, clean_input, pre_len);
    prefix[pre_len] = '\0';
    trim_str(prefix);

    char target_str[256] = "";
    snprintf(target_str, sizeof(target_str), "%s", op_ptr + op_len);
    trim_str(target_str);
    if (target_str[0] == '\0') return false;

    double target_val = 0.0;
    bool t_date = false, t_time = false;
    const char *t_err = NULL;
    if (!evaluate_equation_ex(target_str, ans_val, &target_val, NULL, 0, &t_date, &t_time, &t_err)) {
        if (err_msg) *err_msg = t_err ? t_err : "invalid target date";
        return true;
    }

    time_t now_epoch = time(NULL);
    if (is_business_days_unit(prefix)) {
        struct tm tm_today;
        localtime_r(&now_epoch, &tm_today);
        tm_today.tm_hour = tm_today.tm_min = tm_today.tm_sec = 0; tm_today.tm_isdst = -1;
        time_t today_epoch = mktime(&tm_today);
        int b_days = is_until ? count_business_days(today_epoch, (time_t)round(target_val))
                              : count_business_days((time_t)round(target_val), today_epoch);
        *converted_val = (double)b_days;
        snprintf(result_buf, res_sz, "%d %s", b_days, abs(b_days) == 1 ? "business day" : "business days");
        if (unit_symbol && unit_sz > 0) snprintf(unit_symbol, unit_sz, "business days");
        if (words_suffix && suffix_sz > 0) snprintf(words_suffix, suffix_sz, "Business Days");
        if (err_msg) *err_msg = NULL;
        return true;
    }

    const UnitItem *tu = prefix[0] ? find_unit(prefix) : find_unit("days");
    if (!tu || tu->dim != DIM_TIME) tu = find_unit("days");

    struct tm tm_tgt;
    time_t tgt_t = (time_t)round(target_val);
    localtime_r(&tgt_t, &tm_tgt);
    bool is_midnight = (tm_tgt.tm_hour == 0 && tm_tgt.tm_min == 0 && tm_tgt.tm_sec == 0);
    time_t ref_epoch = now_epoch;

    if (is_midnight && (strcmp(tu->symbol, "d") == 0 || strcmp(tu->symbol, "day") == 0 ||
                        strcmp(tu->symbol, "week") == 0 || strcmp(tu->symbol, "weeks") == 0 ||
                        strcmp(tu->symbol, "month") == 0 || strcmp(tu->symbol, "months") == 0 ||
                        strcmp(tu->symbol, "year") == 0 || strcmp(tu->symbol, "years") == 0)) {
        struct tm tm_today;
        localtime_r(&now_epoch, &tm_today);
        tm_today.tm_hour = tm_today.tm_min = tm_today.tm_sec = 0; tm_today.tm_isdst = -1;
        ref_epoch = mktime(&tm_today);
    }

    double delta_sec = is_until ? (target_val - (double)ref_epoch) : ((double)ref_epoch - target_val);
    double res_val = delta_sec / (tu ? tu->to_base : 86400.0);
    if (tu && (strcmp(tu->symbol, "d") == 0 || strcmp(tu->symbol, "day") == 0)) {
        res_val = round(res_val);
    }
    *converted_val = res_val;

    const char *sym = tu ? tu->symbol : "days";
    if (tu && tu->alias_count > 0) {
        if (fabs(res_val) == 1.0) sym = tu->aliases[0];
        else if (tu->alias_count > 1) sym = tu->aliases[1];
        else sym = tu->aliases[0];
    }

    char comp_buf[128];
    const char *tu_sym = tu ? tu->symbol : "d";
    if (format_compound_unit_downward(res_val, tu_sym, comp_buf, sizeof(comp_buf))) {
        snprintf(result_buf, res_sz, "%s", comp_buf);
    } else {
        char num_str[64];
        format_result_ex(res_val, comma_fmt, num_str, sizeof(num_str));
        snprintf(result_buf, res_sz, "%s %s", num_str, sym);
    }
    if (unit_symbol && unit_sz > 0) snprintf(unit_symbol, unit_sz, "%s", tu ? tu->symbol : "days");
    if (words_suffix && suffix_sz > 0) snprintf(words_suffix, suffix_sz, "%s", sym);
    if (err_msg) *err_msg = NULL;
    return true;
}

static bool try_process_relative_offset_expression(const char *clean_input, double ans_val,
                                                   double *converted_val, char *result_buf, size_t res_sz,
                                                   char *unit_symbol, size_t unit_sz,
                                                   char *words_suffix, size_t suffix_sz, const char **err_msg) {
    size_t in_len = strlen(clean_input);
    const char *op_ptr = NULL;
    size_t op_len = 0;
    bool is_before = false;
    int depth = 0;

    for (size_t i = 0; i < in_len; i++) {
        if (clean_input[i] == '(') depth++;
        else if (clean_input[i] == ')') { if (depth > 0) depth--; }
        if (depth == 0) {
            if (i + 6 <= in_len && i > 0 && isspace((unsigned char)clean_input[i - 1]) &&
                strncasecmp(&clean_input[i], "before", 6) == 0 &&
                isspace((unsigned char)clean_input[i + 6])) {
                op_ptr = clean_input + i;
                op_len = 6;
                is_before = true;
                break;
            } else if (i + 5 <= in_len && i > 0 && isspace((unsigned char)clean_input[i - 1]) &&
                       strncasecmp(&clean_input[i], "after", 5) == 0 &&
                       isspace((unsigned char)clean_input[i + 5])) {
                op_ptr = clean_input + i;
                op_len = 5;
                is_before = false;
                break;
            } else if (i + 4 <= in_len && i > 0 && isspace((unsigned char)clean_input[i - 1]) &&
                       strncasecmp(&clean_input[i], "from", 4) == 0 &&
                       isspace((unsigned char)clean_input[i + 4])) {
                op_ptr = clean_input + i;
                op_len = 4;
                is_before = false;
                break;
            }
        }
    }
    if (!op_ptr) return false;

    char duration_str[256] = "";
    size_t dur_len = (size_t)(op_ptr - clean_input);
    if (dur_len >= sizeof(duration_str)) dur_len = sizeof(duration_str) - 1;
    memcpy(duration_str, clean_input, dur_len);
    duration_str[dur_len] = '\0';
    trim_str(duration_str);

    char date_str[256] = "";
    snprintf(date_str, sizeof(date_str), "%s", op_ptr + op_len);
    trim_str(date_str);

    if (duration_str[0] == '\0' || date_str[0] == '\0') return false;

    double d_val = 0.0;
    bool d_is_date = false, d_is_time = false;
    char d_unit[32] = "";
    if (!evaluate_equation_ex(date_str, ans_val, &d_val, d_unit, sizeof(d_unit), &d_is_date, &d_is_time, NULL)) {
        return false;
    }
    if (!d_is_date && !d_is_time && d_val < 100000000.0) {
        return false;
    }

    char rewrite_expr[600];
    if (is_before) {
        snprintf(rewrite_expr, sizeof(rewrite_expr), "(%s) - (%s)", date_str, duration_str);
    } else {
        snprintf(rewrite_expr, sizeof(rewrite_expr), "(%s) + (%s)", date_str, duration_str);
    }

    double res_val = 0.0;
    bool res_date = false, res_time = false;
    char res_unit[32] = "";
    const char *r_err = NULL;

    if (!evaluate_equation_ex(rewrite_expr, ans_val, &res_val, res_unit, sizeof(res_unit), &res_date, &res_time, &r_err)) {
        if (err_msg) *err_msg = r_err ? r_err : "failed to evaluate relative date";
        return true;
    }

    *converted_val = res_val;
    if (res_unit[0] && is_valid_timezone_query(res_unit)) {
        TimezoneResult tz_res;
        if (convert_to_timezone((time_t)round(res_val), res_unit, &tz_res)) {
            snprintf(result_buf, res_sz, "%s %s", tz_res.time_str, tz_res.abbrev);
            if (unit_symbol && unit_sz > 0) snprintf(unit_symbol, unit_sz, "%s", tz_res.abbrev);
            if (words_suffix && suffix_sz > 0) snprintf(words_suffix, suffix_sz, "%s", tz_res.name);
        } else {
            format_date_result(res_val, result_buf, res_sz);
        }
    } else if (res_time) {
        format_time_result(res_val, false, result_buf, res_sz);
    } else {
        format_date_result(res_val, result_buf, res_sz);
    }

    if (words_suffix && suffix_sz > 0 && !res_unit[0]) {
        time_t t = (time_t)round(res_val);
        struct tm tm_info;
        localtime_r(&t, &tm_info);
        strftime(words_suffix, suffix_sz, "%A", &tm_info);
    }

    if (err_msg) *err_msg = NULL;
    return true;
}

bool try_convert_expression(const char *input, double ans_val,
                            double *converted_val, char *result_buf, size_t res_sz,
                            char *unit_symbol, size_t unit_sz,
                            char *words_suffix, size_t suffix_sz, const char **err_msg) {
    if (!input || !*input) return false;

    char clean_input[256];
    snprintf(clean_input, sizeof(clean_input), "%s", input);
    trim_str(clean_input);

    size_t in_len = strlen(clean_input);
    while (has_outer_matching_brackets(clean_input, in_len)) {
        clean_input[in_len - 1] = '\0';
        memmove(clean_input, clean_input + 1, in_len - 1);
        trim_str(clean_input);
        in_len = strlen(clean_input);
    }

    /* Do not hijack assignments (lhs := rhs or lhs = rhs) */
    int p_depth = 0;
    for (const char *s = clean_input; *s; s++) {
        if (*s == '(') p_depth++;
        else if (*s == ')') { if (p_depth > 0) p_depth--; }
        else if (p_depth == 0) {
            if (*s == ':' && *(s + 1) == '=') return false;
            if (*s == '=' && (s == clean_input || (*(s - 1) != '!' && *(s - 1) != '<' && *(s - 1) != '>' && *(s - 1) != ':')) && *(s + 1) != '=') return false;
        }
    }

    if (strcasecmp(clean_input, "vars") == 0 || strcasecmp(clean_input, "saved") == 0) {
        char list_buf[1024] = "";
        int cnt = format_saved_symbols_list(list_buf, sizeof(list_buf));
        *converted_val = (double)cnt;
        if (cnt == 0) {
            snprintf(result_buf, res_sz, "No saved variables or functions");
        } else {
            snprintf(result_buf, res_sz, "%s", list_buf);
        }
        if (unit_symbol && unit_sz > 0) snprintf(unit_symbol, unit_sz, "%d symbols", cnt);
        if (words_suffix && suffix_sz > 0) words_suffix[0] = '\0';
        if (err_msg) *err_msg = NULL;
        return true;
    }
    if (strncasecmp(clean_input, "delete ", 7) == 0 || strncasecmp(clean_input, "undef ", 6) == 0 || strncasecmp(clean_input, "remove ", 7) == 0) {
        const char *target = clean_input + (clean_input[0] == 'u' || clean_input[0] == 'U' ? 6 : 7);
        while (*target && isspace((unsigned char)*target)) target++;
        char t_name[32];
        size_t t_i = 0;
        while (target[t_i] && !isspace((unsigned char)target[t_i]) && t_i < 31) {
            t_name[t_i] = target[t_i];
            t_i++;
        }
        t_name[t_i] = '\0';
        if (delete_symbol(t_name)) {
            save_saved_symbols();
            *converted_val = 0.0;
            snprintf(result_buf, res_sz, "Deleted %s", t_name);
            if (unit_symbol && unit_sz > 0) snprintf(unit_symbol, unit_sz, "deleted %s", t_name);
            if (words_suffix && suffix_sz > 0) words_suffix[0] = '\0';
            if (err_msg) *err_msg = NULL;
            return true;
        } else {
            if (err_msg) *err_msg = "symbol not found";
            return false;
        }
    }

    if (try_process_between_expression(clean_input, ans_val, converted_val, result_buf, res_sz,
                                       unit_symbol, unit_sz, words_suffix, suffix_sz, err_msg)) {
        return true;
    }
    if (try_process_until_since_expression(clean_input, ans_val, converted_val, result_buf, res_sz,
                                           unit_symbol, unit_sz, words_suffix, suffix_sz, err_msg)) {
        return true;
    }
    if (try_process_relative_offset_expression(clean_input, ans_val, converted_val, result_buf, res_sz,
                                               unit_symbol, unit_sz, words_suffix, suffix_sz, err_msg)) {
        return true;
    }

    /* Search for separator " to " or " in " (case-insensitive) at parenthesis depth 0 */
    const char *sep = NULL;
    size_t sep_len = 0;

    int depth = 0;
    for (size_t i = 0; i + 4 <= in_len; i++) {
        if (clean_input[i] == '(') depth++;
        else if (clean_input[i] == ')') {
            if (depth > 0) depth--;
        }
        if (depth == 0 && clean_input[i] == ' ') {
            if ((clean_input[i+1] == 't' || clean_input[i+1] == 'T') &&
                (clean_input[i+2] == 'o' || clean_input[i+2] == 'O') &&
                clean_input[i+3] == ' ') {
                sep = clean_input + i;
                sep_len = 4;
            } else if ((clean_input[i+1] == 'i' || clean_input[i+1] == 'I') &&
                       (clean_input[i+2] == 'n' || clean_input[i+2] == 'N') &&
                       clean_input[i+3] == ' ') {
                sep = clean_input + i;
                sep_len = 4;
            }
        }
    }

    if (!sep) return false;

    /* Extract Left and Right */
    char left[256];
    char right[128];

    size_t left_len = (size_t)(sep - clean_input);
    if (left_len >= sizeof(left)) left_len = sizeof(left) - 1;
    memcpy(left, clean_input, left_len);
    left[left_len] = '\0';
    trim_str(left);

    snprintf(right, sizeof(right), "%s", sep + sep_len);
    trim_str(right);

    if (left[0] == '\0' || right[0] == '\0') return false;

    /* Check for Programmer Radix & Date target conversions */
    bool is_radix_or_date = false;
    int target_kind = 0; /* 1=hex, 2=bin, 3=oct, 4=dec, 5=date, 6=utc, 7=epoch */
    char target_tz[64] = "";
    char trailing_tz_math[128] = "";
    bool has_tz_math = false;

    if (strcasecmp(right, "hex") == 0 || strcasecmp(right, "hexadecimal") == 0) {
        is_radix_or_date = true;
        target_kind = 1;
    } else if (strcasecmp(right, "bin") == 0 || strcasecmp(right, "binary") == 0) {
        is_radix_or_date = true;
        target_kind = 2;
    } else if (strcasecmp(right, "oct") == 0 || strcasecmp(right, "octal") == 0) {
        is_radix_or_date = true;
        target_kind = 3;
    } else if (strcasecmp(right, "dec") == 0 || strcasecmp(right, "decimal") == 0) {
        is_radix_or_date = true;
        target_kind = 4;
    } else if (strcasecmp(right, "date") == 0) {
        is_radix_or_date = true;
        target_kind = 5;
    } else if (strcasecmp(right, "date24") == 0) {
        is_radix_or_date = true;
        target_kind = 11;
    } else if (strcasecmp(right, "utc") == 0) {
        is_radix_or_date = true;
        target_kind = 6;
    } else if (strcasecmp(right, "epoch") == 0) {
        is_radix_or_date = true;
        target_kind = 7;
    } else if (strcasecmp(right, "weekday") == 0) {
        is_radix_or_date = true;
        target_kind = 8;
    } else if (strcasecmp(right, "time") == 0 || strcasecmp(right, "time12") == 0 || strcasecmp(right, "12h") == 0 || strcasecmp(right, "12hr") == 0) {
        is_radix_or_date = true;
        target_kind = 9;
    } else if (strcasecmp(right, "time24") == 0 || strcasecmp(right, "24h") == 0 || strcasecmp(right, "24hr") == 0) {
        is_radix_or_date = true;
        target_kind = 10;
    } else if (is_valid_timezone_query(right)) {
        is_radix_or_date = true;
        target_kind = 12;
        snprintf(target_tz, sizeof(target_tz), "%s", right);
    } else if (split_timezone_with_math(right, target_tz, sizeof(target_tz), trailing_tz_math, sizeof(trailing_tz_math))) {
        if (strcasestr(trailing_tz_math, " in ") != NULL || strcasestr(trailing_tz_math, " to ") != NULL) {
            if (err_msg) *err_msg = "multiple destination timezones not supported";
            return true;
        }
        is_radix_or_date = true;
        target_kind = 12;
        has_tz_math = true;
    }

    if (is_radix_or_date) {
        if (target_kind == 12) {
            char base_expr[256] = "";
            char src_tz[256] = "";
            bool has_src_tz = extract_source_timezone(left, base_expr, sizeof(base_expr), src_tz, sizeof(src_tz));

            const char *expr_to_eval = has_src_tz ? base_expr : left;
            double in_val = 0.0;
            const char *m_err = NULL;
            bool is_date = false, is_time = false;
            if (strcasecmp(expr_to_eval, "today") == 0) {
                in_val = (double)time(NULL);
                is_date = true;
            } else if (!evaluate_equation_ex(expr_to_eval, ans_val, &in_val, NULL, 0, &is_date, &is_time, &m_err)) {
                /* If left was just a timezone name itself (e.g. "est in tokyo"), treat it as "now" in that timezone */
                if (!has_src_tz && is_valid_timezone_query(left)) {
                    in_val = (double)time(NULL);
                    has_src_tz = true;
                    snprintf(src_tz, sizeof(src_tz), "%s", left);
                } else {
                    if (err_msg) *err_msg = m_err ? m_err : "math error";
                    return true;
                }
            }

            time_t ref_time = (time_t)round(in_val);
            time_t epoch_utc = ref_time;

            if (has_src_tz) {
                struct tm local_tm;
                localtime_r(&ref_time, &local_tm);
                int local_offset_min = (int)(local_tm.tm_gmtoff / 60);

                int src_offset_min = 0;
                char src_abbrev[16] = "";
                if (!parse_source_timezone_offset(src_tz, ref_time, &src_offset_min, src_abbrev, sizeof(src_abbrev))) {
                    if (err_msg) *err_msg = "unknown source timezone";
                    return true;
                }
                epoch_utc = ref_time + (time_t)((local_offset_min - src_offset_min) * 60);
            }

            if (has_tz_math) {
                double shift_sec = 0.0;
                bool s_is_date = false, s_is_time = false;
                char s_unit[32] = "";
                const char *s_err = NULL;
                if (!evaluate_equation_ex(trailing_tz_math, ans_val, &shift_sec, s_unit, sizeof(s_unit),
                                          &s_is_date, &s_is_time, &s_err)) {
                    if (err_msg) *err_msg = s_err ? s_err : "invalid time offset";
                    return true;
                }
                const UnitItem *su = find_unit(s_unit);
                if (s_is_date || (su && su->dim != DIM_TIME)) {
                    if (err_msg) *err_msg = "expected time duration offset";
                    return true;
                }
                if (!su && !s_is_time && fabs(shift_sec) > 0.0) {
                    if (err_msg) *err_msg = "expected time unit in offset (e.g. + 2 weeks, - 3 days)";
                    return true;
                }
                double actual_shift = shift_sec * (su ? su->to_base : 1.0);
                epoch_utc += (time_t)round(actual_shift);
            }

            TimezoneResult tz_res;
            if (!convert_to_timezone_ex(epoch_utc, ref_time, target_tz, &tz_res)) {
                if (err_msg) *err_msg = "unknown target timezone";
                return true;
            }

            *converted_val = (double)epoch_utc;
            snprintf(result_buf, res_sz, "%s %s", tz_res.time_str, tz_res.abbrev);
            if (unit_symbol && unit_sz > 0) snprintf(unit_symbol, unit_sz, "%s", tz_res.abbrev);
            if (words_suffix && suffix_sz > 0) snprintf(words_suffix, suffix_sz, "%s  •  %s", tz_res.name, tz_res.weekday_str);
            if (err_msg) *err_msg = NULL;
            return true;
        }

        double in_val = 0.0;
        const char *m_err = NULL;
        if (!evaluate_equation(left, ans_val, &in_val, &m_err)) {
            if (err_msg) *err_msg = m_err ? m_err : "math error";
            return true;
        }

        *converted_val = in_val;
        if (unit_symbol && unit_sz > 0) unit_symbol[0] = '\0';

        if (target_kind == 1) { /* hex */
            int64_t ival = (int64_t)round(in_val);
            if (ival < 0) {
                snprintf(result_buf, res_sz, "-0x%llX", (unsigned long long)(-ival));
            } else {
                snprintf(result_buf, res_sz, "0x%llX", (unsigned long long)ival);
            }
            if (words_suffix && suffix_sz > 0) snprintf(words_suffix, suffix_sz, "Hexadecimal");
        } else if (target_kind == 2) { /* bin */
            int64_t ival = (int64_t)round(in_val);
            format_bin_string(ival, result_buf, res_sz);
            if (words_suffix && suffix_sz > 0) snprintf(words_suffix, suffix_sz, "Binary");
        } else if (target_kind == 3) { /* oct */
            int64_t ival = (int64_t)round(in_val);
            if (ival < 0) {
                snprintf(result_buf, res_sz, "-0o%llo", (unsigned long long)(-ival));
            } else {
                snprintf(result_buf, res_sz, "0o%llo", (unsigned long long)ival);
            }
            if (words_suffix && suffix_sz > 0) snprintf(words_suffix, suffix_sz, "Octal");
        } else if (target_kind == 4) { /* dec */
            format_result_ex(in_val, comma_fmt, result_buf, res_sz);
            if (words_suffix && suffix_sz > 0) snprintf(words_suffix, suffix_sz, "Decimal");
        } else if (target_kind == 5) { /* date */
            time_t t = (time_t)round(in_val);
            struct tm tm_info;
            localtime_r(&t, &tm_info);
            static const char *m_shorts[] = {
                "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                "Jul", "Aug", "Sept", "Oct", "Nov", "Dec"
            };
            const char *m_str = (tm_info.tm_mon >= 0 && tm_info.tm_mon < 12) ? m_shorts[tm_info.tm_mon] : "Jan";
            if (tm_info.tm_hour == 0 && tm_info.tm_min == 0 && tm_info.tm_sec == 0) {
                snprintf(result_buf, res_sz, "%02d-%s-%04d", tm_info.tm_mday, m_str, tm_info.tm_year + 1900);
            } else {
                int hour12 = tm_info.tm_hour % 12;
                if (hour12 == 0) hour12 = 12;
                const char *am_pm = tm_info.tm_hour >= 12 ? "PM" : "AM";
                snprintf(result_buf, res_sz, "%02d-%s-%04d %02d:%02d:%02d %s",
                         tm_info.tm_mday, m_str, tm_info.tm_year + 1900,
                         hour12, tm_info.tm_min, tm_info.tm_sec, am_pm);
            }
            if (words_suffix && suffix_sz > 0) {
                strftime(words_suffix, suffix_sz, "%A", &tm_info);
            }
        } else if (target_kind == 11) { /* date24 (explicit 24-hour time) */
            time_t t = (time_t)round(in_val);
            struct tm tm_info;
            localtime_r(&t, &tm_info);
            static const char *m_shorts[] = {
                "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                "Jul", "Aug", "Sept", "Oct", "Nov", "Dec"
            };
            const char *m_str = (tm_info.tm_mon >= 0 && tm_info.tm_mon < 12) ? m_shorts[tm_info.tm_mon] : "Jan";
            snprintf(result_buf, res_sz, "%02d-%s-%04d %02d:%02d:%02d",
                     tm_info.tm_mday, m_str, tm_info.tm_year + 1900,
                     tm_info.tm_hour, tm_info.tm_min, tm_info.tm_sec);
            if (words_suffix && suffix_sz > 0) {
                strftime(words_suffix, suffix_sz, "%A", &tm_info);
            }
        } else if (target_kind == 6) { /* utc */
            time_t t = (time_t)round(in_val);
            struct tm tm_info;
            gmtime_r(&t, &tm_info);
            static const char *m_shorts[] = {
                "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                "Jul", "Aug", "Sept", "Oct", "Nov", "Dec"
            };
            const char *m_str = (tm_info.tm_mon >= 0 && tm_info.tm_mon < 12) ? m_shorts[tm_info.tm_mon] : "Jan";
            snprintf(result_buf, res_sz, "%02d-%s-%04d %02d:%02d:%02d UTC",
                     tm_info.tm_mday, m_str, tm_info.tm_year + 1900,
                     tm_info.tm_hour, tm_info.tm_min, tm_info.tm_sec);
            if (unit_symbol && unit_sz > 0) snprintf(unit_symbol, unit_sz, "UTC");
            if (words_suffix && suffix_sz > 0) snprintf(words_suffix, suffix_sz, "Universal Time");
        } else if (target_kind == 7) { /* epoch */
            snprintf(result_buf, res_sz, "%.0f", round(in_val));
            if (unit_symbol && unit_sz > 0) snprintf(unit_symbol, unit_sz, "s");
            if (words_suffix && suffix_sz > 0) snprintf(words_suffix, suffix_sz, "Epoch Seconds");
        } else if (target_kind == 8) { /* weekday */
            time_t t = (time_t)round(in_val);
            struct tm tm_info;
            localtime_r(&t, &tm_info);
            strftime(result_buf, res_sz, "%A", &tm_info);
            if (words_suffix && suffix_sz > 0) {
                static const char *m_shorts[] = {
                    "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                    "Jul", "Aug", "Sept", "Oct", "Nov", "Dec"
                };
                const char *m_str = (tm_info.tm_mon >= 0 && tm_info.tm_mon < 12) ? m_shorts[tm_info.tm_mon] : "Jan";
                snprintf(words_suffix, suffix_sz, "%02d-%s-%04d", tm_info.tm_mday, m_str, tm_info.tm_year + 1900);
            }
        } else if (target_kind == 9) { /* time 12h AM/PM */
            time_t t = (time_t)round(in_val);
            struct tm tm_info;
            localtime_r(&t, &tm_info);
            int h12 = tm_info.tm_hour % 12;
            if (h12 == 0) h12 = 12;
            const char *am_pm = tm_info.tm_hour >= 12 ? "PM" : "AM";
            snprintf(result_buf, res_sz, "%02d:%02d:%02d %s", h12, tm_info.tm_min, tm_info.tm_sec, am_pm);
            if (unit_symbol && unit_sz > 0) snprintf(unit_symbol, unit_sz, "%s", am_pm);
            if (words_suffix && suffix_sz > 0) {
                static const char *m_shorts[] = {
                    "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                    "Jul", "Aug", "Sept", "Oct", "Nov", "Dec"
                };
                const char *m_str = (tm_info.tm_mon >= 0 && tm_info.tm_mon < 12) ? m_shorts[tm_info.tm_mon] : "Jan";
                char weekday_buf[32];
                strftime(weekday_buf, sizeof(weekday_buf), "%A", &tm_info);
                snprintf(words_suffix, suffix_sz, "%s, %02d-%s-%04d", weekday_buf, tm_info.tm_mday, m_str, tm_info.tm_year + 1900);
            }
        } else if (target_kind == 10) { /* time 24h */
            time_t t = (time_t)round(in_val);
            struct tm tm_info;
            localtime_r(&t, &tm_info);
            snprintf(result_buf, res_sz, "%02d:%02d:%02d", tm_info.tm_hour, tm_info.tm_min, tm_info.tm_sec);
            if (unit_symbol && unit_sz > 0) snprintf(unit_symbol, unit_sz, "24h");
            if (words_suffix && suffix_sz > 0) {
                static const char *m_shorts[] = {
                    "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                    "Jul", "Aug", "Sept", "Oct", "Nov", "Dec"
                };
                const char *m_str = (tm_info.tm_mon >= 0 && tm_info.tm_mon < 12) ? m_shorts[tm_info.tm_mon] : "Jan";
                char weekday_buf[32];
                strftime(weekday_buf, sizeof(weekday_buf), "%A", &tm_info);
                snprintf(words_suffix, suffix_sz, "%s, %02d-%s-%04d", weekday_buf, tm_info.tm_mday, m_str, tm_info.tm_year + 1900);
            }
        }

        if (err_msg) *err_msg = NULL;
        return true;
    }

    /* Right string is the target unit */
    const UnitItem *to_unit = find_unit(right);
    if (!to_unit) {
        /* If right contains operators or brackets, this is an embedded math expression, not a top-level conversion */
        bool has_op = false;
        for (size_t k = 0; right[k]; k++) {
            if (right[k] == ')' || right[k] == '(' || right[k] == '+' ||
                right[k] == '*' || right[k] == '/' || right[k] == '^') {
                has_op = true;
                break;
            }
        }
        if (has_op) return false;

        if (err_msg) *err_msg = "unknown target unit";
        return true;
    }

    /* Left string contains <math_expr> and <source_unit> */
    size_t u_start = 0;
    const UnitItem *from_unit = NULL;
    double in_val = 1.0;
    bool in_val_computed = false;

    /* Check if left evaluates to a date/time or an expression with units */
    char eval_unit[32] = "";
    const char *m_err = NULL;
    bool is_date_eval = false;
    bool is_time_eval = false;
    double eval_res = 0.0;
    if (evaluate_equation_ex(left, ans_val, &eval_res, eval_unit, sizeof(eval_unit), &is_date_eval, &is_time_eval, &m_err)) {
        if (is_date_eval || is_time_eval || eval_res > 100000000.0) {
            /* Left is a full date/time expression (e.g. "now", "now - 5h", "today + 2h") */
            from_unit = NULL;
        } else if (eval_unit[0] != '\0') {
            from_unit = find_unit(eval_unit);
            if (from_unit) {
                in_val = eval_res;
                in_val_computed = true;
            }
        }
    }

    if (!from_unit && !is_date_eval && !is_time_eval && eval_res <= 100000000.0) {
        from_unit = extract_unit_suffix(left, &u_start);
    }

    if (!from_unit) {
        /* If target is time (e.g. delta between dates or seconds to hours), base is seconds */
        if (to_unit->dim == DIM_TIME) {
            double t_val = 0.0;
            const char *m_err = NULL;
            bool is_date = false;
            bool is_time = false;
            if (!evaluate_equation_ex(left, ans_val, &t_val, NULL, 0, &is_date, &is_time, &m_err)) {
                if (err_msg) *err_msg = m_err ? m_err : "math error";
                return true;
            }

            /* If t_val is a date/time (or raw epoch timestamp > 100,000,000s) */
            if (is_date || is_time || t_val > 100000000.0) {
                /* Target is day/days -> format calendar date */
                if (strcasecmp(to_unit->symbol, "d") == 0 || strcasecmp(right, "day") == 0 || strcasecmp(right, "days") == 0) {
                    *converted_val = t_val;
                    time_t t = (time_t)round(t_val);
                    struct tm tm_info;
                    localtime_r(&t, &tm_info);
                    static const char *m_shorts[] = {
                        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                        "Jul", "Aug", "Sept", "Oct", "Nov", "Dec"
                    };
                    const char *m_str = (tm_info.tm_mon >= 0 && tm_info.tm_mon < 12) ? m_shorts[tm_info.tm_mon] : "Jan";
                    char weekday_buf[32];
                    strftime(weekday_buf, sizeof(weekday_buf), "%A", &tm_info);
                    snprintf(result_buf, res_sz, "%s, %02d-%s-%04d", weekday_buf, tm_info.tm_mday, m_str, tm_info.tm_year + 1900);
                    if (unit_symbol && unit_sz > 0) snprintf(unit_symbol, unit_sz, "%s", weekday_buf);
                    if (words_suffix && suffix_sz > 0) snprintf(words_suffix, suffix_sz, "%s", weekday_buf);
                    if (err_msg) *err_msg = NULL;
                    return true;
                }

                /* Sub-day time units (hour, min, s, etc.): elapsed time since midnight */
                if (to_unit->to_base < 86400.0) {
                    time_t t = (time_t)round(t_val);
                    struct tm tm_info;
                    localtime_r(&t, &tm_info);
                    double seconds_today = tm_info.tm_hour * 3600.0 + tm_info.tm_min * 60.0 + tm_info.tm_sec;
                    double res_val = seconds_today / to_unit->to_base;
                    *converted_val = res_val;
                    char comp_buf[128];
                    if (format_compound_unit_downward(res_val, to_unit->symbol, comp_buf, sizeof(comp_buf))) {
                        snprintf(result_buf, res_sz, "%s", comp_buf);
                    } else {
                        char num_str[64];
                        format_result_ex(res_val, comma_fmt, num_str, sizeof(num_str));
                        snprintf(result_buf, res_sz, "%s %s", num_str, to_unit->symbol);
                    }
                    if (unit_symbol && unit_sz > 0) snprintf(unit_symbol, unit_sz, "%s", to_unit->symbol);
                    if (words_suffix && suffix_sz > 0) {
                        if (to_unit->alias_count > 1) snprintf(words_suffix, suffix_sz, "%s", to_unit->aliases[1]);
                        else if (to_unit->alias_count > 0) snprintf(words_suffix, suffix_sz, "%s", to_unit->aliases[0]);
                        else snprintf(words_suffix, suffix_sz, "%s", to_unit->symbol);
                        if (words_suffix[0]) words_suffix[0] = (char)toupper((unsigned char)words_suffix[0]);
                    }
                    if (err_msg) *err_msg = NULL;
                    return true;
                }
            }

            double res_val = t_val / to_unit->to_base;
            *converted_val = res_val;
            char comp_buf[128];
            if (format_compound_unit_downward(res_val, to_unit->symbol, comp_buf, sizeof(comp_buf))) {
                snprintf(result_buf, res_sz, "%s", comp_buf);
            } else {
                char num_str[64];
                format_result_ex(res_val, comma_fmt, num_str, sizeof(num_str));
                snprintf(result_buf, res_sz, "%s %s", num_str, to_unit->symbol);
            }
            if (unit_symbol && unit_sz > 0) snprintf(unit_symbol, unit_sz, "%s", to_unit->symbol);
            if (words_suffix && suffix_sz > 0) {
                if (to_unit->alias_count > 1) snprintf(words_suffix, suffix_sz, "%s", to_unit->aliases[1]);
                else if (to_unit->alias_count > 0) snprintf(words_suffix, suffix_sz, "%s", to_unit->aliases[0]);
                else snprintf(words_suffix, suffix_sz, "%s", to_unit->symbol);
                if (words_suffix[0]) words_suffix[0] = (char)toupper((unsigned char)words_suffix[0]);
            }
            if (err_msg) *err_msg = NULL;
            return true;
        }

        if (err_msg) *err_msg = "missing or unknown source unit";
        return true;
    }

    if (!in_val_computed) {
        char math_expr[256];
        size_t m_len = u_start;
        if (m_len >= sizeof(math_expr)) m_len = sizeof(math_expr) - 1;
        memcpy(math_expr, left, m_len);
        math_expr[m_len] = '\0';
        trim_str(math_expr);

        in_val = 1.0;
        if (math_expr[0] != '\0') {
            const char *m_err = NULL;
            if (!evaluate_equation(math_expr, ans_val, &in_val, &m_err)) {
                if (err_msg) *err_msg = m_err ? m_err : "math error";
                return true;
            }
        }
    }

    /* Perform unit conversion */
    double res_val = 0.0;
    if (from_unit->dim == DIM_TEMP) {
        res_val = convert_temp(in_val, from_unit->symbol, to_unit->symbol);
    } else if (from_unit->dim == DIM_CURRENCY) {
        double usd_val = in_val / from_unit->to_base;
        res_val = usd_val * to_unit->to_base;
    } else {
        double base_val = in_val * from_unit->to_base;
        res_val = base_val / to_unit->to_base;
    }

    *converted_val = res_val;

    /* Format result with unit symbol */
    char comp_buf[128];
    if (format_compound_unit_downward(res_val, to_unit->symbol, comp_buf, sizeof(comp_buf))) {
        snprintf(result_buf, res_sz, "%s", comp_buf);
    } else {
        char num_str[64];
        CommaFormat eff_fmt = comma_fmt;
        if (to_unit->dim == DIM_CURRENCY && strcasecmp(to_unit->symbol, "inr") != 0) {
            if (eff_fmt == FMT_INDIAN) eff_fmt = FMT_INTL;
        }
        format_result_ex(res_val, eff_fmt, num_str, sizeof(num_str));
        snprintf(result_buf, res_sz, "%s %s", num_str, to_unit->symbol);
    }

    if (unit_symbol && unit_sz > 0) {
        snprintf(unit_symbol, unit_sz, "%s", to_unit->symbol);
    }

    /* Prepare word representation suffix */
    if (words_suffix && suffix_sz > 0) {
        if (to_unit->alias_count > 1) {
            snprintf(words_suffix, suffix_sz, "%s", to_unit->aliases[1]); /* e.g. "miles" */
        } else if (to_unit->alias_count > 0) {
            snprintf(words_suffix, suffix_sz, "%s", to_unit->aliases[0]);
        } else {
            snprintf(words_suffix, suffix_sz, "%s", to_unit->symbol);
        }
        /* Capitalize first letter */
        if (words_suffix[0]) words_suffix[0] = (char)toupper((unsigned char)words_suffix[0]);
    }

    if (err_msg) *err_msg = NULL;
    return true;
}

bool get_conversion_hints(const char *input, char *hint_out, size_t hint_sz) {
    if (!input || !*input || hint_sz == 0) return false;
    hint_out[0] = '\0';

    char clean[256];
    snprintf(clean, sizeof(clean), "%s", input);
    trim_str(clean);
    size_t in_len = strlen(clean);
    while (has_outer_matching_brackets(clean, in_len)) {
        clean[in_len - 1] = '\0';
        memmove(clean, clean + 1, in_len - 1);
        trim_str(clean);
        in_len = strlen(clean);
    }

    /* Find separator " to " or " in " (case-insensitive) at depth 0 */
    const char *sep = NULL;
    size_t sep_len = 0;
    int depth = 0;
    for (size_t i = 0; i + 4 <= in_len; i++) {
        if (clean[i] == '(') depth++;
        else if (clean[i] == ')') {
            if (depth > 0) depth--;
        }
        if (depth == 0 && clean[i] == ' ') {
            if ((clean[i+1] == 't' || clean[i+1] == 'T') &&
                (clean[i+2] == 'o' || clean[i+2] == 'O') &&
                clean[i+3] == ' ') {
                sep = clean + i;
                sep_len = 4;
            } else if ((clean[i+1] == 'i' || clean[i+1] == 'I') &&
                       (clean[i+2] == 'n' || clean[i+2] == 'N') &&
                       clean[i+3] == ' ') {
                sep = clean + i;
                sep_len = 4;
            }
        }
    }

    /* Also check for trailing " to" or " in" at end of string */
    if (!sep) {
        if (in_len >= 3 && clean[in_len - 3] == ' ' &&
            tolower((unsigned char)clean[in_len - 2]) == 't' &&
            tolower((unsigned char)clean[in_len - 1]) == 'o') {
            sep = clean + in_len - 3;
            sep_len = 3;
        } else if (in_len >= 3 && clean[in_len - 3] == ' ' &&
                   tolower((unsigned char)clean[in_len - 2]) == 'i' &&
                   tolower((unsigned char)clean[in_len - 1]) == 'n') {
            sep = clean + in_len - 3;
            sep_len = 3;
        }
    }

    if (!sep) return false;

    /* Extract Left */
    char left[256];
    size_t left_len = (size_t)(sep - clean);
    if (left_len >= sizeof(left)) left_len = sizeof(left) - 1;
    memcpy(left, clean, left_len);
    left[left_len] = '\0';
    trim_str(left);

    /* Extract Right (target prefix being typed) */
    char right[64] = "";
    if (sep + sep_len < clean + in_len) {
        snprintf(right, sizeof(right), "%s", sep + sep_len);
        trim_str(right);
    }

    /* Extract source unit from left */
    size_t u_start = 0;
    const UnitItem *from_unit = extract_unit_suffix(left, &u_start);
    if (from_unit) {
        Dimension dim = from_unit->dim;
        char matches[256] = "";
        int match_count = 0;

        for (int i = 0; i < g_unit_count; i++) {
            if (g_units[i].dim != dim) continue;
            /* Do not suggest the source unit itself */
            if (strcasecmp(g_units[i].symbol, from_unit->symbol) == 0) continue;

            /* Filter by right prefix if typed */
            if (right[0] != '\0') {
                bool pref_match = (strncasecmp(g_units[i].symbol, right, strlen(right)) == 0);
                if (!pref_match) {
                    for (int a = 0; a < g_units[i].alias_count; a++) {
                        if (strncasecmp(g_units[i].aliases[a], right, strlen(right)) == 0) {
                            pref_match = true;
                            break;
                        }
                    }
                }
                if (!pref_match) continue;
            }

            if (match_count > 0 && strlen(matches) + 2 < sizeof(matches)) {
                strcat(matches, ", ");
            }
            if (strlen(matches) + strlen(g_units[i].symbol) < sizeof(matches)) {
                strcat(matches, g_units[i].symbol);
                match_count++;
            }
        }

        if (match_count > 0) {
            snprintf(hint_out, hint_sz, "Targets (%s): %s", dim_name(dim), matches);
            return true;
        } else if (right[0] != '\0') {
            snprintf(hint_out, hint_sz, "No matching %s unit for '%s'", dim_name(dim), right);
            return true;
        }
    } else {
        /* Check if left is a date/time expression or has source timezone */
        bool is_dt_context = false;
        double dummy_v = 0.0;
        bool is_d = false, is_t = false;
        if (evaluate_equation_ex(left, 0.0, &dummy_v, NULL, 0, &is_d, &is_t, NULL)) {
            if (is_d || is_t || dummy_v > 100000000.0) is_dt_context = true;
        }
        if (!is_dt_context) {
            char b_tmp[128], s_tmp[64];
            if (extract_source_timezone(left, b_tmp, sizeof(b_tmp), s_tmp, sizeof(s_tmp))) {
                is_dt_context = true;
            }
        }

        if (right[0] != '\0') {
            char tz_hint[256] = "";
            if (get_matching_timezone_hints(right, tz_hint, sizeof(tz_hint))) {
                snprintf(hint_out, hint_sz, "%s", tz_hint);
                return true;
            }
        } else if (is_dt_context) {
            char tz_hint[256] = "";
            if (get_matching_timezone_hints("", tz_hint, sizeof(tz_hint))) {
                snprintf(hint_out, hint_sz, "%s", tz_hint);
                return true;
            }
        }

        /* Source unit not yet recognized: check if right prefix matches programmer radix or date/time */
        static const char *specials[] = { "hex", "bin", "oct", "dec", "date", "time", "weekday", "utc", "epoch", "24h" };
        char matches[128] = "";
        int match_cnt = 0;
        for (size_t s = 0; s < sizeof(specials)/sizeof(specials[0]); s++) {
            if (right[0] == '\0' || strncasecmp(specials[s], right, strlen(right)) == 0) {
                if (match_cnt > 0) strcat(matches, ", ");
                strcat(matches, specials[s]);
                match_cnt++;
            }
        }
        if (match_cnt > 0) {
            snprintf(hint_out, hint_sz, "Targets (radix/date): %s", matches);
            return true;
        }
        snprintf(hint_out, hint_sz, "Units: length, mass, time, data, temp, currency, speed, area, data rate, pressure");
        return true;
    }

    return false;
}

void print_units_help(void) {
    printf("Elephant Calc - Available Units & Multipliers\n");
    printf("Usage: <expr> <from_unit> to <to_unit>   (e.g., '12 crore inr to usd')\n\n");

    const Dimension dims[] = {
        DIM_LENGTH,
        DIM_MASS,
        DIM_TIME,
        DIM_DATA,
        DIM_TEMP,
        DIM_CURRENCY,
        DIM_SPEED,
        DIM_AREA,
        DIM_DATA_RATE,
        DIM_PRESSURE
    };

    const char *dim_titles[] = {
        "Length (Base: meter)",
        "Mass / Weight (Base: gram)",
        "Time (Base: second)",
        "Data Storage (Base: byte)",
        "Temperature",
        "Currency (Base: USD - Snapshot Rates)",
        "Speed (Base: meter/second)",
        "Area (Base: square meter)",
        "Data Transfer Rate (Base: bit/second)",
        "Pressure (Base: Pascal)"
    };

    for (size_t d = 0; d < sizeof(dims) / sizeof(dims[0]); d++) {
        Dimension target_dim = dims[d];
        printf("[%s]\n", dim_titles[d]);

        for (int i = 0; i < g_unit_count; i++) {
            if (g_units[i].dim != target_dim) continue;

            printf("  %-8s", g_units[i].symbol);

            if (g_units[i].alias_count > 0) {
                printf(" : ");
                for (int a = 0; a < g_units[i].alias_count; a++) {
                    printf("%s%s", g_units[i].aliases[a], (a + 1 < g_units[i].alias_count) ? ", " : "");
                }
            }
            printf("\n");
        }
        printf("\n");
    }

    printf("[Magnitude Scale Multipliers]\n");
    printf("  Western  : k (1,000), thousand[s], million[s] (1e6), billion[s] (1e9), trillion[s] (1e12)\n");
    printf("  Indian   : lakh[s], lac[s] (100,000), crore[s], cr (10,000,000)\n\n");
    printf("[Programmer Mode & Radix Conversions]\n");
    printf("  Literals : 0x (Hex, e.g. 0xFF), 0b (Bin, e.g. 0b1010), 0o (Oct, e.g. 0o77)\n");
    printf("  Bitwise  : & (AND), | (OR), ~ (NOT), << (LShift), >> (RShift), xor(a, b) / a xor b\n");
    printf("  Radix    : <expr> to hex, <expr> to bin, <expr> to oct, <expr> to dec\n\n");
    printf("[Date, Time & Epoch Calculations]\n");
    printf("  Keywords : now, today, tomorrow, yesterday, next month/year, end of month\n");
    printf("  Offsets  : today + 45 days, 3 weeks, 12 hours, 30 minutes, 1 year\n");
    printf("  Business : today + 10 bdays, today - 5 workdays, business days between d1 and d2\n");
    printf("  Anchors  : days until <date>, weeks since <date>, 3 days before <date>, 2 weeks after <date>\n");
    printf("  Deltas   : date1 - date2, (date1 - date2) to days\n");
    printf("  Formats  : <expr> to date, <expr> to weekday, <expr> to utc, <expr> to epoch\n\n");
    printf("[Financial & Statistical Functions]\n");
    printf("  avg(x1, x2, ...)       : Mean / average of values\n");
    printf("  median(x1, x2, ...)    : Median value of arguments\n");
    printf("  min(...), max(...)     : Minimum and maximum value\n");
    printf("  emi(P, r, n)           : Monthly EMI for loan P, annual rate r%%, n months\n");
    printf("  cagr(v_i, v_f, y)      : Compound Annual Growth Rate over y years\n");
    printf("  sip(P, r, y)           : SIP future value for monthly deposit P, r%% p.a., y years\n");
    printf("  compound(P, r, y[, n]) : Compound interest future value (annual or n-times/yr)\n");
    printf("  discount(price, pct)   : Price after pct%% discount\n");
    printf("  markup(cost, margin)   : Price after margin%% markup\n\n");
    printf("CLI Options:\n");
    printf("  --eval \"<expr>\"   Evaluate expression and print result to stdout\n");
    printf("  --words           Print spoken words and computing rows\n");
    printf("  --comp-only       Display only computing terms (Hex, Bin - default)\n");
    printf("  --all             Display full radix output including Dec\n");
    printf("  --simple          Disable computing row\n");
    printf("  --decimals <N>    Set decimal places (0-8 or auto)\n");
    printf("  --intl            Use International comma formatting (1,000,000)\n");
    printf("  --indian          Use Indian comma formatting (10,00,000)\n");
    printf("  --plain           Disable comma formatting\n");
    printf("  --scale <factor>  Set GUI font scale factor (0.8 - 1.8)\n");
    printf("  --config <path>   Use custom settings directory\n");
    printf("  --units, --help   Show this help reference\n");
}

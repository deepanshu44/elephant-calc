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
#include <stdint.h>
#include <time.h>

CommaFormat comma_fmt = FMT_INTL;
int decimal_places = -1;
OutputMode output_mode = OUTPUT_COMP_ONLY;

/* ==========================================================================
 * Result Formatter (Comma Separation & Precision)
 * ========================================================================== */

void format_result_ex(double val, CommaFormat fmt, char *out, size_t out_sz) {
    if (isnan(val)) {
        snprintf(out, out_sz, "NaN");
        return;
    }
    if (isinf(val)) {
        snprintf(out, out_sz, val > 0 ? "Infinity" : "-Infinity");
        return;
    }
    if (fabs(val) >= 1e14 || (fabs(val) < 1e-6 && fabs(val) > 0.0)) {
        snprintf(out, out_sz, "%.8g", val);
        return;
    }

    char raw[64];
    if (decimal_places >= 0) {
        int dec = decimal_places > 8 ? 8 : decimal_places;
        snprintf(raw, sizeof(raw), "%.*f", dec, val);
    } else {
        bool is_int = (fabs(val - round(val)) < 1e-9);
        if (is_int) {
            snprintf(raw, sizeof(raw), "%.0f", val);
        } else {
            snprintf(raw, sizeof(raw), "%.8f", val);
            size_t l = strlen(raw);
            while (l > 0 && raw[l - 1] == '0') raw[--l] = '\0';
            if (l > 0 && raw[l - 1] == '.') raw[--l] = '\0';
        }
    }

    char *dot = strchr(raw, '.');
    char int_part[64];
    const char *frac_part = dot ? dot : "";
    if (dot) {
        size_t int_len = (size_t)(dot - raw);
        memcpy(int_part, raw, int_len);
        int_part[int_len] = '\0';
    } else {
        strncpy(int_part, raw, sizeof(int_part) - 1);
        int_part[sizeof(int_part) - 1] = '\0';
    }

    char formatted_int[64] = "";
    size_t int_len = strlen(int_part);
    size_t start = (int_part[0] == '-') ? 1 : 0;
    size_t num_digits = int_len - start;

    size_t out_idx = 0;
    if (start == 1) formatted_int[out_idx++] = '-';

    for (size_t i = 0; i < num_digits; i++) {
        formatted_int[out_idx++] = int_part[start + i];
        size_t remaining = num_digits - 1 - i;
        bool need_comma = (fmt == FMT_PLAIN) ? false :
            (fmt == FMT_INDIAN 
                ? (remaining == 3 || (remaining > 3 && (remaining - 3) % 2 == 0))
                : (remaining % 3 == 0));
        if (remaining > 0 && need_comma) {
            formatted_int[out_idx++] = ',';
        }
    }
    formatted_int[out_idx] = '\0';

    snprintf(out, out_sz, "%s%s", formatted_int, frac_part);
}

void format_result(double val, char *out, size_t out_sz) {
    format_result_ex(val, comma_fmt, out, out_sz);
}

void format_date_result(double val, char *out, size_t out_sz) {
    if (isnan(val) || isinf(val)) {
        format_result(val, out, out_sz);
        return;
    }
    time_t t = (time_t)round(val);
    struct tm tm_info;
    localtime_r(&t, &tm_info);
    static const char *m_shorts[] = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sept", "Oct", "Nov", "Dec"
    };
    const char *m_str = (tm_info.tm_mon >= 0 && tm_info.tm_mon < 12) ? m_shorts[tm_info.tm_mon] : "Jan";
    if (tm_info.tm_hour == 0 && tm_info.tm_min == 0 && tm_info.tm_sec == 0) {
        snprintf(out, out_sz, "%02d-%s-%04d", tm_info.tm_mday, m_str, tm_info.tm_year + 1900);
    } else {
        int hour12 = tm_info.tm_hour % 12;
        if (hour12 == 0) hour12 = 12;
        const char *am_pm = tm_info.tm_hour >= 12 ? "PM" : "AM";
        snprintf(out, out_sz, "%02d-%s-%04d %02d:%02d:%02d %s",
                 tm_info.tm_mday, m_str, tm_info.tm_year + 1900,
                 hour12, tm_info.tm_min, tm_info.tm_sec, am_pm);
    }
}

void format_time_result(double val, bool is_24h, char *out, size_t out_sz) {
    if (isnan(val) || isinf(val)) {
        format_result(val, out, out_sz);
        return;
    }
    time_t t = (time_t)round(val);
    struct tm tm_info;
    localtime_r(&t, &tm_info);
    if (is_24h) {
        snprintf(out, out_sz, "%02d:%02d:%02d", tm_info.tm_hour, tm_info.tm_min, tm_info.tm_sec);
    } else {
        int hour12 = tm_info.tm_hour % 12;
        if (hour12 == 0) hour12 = 12;
        snprintf(out, out_sz, "%02d:%02d:%02d %s",
                 hour12, tm_info.tm_min, tm_info.tm_sec,
                 tm_info.tm_hour >= 12 ? "PM" : "AM");
    }
}

void format_number_token(const char *raw, char *out, size_t out_sz) {
    if (!raw || strlen(raw) == 0) {
        if (out_sz > 0) out[0] = '\0';
        return;
    }

    const char *dot = strchr(raw, '.');
    char clean_int[64] = "";
    size_t c_idx = 0;

    size_t int_raw_len = dot ? (size_t)(dot - raw) : strlen(raw);
    for (size_t i = 0; i < int_raw_len; i++) {
        if (isdigit((unsigned char)raw[i])) {
            if (c_idx < sizeof(clean_int) - 1) clean_int[c_idx++] = raw[i];
        }
    }
    clean_int[c_idx] = '\0';

    if (c_idx == 0) {
        snprintf(out, out_sz, "%s", raw);
        return;
    }

    char formatted_int[64] = "";
    size_t out_i = 0;
    for (size_t i = 0; i < c_idx; i++) {
        if (out_i < sizeof(formatted_int) - 1) formatted_int[out_i++] = clean_int[i];
        size_t rem = c_idx - 1 - i;
        bool need_comma = (comma_fmt == FMT_PLAIN) ? false :
            (comma_fmt == FMT_INDIAN 
                ? (rem == 3 || (rem > 3 && (rem - 3) % 2 == 0))
                : (rem % 3 == 0));
        if (rem > 0 && need_comma) {
            if (out_i < sizeof(formatted_int) - 1) formatted_int[out_i++] = ',';
        }
    }
    formatted_int[out_i] = '\0';

    const char *frac = dot ? dot : "";
    snprintf(out, out_sz, "%s%s", formatted_int, frac);
}

static bool is_month_word(const char *s, size_t len) {
    static const char *months[] = {
        "jan", "january", "feb", "february", "mar", "march", "apr", "april",
        "may", "jun", "june", "jul", "july", "aug", "august",
        "sep", "sept", "september", "oct", "october", "nov", "november", "dec", "december"
    };
    for (size_t i = 0; i < sizeof(months)/sizeof(months[0]); i++) {
        if (strncasecmp(s, months[i], len) == 0 && months[i][len] == '\0') return true;
    }
    return false;
}

static bool is_date_or_radix_context(const char *buf, size_t num_start, size_t num_len, size_t buf_len) {
    /* 1. Radix prefix: 0x, 0b, 0o */
    if (num_start >= 2 &&
        (buf[num_start - 1] == 'x' || buf[num_start - 1] == 'X' ||
         buf[num_start - 1] == 'b' || buf[num_start - 1] == 'B' ||
         buf[num_start - 1] == 'o' || buf[num_start - 1] == 'O') &&
        buf[num_start - 2] == '0') {
        return true;
    }

    /* 2. Check preceding characters */
    if (num_start > 0) {
        char prev = buf[num_start - 1];
        if (prev == '-' || prev == '/') {
            if (num_start >= 2) {
                char before_sep = buf[num_start - 2];
                if (isalnum((unsigned char)before_sep)) {
                    return true;
                }
            }
        } else if (prev == ' ' || prev == ',') {
            size_t p = num_start - 1;
            while (p > 0 && (buf[p - 1] == ' ' || buf[p - 1] == ',')) p--;
            size_t w_end = p;
            while (p > 0 && isalpha((unsigned char)buf[p - 1])) p--;
            if (w_end > p && is_month_word(&buf[p], w_end - p)) {
                return true;
            }
            /* Also check Month + Day e.g. "Oct 10 2026" or "Oct 10, 2026" */
            size_t d_end = w_end;
            size_t dp = d_end;
            while (dp > 0 && isdigit((unsigned char)buf[dp - 1])) dp--;
            if (d_end > dp && (d_end - dp) <= 2) {
                while (dp > 0 && (buf[dp - 1] == ' ' || buf[dp - 1] == ',')) dp--;
                size_t mw_end = dp;
                while (dp > 0 && isalpha((unsigned char)buf[dp - 1])) dp--;
                if (mw_end > dp && is_month_word(&buf[dp], mw_end - dp)) {
                    return true;
                }
            }
        }
    }

    /* 3. Check succeeding characters */
    size_t num_end = num_start + num_len;
    if (num_end < buf_len) {
        char next = buf[num_end];
        if (next == '-' || next == '/') {
            if (num_end + 1 < buf_len) {
                char after_sep = buf[num_end + 1];
                if (isalnum((unsigned char)after_sep)) {
                    return true;
                }
            }
            if (num_len == 4) {
                return true;
            }
        } else if (next == ' ') {
            size_t p = num_end;
            while (p < buf_len && buf[p] == ' ') p++;
            size_t w_start = p;
            while (p < buf_len && isalpha((unsigned char)buf[p])) p++;
            if (p > w_start && is_month_word(&buf[w_start], p - w_start)) {
                return true;
            }
        }
    }

    return false;
}

void reformat_buffer_numbers(char *buf, size_t buf_sz, size_t *cursor_pos, size_t *input_len) {
    size_t old_len = *input_len;
    size_t non_comma_before_cursor = 0;
    for (size_t k = 0; k < *cursor_pos && k < old_len; k++) {
        if (buf[k] != ',') non_comma_before_cursor++;
    }

    char new_buf[256] = "";
    size_t new_idx = 0;
    size_t i = 0;

    while (i < old_len) {
        if (isdigit((unsigned char)buf[i]) || (buf[i] == '.' && isdigit((unsigned char)buf[i+1]))) {
            size_t num_start = i;
            while (i < old_len && (isdigit((unsigned char)buf[i]) || buf[i] == ',' || buf[i] == '.')) i++;
            size_t num_len = i - num_start;
            char token[64];
            if (num_len < sizeof(token)) {
                memcpy(token, &buf[num_start], num_len);
                token[num_len] = '\0';

                char formatted[64];
                if (is_date_or_radix_context(buf, num_start, num_len, old_len)) {
                    /* Strip any existing commas from date/radix number */
                    size_t c_out = 0;
                    for (size_t k = 0; k < num_len; k++) {
                        if (token[k] != ',') formatted[c_out++] = token[k];
                    }
                    formatted[c_out] = '\0';
                } else {
                    format_number_token(token, formatted, sizeof(formatted));
                }

                size_t flen = strlen(formatted);
                if (new_idx + flen < buf_sz - 1) {
                    memcpy(&new_buf[new_idx], formatted, flen);
                    new_idx += flen;
                }
            }
        } else {
            if (new_idx < buf_sz - 1) new_buf[new_idx++] = buf[i];
            i++;
        }
    }
    new_buf[new_idx] = '\0';
    strncpy(buf, new_buf, buf_sz - 1);
    buf[buf_sz - 1] = '\0';
    *input_len = strlen(buf);

    size_t count = 0;
    size_t new_cursor = 0;
    while (new_cursor < *input_len && count < non_comma_before_cursor) {
        if (buf[new_cursor] != ',') count++;
        new_cursor++;
    }
    *cursor_pos = new_cursor;
    if (*cursor_pos > *input_len) *cursor_pos = *input_len;
}

void maybe_format_number_before_cursor(char *buf, size_t buf_sz, size_t *cursor_pos, size_t *input_len, char next_char) {
    if (comma_fmt == FMT_PLAIN) return;
    if (*cursor_pos == 0) return;

    size_t end = *cursor_pos;
    size_t start = end;

    while (start > 0) {
        char ch = buf[start - 1];
        if (isdigit((unsigned char)ch) || ch == ',' || ch == '.') {
            start--;
        } else {
            break;
        }
    }

    if (start >= end) return;

    size_t raw_len = end - start;

    /* 4-digit number followed immediately by date separator is a year */
    if ((next_char == '-' || next_char == '/') && raw_len == 4) {
        return;
    }
    if ((next_char == '-' || next_char == '/') && is_date_or_radix_context(buf, start, raw_len, *input_len)) {
        return;
    }
    if (is_date_or_radix_context(buf, start, raw_len, *input_len)) {
        return;
    }

    char raw_token[64];
    if (raw_len >= sizeof(raw_token)) return;
    memcpy(raw_token, &buf[start], raw_len);
    raw_token[raw_len] = '\0';

    char formatted[64];
    format_number_token(raw_token, formatted, sizeof(formatted));

    size_t fmt_len = strlen(formatted);
    if (fmt_len == raw_len && strcmp(raw_token, formatted) == 0) {
        return;
    }

    if (*input_len - raw_len + fmt_len >= buf_sz - 1) return;

    memmove(&buf[start + fmt_len], &buf[end], *input_len - end + 1);
    memcpy(&buf[start], formatted, fmt_len);

    *input_len = *input_len - raw_len + fmt_len;
    *cursor_pos = start + fmt_len;
}

/* ==========================================================================
 * Data-Driven Number-to-Words Engine
 * ========================================================================== */

typedef struct {
    long long value;
    const char *name;
} ScaleUnit;

/* International / Western short-scale powers of 1000 */
static const ScaleUnit SCALE_INTL[] = {
    { 1000000000000LL, "Trillion" },
    { 1000000000LL,    "Billion" },
    { 1000000LL,       "Million" },
    { 1000LL,          "Thousand" },
    { 0, NULL }
};

/* Indian Vedic scale powers */
static const ScaleUnit SCALE_INDIAN[] = {
    { 1000000000LL,    "Arab" },
    { 10000000LL,      "Crore" },
    { 100000LL,        "Lakh" },
    { 1000LL,          "Thousand" },
    { 0, NULL }
};

static const char *WORD_ONES[] = {
    "", "One", "Two", "Three", "Four", "Five", "Six", "Seven", "Eight", "Nine",
    "Ten", "Eleven", "Twelve", "Thirteen", "Fourteen", "Fifteen", "Sixteen",
    "Seventeen", "Eighteen", "Nineteen"
};

static const char *WORD_TENS[] = {
    "", "", "Twenty", "Thirty", "Forty", "Fifty", "Sixty", "Seventy", "Eighty", "Ninety"
};

static void words_append(char *out, size_t sz, const char *word) {
    if (!word || !*word) return;
    if (out[0] != '\0' && out[strlen(out) - 1] != ' ') {
        strncat(out, " ", sz - strlen(out) - 1);
    }
    strncat(out, word, sz - strlen(out) - 1);
}

static void words_sub_1000(long long n, char *out, size_t sz) {
    if (n >= 100) {
        words_append(out, sz, WORD_ONES[n / 100]);
        words_append(out, sz, "Hundred");
        n %= 100;
    }
    if (n >= 20) {
        words_append(out, sz, WORD_TENS[n / 10]);
        n %= 10;
    }
    if (n > 0) {
        words_append(out, sz, WORD_ONES[n]);
    }
}

static void words_format_integer(long long n, const ScaleUnit *scales, char *out, size_t sz) {
    if (n == 0) return;
    for (const ScaleUnit *s = scales; s->value > 0; s++) {
        if (n >= s->value) {
            long long chunk = n / s->value;
            if (chunk >= 1000 && scales == SCALE_INDIAN) {
                words_format_integer(chunk, scales, out, sz);
            } else {
                words_sub_1000(chunk, out, sz);
            }
            words_append(out, sz, s->name);
            n %= s->value;
        }
    }
    if (n > 0) {
        words_sub_1000(n, out, sz);
    }
}

void number_to_words(double val, bool indian, char *out, size_t sz) {
    out[0] = '\0';
    if (isnan(val) || isinf(val)) {
        snprintf(out, sz, "Invalid");
        return;
    }
    if (val < 0.0) {
        words_append(out, sz, "Negative");
        val = -val;
    }
    if (val > 1e14) {
        snprintf(out, sz, "Value Too Large");
        return;
    }

    long long int_part = (long long)floor(val + 1e-9);
    double frac_part = val - (double)int_part;

    const ScaleUnit *scales = indian ? SCALE_INDIAN : SCALE_INTL;

    if (int_part == 0) {
        words_append(out, sz, "Zero");
    } else {
        words_format_integer(int_part, scales, out, sz);
    }

    if (frac_part >= 0.0001) {
        words_append(out, sz, "point");
        char frac_str[16];
        snprintf(frac_str, sizeof(frac_str), "%.4f", frac_part);
        char *p = strchr(frac_str, '.');
        if (p) {
            p++;
            int flen = strlen(p);
            while (flen > 0 && p[flen - 1] == '0') {
                p[--flen] = '\0';
            }
            for (int i = 0; i < flen; i++) {
                int digit = p[i] - '0';
                if (digit >= 0 && digit <= 9) {
                    if (digit == 0) words_append(out, sz, "Zero");
                    else words_append(out, sz, WORD_ONES[digit]);
                }
            }
        }
    }
}

void format_compact_number(double val, bool indian, char *out, size_t out_sz) {
    if (out_sz == 0) return;
    out[0] = '\0';
    if (isnan(val) || isinf(val)) return;

    double abs_val = fabs(val);
    if (abs_val < 1000.0) {
        return; /* Not large enough for compact representation */
    }

    const char *sign = (val < 0.0) ? "-" : "";
    double scaled = abs_val;
    const char *suffix = "";

    if (indian) {
        if (abs_val >= 9999500.0) { /* >= 1 Cr rounded */
            scaled = abs_val / 1e7;
            suffix = "Cr";
        } else if (abs_val >= 99950.0) { /* >= 1 Lakh rounded */
            scaled = abs_val / 1e5;
            suffix = "Lakh";
        } else {
            scaled = abs_val / 1e3;
            suffix = "k";
        }
    } else {
        if (abs_val >= 999950000000.0) {
            scaled = abs_val / 1e12;
            suffix = "T";
        } else if (abs_val >= 999950000.0) {
            scaled = abs_val / 1e9;
            suffix = "B";
        } else if (abs_val >= 999950.0) {
            scaled = abs_val / 1e6;
            suffix = "M";
        } else {
            scaled = abs_val / 1e3;
            suffix = "k";
        }
    }

    char num_str[32];
    snprintf(num_str, sizeof(num_str), "%.2f", scaled);
    /* Trim trailing zeros and unnecessary dot */
    char *dot = strchr(num_str, '.');
    if (dot) {
        size_t l = strlen(num_str);
        while (l > 0 && num_str[l - 1] == '0') num_str[--l] = '\0';
        if (l > 0 && num_str[l - 1] == '.') num_str[--l] = '\0';
    }

    if (suffix[0] == 'k' || suffix[0] == 'M' || suffix[0] == 'B' || suffix[0] == 'T') {
        snprintf(out, out_sz, "%s%s%s", sign, num_str, suffix);
    } else {
        snprintf(out, out_sz, "%s%s %s", sign, num_str, suffix);
    }
}

void format_bin_string(long long val, char *buf, size_t buf_sz) {
    if (buf_sz == 0) return;
    if (val == 0) {
        snprintf(buf, buf_sz, "0b0");
        return;
    }
    bool neg = val < 0;
    unsigned long long u = neg ? (unsigned long long)(-val) : (unsigned long long)val;
    char bits[65];
    int idx = 0;
    while (u > 0 && idx < 64) {
        bits[idx++] = (u & 1) ? '1' : '0';
        u >>= 1;
    }
    char rev[65];
    for (int i = 0; i < idx; i++) {
        rev[i] = bits[idx - 1 - i];
    }
    rev[idx] = '\0';
    if (neg) {
        snprintf(buf, buf_sz, "-0b%s", rev);
    } else {
        snprintf(buf, buf_sz, "0b%s", rev);
    }
}

void format_computing_row(double val, OutputMode mode, const char *unit_sym, const char *words_sfx, bool is_date, bool is_time, char *out, size_t out_sz) {
    if (!out || out_sz == 0) return;
    out[0] = '\0';
    if (isnan(val) || isinf(val)) {
        return;
    }

    if (mode == OUTPUT_SIMPLE) {
        return;
    }

    char dec_str[64];
    format_result_ex(val, comma_fmt, dec_str, sizeof(dec_str));

    /* Computing Mode: Output Hex, Bin (and Dec if mode == OUTPUT_ALL) */
    bool is_pure_int = !is_date && !is_time &&
                       (unit_sym == NULL || unit_sym[0] == '\0') &&
                       (fabs(val) < 9e15) &&
                       (fabs(val - round(val)) < 1e-9);

    bool is_radix_res = words_sfx && (strcasecmp(words_sfx, "Hexadecimal") == 0 ||
                                     strcasecmp(words_sfx, "Binary") == 0 ||
                                     strcasecmp(words_sfx, "Octal") == 0 ||
                                     strcasecmp(words_sfx, "Decimal") == 0);

    if (is_pure_int || is_radix_res) {
        long long ival = (long long)round(val);

        /* If ival falls in timestamp epoch range (2000 to 2100) and no explicit radix suffix */
        if (!is_radix_res && ival >= 946684800LL && ival <= 4102444800LL) {
            time_t t = (time_t)ival;
            struct tm tm_l;
            localtime_r(&t, &tm_l);
            static const char *m_shorts[] = {
                "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                "Jul", "Aug", "Sept", "Oct", "Nov", "Dec"
            };
            const char *mn = (tm_l.tm_mon >= 0 && tm_l.tm_mon < 12) ? m_shorts[tm_l.tm_mon] : "Jan";
            char dt_buf[64];
            snprintf(dt_buf, sizeof(dt_buf), "%02d-%s-%04d", tm_l.tm_mday, mn, tm_l.tm_year + 1900);
            char day_buf[32];
            strftime(day_buf, sizeof(day_buf), "%A", &tm_l);
            char hex_str[64];
            if (ival < 0) snprintf(hex_str, sizeof(hex_str), "-0x%llX", (unsigned long long)(-ival));
            else snprintf(hex_str, sizeof(hex_str), "0x%llX", (unsigned long long)ival);

            if (mode == OUTPUT_ALL) {
                snprintf(out, out_sz, "Dec: %s  •  Hex: %s  •  Date: %s (%s)", dec_str, hex_str, dt_buf, day_buf);
            } else {
                snprintf(out, out_sz, "Hex: %s  •  Date: %s (%s)", hex_str, dt_buf, day_buf);
            }
            return;
        }

        char hex_str[64];
        if (ival < 0) snprintf(hex_str, sizeof(hex_str), "-0x%llX", (unsigned long long)(-ival));
        else snprintf(hex_str, sizeof(hex_str), "0x%llX", (unsigned long long)ival);
        char bin_str[70];
        format_bin_string(ival, bin_str, sizeof(bin_str));

        if (mode == OUTPUT_ALL) {
            if (words_sfx && strcasecmp(words_sfx, "Hexadecimal") == 0) {
                snprintf(out, out_sz, "Dec: %s  •  Bin: %s", dec_str, bin_str);
            } else if (words_sfx && strcasecmp(words_sfx, "Binary") == 0) {
                snprintf(out, out_sz, "Dec: %s  •  Hex: %s", dec_str, hex_str);
            } else {
                snprintf(out, out_sz, "Dec: %s  •  Hex: %s  •  Bin: %s", dec_str, hex_str, bin_str);
            }
        } else {
            /* OUTPUT_COMP_ONLY: Only computing terms, no redundant Dec: */
            if (words_sfx && strcasecmp(words_sfx, "Hexadecimal") == 0) {
                snprintf(out, out_sz, "Bin: %s", bin_str);
            } else if (words_sfx && strcasecmp(words_sfx, "Binary") == 0) {
                snprintf(out, out_sz, "Hex: %s", hex_str);
            } else {
                snprintf(out, out_sz, "Hex: %s  •  Bin: %s", hex_str, bin_str);
            }
        }
        return;
    }

    /* Timezone conversion result */
    if (unit_sym && is_valid_timezone_query(unit_sym) && val > 100000000.0) {
        TimezoneResult tz_res;
        if (convert_to_timezone((time_t)round(val), unit_sym, &tz_res)) {
            if (mode == OUTPUT_ALL) {
                snprintf(out, out_sz, "Dec: %s  •  %s  •  %s  •  Epoch: %.0f",
                         dec_str, tz_res.diff_from_local, tz_res.offset_str, round(val));
            } else {
                snprintf(out, out_sz, "%s  •  %s  •  Epoch: %.0f",
                         tz_res.diff_from_local, tz_res.offset_str, round(val));
            }
            return;
        }
    }

    /* Date / Time conversion result */
    if (is_date || is_time ||
        (words_sfx && (strcasecmp(words_sfx, "Epoch Seconds") == 0 ||
                      strcasecmp(words_sfx, "Universal Time") == 0 ||
                      (unit_sym && (strcasecmp(unit_sym, "UTC") == 0 ||
                                    strcasecmp(unit_sym, "AM") == 0 ||
                                    strcasecmp(unit_sym, "PM") == 0 ||
                                    strcasecmp(unit_sym, "24h") == 0)) ||
                      strcasecmp(words_sfx, "Time (12h)") == 0 ||
                      strcasecmp(words_sfx, "Time (24h)") == 0))) {
        time_t t = (time_t)round(val);
        struct tm tm_l;
        localtime_r(&t, &tm_l);
        char day_name[32];
        strftime(day_name, sizeof(day_name), "%A", &tm_l);
        int day_of_year = tm_l.tm_yday + 1;
        char week_num[16];
        strftime(week_num, sizeof(week_num), "Week %V", &tm_l);

        if (mode == OUTPUT_ALL) {
            snprintf(out, out_sz, "Day: %s  •  %s  •  Day %d of %d  •  Epoch: %.0f",
                     day_name, week_num, day_of_year, tm_l.tm_year + 1900, round(val));
        } else {
            snprintf(out, out_sz, "%s  •  %s (Day %d)  •  Epoch: %.0f",
                     day_name, week_num, day_of_year, round(val));
        }
        return;
    }

    if (mode == OUTPUT_ALL) {
        snprintf(out, out_sz, "Dec: %s", dec_str);
    } else {
        out[0] = '\0';
    }
}

typedef struct {
    const char *symbol;     /* Matches find_unit symbol */
    const char *display;    /* Canonical label for display */
    double to_base;         /* Factor to base unit in that dimension */
} CompoundStep;

typedef struct {
    const char *unit_sym;
    const char *singular_name;
    const char *plural_name;
} PreviewUnit;

typedef struct {
    const char *system_name;
    Dimension dim;
    CompoundStep steps[5];
    int step_count;
    PreviewUnit preview_units[4];
    int preview_count;
} CompoundSystem;

static const CompoundSystem g_compound_systems[] = {
    {
        "time",
        DIM_TIME,
        {
            { "d",   "day", 86400.0 },
            { "h",   "hr",   3600.0 },
            { "min", "min",    60.0 },
            { "s",   "s",       1.0 }
        },
        4,
        {
            { "d",   "day",    "days" },
            { "h",   "hour",   "hours" },
            { "min", "minute", "minutes" },
            { "s",   "second", "seconds" }
        },
        4
    },
    {
        "imperial_length",
        DIM_LENGTH,
        {
            { "ft",  "ft",   0.3048 },
            { "in",  "in",   0.0254 }
        },
        2,
        {
            { "in", "in", "in" },
            { "ft", "ft", "ft" },
            { "m",  "m",  "m" }
        },
        3
    },
    {
        "imperial_mass",
        DIM_MASS,
        {
            { "lb",  "lb",   453.59237 },
            { "oz",  "oz",   453.59237 / 16.0 }
        },
        2,
        {
            { "oz", "oz", "oz" },
            { "lb", "lb", "lb" },
            { "kg", "kg", "kg" }
        },
        3
    }
};

static const CompoundSystem *find_compound_system(const char *unit_sym) {
    if (!unit_sym || !*unit_sym) return NULL;
    const UnitItem *u = find_unit(unit_sym);
    if (!u) return NULL;

    for (size_t i = 0; i < sizeof(g_compound_systems)/sizeof(g_compound_systems[0]); i++) {
        const CompoundSystem *sys = &g_compound_systems[i];
        if (sys->dim != u->dim) continue;
        for (int s = 0; s < sys->step_count; s++) {
            if (strcasecmp(u->symbol, sys->steps[s].symbol) == 0 ||
                strcasecmp(unit_sym, sys->steps[s].symbol) == 0 ||
                strcasecmp(unit_sym, sys->steps[s].display) == 0) {
                return sys;
            }
            const UnitItem *step_u = find_unit(sys->steps[s].symbol);
            if (step_u && strcasecmp(step_u->symbol, u->symbol) == 0) {
                return sys;
            }
        }
    }
    return NULL;
}

bool are_units_same_compound_system(const char *u1, const char *u2) {
    if (!u1 || !u2) return false;
    const CompoundSystem *s1 = find_compound_system(u1);
    const CompoundSystem *s2 = find_compound_system(u2);
    if (!s1 || !s2) return false;
    return (s1 == s2);
}

bool format_compound_unit(double val, const char *unit_sym, char *out, size_t out_sz) {
    if (!out || out_sz == 0 || !unit_sym || !*unit_sym) return false;
    const CompoundSystem *sys = find_compound_system(unit_sym);
    if (!sys) return false;
    const UnitItem *u = find_unit(unit_sym);
    if (!u) return false;
    if (isnan(val) || isinf(val)) return false;

    double base_val = val * u->to_base;
    bool is_neg = base_val < 0.0;
    double abs_base = fabs(base_val);
    if (abs_base < 1e-9) return false;

    const CompoundStep *smallest = &sys->steps[sys->step_count - 1];
    /* If value is strictly less than smallest step and was expressed in that unit, keep decimal */
    if (abs_base < smallest->to_base && (strcasecmp(unit_sym, smallest->symbol) == 0 || strcasecmp(unit_sym, smallest->display) == 0)) {
        return false;
    }

    /* Decompose into steps */
    long long counts[8] = {0};
    double rem = abs_base;

    for (int i = 0; i < sys->step_count; i++) {
        double factor = sys->steps[i].to_base;
        if (i == sys->step_count - 1) {
            double step_cnt = rem / factor;
            counts[i] = (long long)round(step_cnt);
            rem -= (double)counts[i] * factor;
        } else {
            counts[i] = (long long)(rem / factor + 1e-9);
            rem -= (double)counts[i] * factor;
        }
    }

    /* Handle carryover from rounding smallest step */
    for (int i = sys->step_count - 1; i > 0; i--) {
        double ratio = sys->steps[i - 1].to_base / sys->steps[i].to_base;
        long long r_int = (long long)round(ratio);
        if (r_int > 0 && counts[i] >= r_int) {
            counts[i - 1] += counts[i] / r_int;
            counts[i] %= r_int;
        }
    }

    /* Check if this is an exact single integer unit matching input unit */
    int active_steps = 0;
    int single_active_idx = -1;
    for (int i = 0; i < sys->step_count; i++) {
        if (counts[i] > 0) {
            active_steps++;
            single_active_idx = i;
        }
    }

    if (active_steps == 1 && single_active_idx >= 0) {
        const CompoundStep *step = &sys->steps[single_active_idx];
        if (strcasecmp(u->symbol, step->symbol) == 0 || strcasecmp(unit_sym, step->display) == 0) {
            return false;
        }
    }

    if (active_steps == 0) return false;

    /* Build formatted compound string */
    char buf[128] = "";
    size_t pos = 0;
    if (is_neg) {
        pos += snprintf(buf + pos, sizeof(buf) - pos, "-");
    }

    bool has_prev = false;
    for (int i = 0; i < sys->step_count; i++) {
        if (counts[i] > 0) {
            if (has_prev) pos += snprintf(buf + pos, sizeof(buf) - pos, " ");
            const char *lbl = sys->steps[i].display;
            if (strcmp(lbl, "day") == 0 && counts[i] > 1) lbl = "days";
            pos += snprintf(buf + pos, sizeof(buf) - pos, "%lld %s", counts[i], lbl);
            has_prev = true;
        }
    }

    if (pos == 0) return false;
    snprintf(out, out_sz, "%s", buf);
    return true;
}

bool format_compound_unit_downward(double val, const char *unit_sym, char *out, size_t out_sz) {
    if (!out || out_sz == 0 || !unit_sym || !*unit_sym) return false;
    const CompoundSystem *sys = find_compound_system(unit_sym);
    if (!sys) return false;
    const UnitItem *u = find_unit(unit_sym);
    if (!u) return false;
    if (isnan(val) || isinf(val)) return false;

    int start_idx = -1;
    for (int s = 0; s < sys->step_count; s++) {
        if (strcasecmp(u->symbol, sys->steps[s].symbol) == 0 ||
            strcasecmp(unit_sym, sys->steps[s].symbol) == 0 ||
            strcasecmp(unit_sym, sys->steps[s].display) == 0) {
            start_idx = s;
            break;
        }
        const UnitItem *step_u = find_unit(sys->steps[s].symbol);
        if (step_u && strcasecmp(step_u->symbol, u->symbol) == 0) {
            start_idx = s;
            break;
        }
    }
    if (start_idx < 0) return false;
    if (start_idx >= sys->step_count - 1) return false;

    double base_val = val * u->to_base;
    bool is_neg = base_val < 0.0;
    double abs_base = fabs(base_val);
    if (abs_base < 1e-9) return false;

    /* Decompose into steps starting from start_idx */
    long long counts[8] = {0};
    double rem = abs_base;

    for (int i = start_idx; i < sys->step_count; i++) {
        double factor = sys->steps[i].to_base;
        if (i == sys->step_count - 1) {
            double step_cnt = rem / factor;
            counts[i] = (long long)round(step_cnt);
            rem -= (double)counts[i] * factor;
        } else {
            counts[i] = (long long)(rem / factor + 1e-9);
            rem -= (double)counts[i] * factor;
        }
    }

    /* Handle carryover from rounding smallest step */
    for (int i = sys->step_count - 1; i > start_idx; i--) {
        double ratio = sys->steps[i - 1].to_base / sys->steps[i].to_base;
        long long r_int = (long long)round(ratio);
        if (r_int > 0 && counts[i] >= r_int) {
            counts[i - 1] += counts[i] / r_int;
            counts[i] %= r_int;
        }
    }

    /* Check if this is an exact single integer unit matching start_idx */
    int active_steps = 0;
    int single_active_idx = -1;
    for (int i = start_idx; i < sys->step_count; i++) {
        if (counts[i] > 0) {
            active_steps++;
            single_active_idx = i;
        }
    }

    if (active_steps == 1 && single_active_idx == start_idx) {
        return false;
    }

    if (active_steps == 0) return false;

    /* Build formatted compound string */
    char buf[128] = "";
    size_t pos = 0;
    if (is_neg) {
        pos += snprintf(buf + pos, sizeof(buf) - pos, "-");
    }

    bool has_prev = false;
    for (int i = start_idx; i < sys->step_count; i++) {
        if (counts[i] > 0) {
            if (has_prev) pos += snprintf(buf + pos, sizeof(buf) - pos, " ");
            const char *lbl = sys->steps[i].display;
            if (strcmp(lbl, "day") == 0 && counts[i] > 1) lbl = "days";
            pos += snprintf(buf + pos, sizeof(buf) - pos, "%lld %s", counts[i], lbl);
            has_prev = true;
        }
    }

    if (pos == 0) return false;
    snprintf(out, out_sz, "%s", buf);
    return true;
}

bool format_compound_subtitle(double val, const char *unit_sym, char *out, size_t out_sz) {
    if (!out || out_sz == 0 || !unit_sym || !*unit_sym) return false;
    const CompoundSystem *sys = find_compound_system(unit_sym);
    if (!sys) return false;
    const UnitItem *u = find_unit(unit_sym);
    if (!u) return false;
    if (isnan(val) || isinf(val)) return false;

    double base_val = fabs(val * u->to_base);
    if (base_val < 1e-9) return false;

    char parts[4][96];
    int p_count = 0;
    char num_str[64];

    if (sys->dim == DIM_TIME) {
        int start_i = 0, end_i = 3;
        if (base_val >= 86400.0) {
            start_i = 0; end_i = 2; /* d, h, min */
        } else if (base_val >= 3600.0) {
            start_i = 1; end_i = 3; /* h, min, s */
        } else if (base_val >= 60.0) {
            start_i = 2; end_i = 3; /* min, s */
        } else {
            return false;
        }
        for (int i = start_i; i <= end_i && i < sys->preview_count; i++) {
            const PreviewUnit *pu = &sys->preview_units[i];
            const UnitItem *pu_u = find_unit(pu->unit_sym);
            if (!pu_u || pu_u->to_base <= 0) continue;
            double step_val = base_val / pu_u->to_base;
            format_result_ex(step_val, comma_fmt, num_str, sizeof(num_str));
            const char *lbl = (fabs(step_val - 1.0) < 1e-6) ? pu->singular_name : pu->plural_name;
            snprintf(parts[p_count++], sizeof(parts[0]), "%s %s", num_str, lbl);
        }
    } else {
        for (int i = 0; i < sys->preview_count; i++) {
            const PreviewUnit *pu = &sys->preview_units[i];
            const UnitItem *pu_u = find_unit(pu->unit_sym);
            if (!pu_u || pu_u->to_base <= 0) continue;
            double step_val = base_val / pu_u->to_base;
            if (fabs(step_val - round(step_val)) < 1e-5) step_val = round(step_val);
            format_result_ex(step_val, comma_fmt, num_str, sizeof(num_str));
            snprintf(parts[p_count++], sizeof(parts[0]), "%s %s", num_str, pu->singular_name);
        }
    }

    if (p_count == 0) return false;
    char buf[256] = "";
    size_t pos = 0;
    for (int i = 0; i < p_count; i++) {
        if (i > 0) pos += snprintf(buf + pos, sizeof(buf) - pos, "  •  ");
        pos += snprintf(buf + pos, sizeof(buf) - pos, "%s", parts[i]);
    }
    snprintf(out, out_sz, "%s", buf);
    return true;
}

bool format_compound_duration(double val, const char *unit_sym, char *out, size_t out_sz) {
    return format_compound_unit(val, unit_sym, out, out_sz);
}

bool format_duration_subtitle(double val, const char *unit_sym, char *out, size_t out_sz) {
    return format_compound_subtitle(val, unit_sym, out, out_sz);
}

void format_subtitle_preview(double val, const char *unit_sym, const char *words_sfx, bool is_date, bool is_time, char *out, size_t out_sz) {
    if (!out || out_sz == 0) return;
    out[0] = '\0';
    if (isnan(val) || isinf(val)) {
        snprintf(out, out_sz, "—");
        return;
    }

    /* 0. Compound unit subtitle (time, imperial length, imperial mass) */
    if (!is_date && !is_time && unit_sym && unit_sym[0]) {
        char comp_sub[256];
        if (format_compound_subtitle(val, unit_sym, comp_sub, sizeof(comp_sub))) {
            snprintf(out, out_sz, "%s", comp_sub);
            return;
        }
    }

    /* 0. Date or Time result */
    if (is_date || is_time) {
        time_t t = (time_t)round(val);
        struct tm tm_l;
        localtime_r(&t, &tm_l);

        char date_str[64];
        strftime(date_str, sizeof(date_str), "%A, %B %d, %Y", &tm_l);

        /* Relative day indicator */
        time_t now_t = time(NULL);
        struct tm now_tm;
        localtime_r(&now_t, &now_tm);
        now_tm.tm_hour = 0;
        now_tm.tm_min = 0;
        now_tm.tm_sec = 0;
        time_t today_mid = mktime(&now_tm);

        struct tm target_tm = tm_l;
        target_tm.tm_hour = 0;
        target_tm.tm_min = 0;
        target_tm.tm_sec = 0;
        time_t target_mid = mktime(&target_tm);

        long diff_days = (long)round(difftime(target_mid, today_mid) / 86400.0);
        char rel_tag[32] = "";
        if (diff_days == 0) snprintf(rel_tag, sizeof(rel_tag), "Today");
        else if (diff_days == 1) snprintf(rel_tag, sizeof(rel_tag), "Tomorrow");
        else if (diff_days == -1) snprintf(rel_tag, sizeof(rel_tag), "Yesterday");
        else if (diff_days > 1) snprintf(rel_tag, sizeof(rel_tag), "+%ld days", diff_days);
        else if (diff_days < -1) snprintf(rel_tag, sizeof(rel_tag), "%ld days", diff_days);

        if (is_time || (tm_l.tm_hour != 0 || tm_l.tm_min != 0 || tm_l.tm_sec != 0)) {
            char time_str[32];
            strftime(time_str, sizeof(time_str), "%I:%M:%S %p", &tm_l);
            if (rel_tag[0]) {
                snprintf(out, out_sz, "%s at %s (%s)", date_str, time_str, rel_tag);
            } else {
                snprintf(out, out_sz, "%s at %s", date_str, time_str);
            }
        } else {
            if (rel_tag[0]) {
                snprintf(out, out_sz, "%s (%s)", date_str, rel_tag);
            } else {
                snprintf(out, out_sz, "%s", date_str);
            }
        }
        return;
    }

    /* 1. Date / Day of week / Timezone results */
    if (words_sfx && (strcasecmp(words_sfx, "Monday") == 0 ||
                      strcasecmp(words_sfx, "Tuesday") == 0 ||
                      strcasecmp(words_sfx, "Wednesday") == 0 ||
                      strcasecmp(words_sfx, "Thursday") == 0 ||
                      strcasecmp(words_sfx, "Friday") == 0 ||
                      strcasecmp(words_sfx, "Saturday") == 0 ||
                      strcasecmp(words_sfx, "Sunday") == 0)) {
        snprintf(out, out_sz, "%s", words_sfx);
        return;
    }
    if (val > 100000000.0 && words_sfx && words_sfx[0]) {
        snprintf(out, out_sz, "%s", words_sfx);
        return;
    }

    /* 2. Spoken words translation */
    bool use_indian = (comma_fmt == FMT_INDIAN) &&
                      (unit_sym == NULL || strcasecmp(unit_sym, "inr") == 0);

    char compact_buf[64] = "";
    format_compact_number(val, use_indian, compact_buf, sizeof(compact_buf));

    char words_part[384] = "";
    number_to_words(val, use_indian, words_part, sizeof(words_part));
    if (words_sfx && words_sfx[0] &&
        strcasecmp(words_sfx, "Hexadecimal") != 0 &&
        strcasecmp(words_sfx, "Binary") != 0 &&
        strcasecmp(words_sfx, "Octal") != 0 &&
        strcasecmp(words_sfx, "Decimal") != 0 &&
        strcasecmp(words_sfx, "Epoch Seconds") != 0 &&
        strcasecmp(words_sfx, "Universal Time") != 0 &&
        strcasecmp(words_sfx, "Time (12h)") != 0 &&
        strcasecmp(words_sfx, "Time (24h)") != 0) {
        strncat(words_part, " ", sizeof(words_part) - strlen(words_part) - 1);
        strncat(words_part, words_sfx, sizeof(words_part) - strlen(words_part) - 1);
    }

    if (compact_buf[0]) {
        if (unit_sym && unit_sym[0]) {
            snprintf(out, out_sz, "≈ %s %s  •  %s", compact_buf, unit_sym, words_part);
        } else {
            snprintf(out, out_sz, "≈ %s  •  %s", compact_buf, words_part);
        }
    } else {
        snprintf(out, out_sz, "%s", words_part);
    }
}

bool extract_trailing_binary_op(const char *expr, char *out_op, size_t out_sz) {
    if (!expr || !out_op || out_sz < 8) return false;
    out_op[0] = '\0';
    size_t len = strlen(expr);
    if (len == 0) return false;

    int depth = 0;
    ssize_t op_pos = -1;

    for (ssize_t i = (ssize_t)len - 1; i >= 0; i--) {
        char c = expr[i];
        if (c == ')') depth++;
        else if (c == '(') { if (depth > 0) depth--; }
        else if (depth == 0) {
            if (c == '+' || c == '-' || c == '*' || c == '/' || c == '^' || c == '%') {
                /* A binary operator must have a right operand */
                ssize_t next = i + 1;
                while (next < (ssize_t)len && isspace((unsigned char)expr[next])) next++;
                if (next >= (ssize_t)len || expr[next] == ')' || expr[next] == ']') {
                    continue;
                }

                /* Verify this is a binary operator, not a unary prefix or sci notation */
                ssize_t prev = i - 1;
                while (prev >= 0 && isspace((unsigned char)expr[prev])) prev--;
                if (prev >= 0) {
                    char prev_c = expr[prev];
                    /* If preceded by 'e' or 'E' and a digit, might be scientific notation */
                    if ((c == '+' || c == '-') && (prev_c == 'e' || prev_c == 'E') && prev > 0 && isdigit((unsigned char)expr[prev - 1])) {
                        continue;
                    }
                    /* If preceded by an operand or closing paren */
                    if (isalnum((unsigned char)prev_c) || prev_c == ')' || prev_c == '%' || prev_c == ']' || prev_c == '"') {
                        op_pos = i;
                        break;
                    }
                }
            }
        }
    }

    if (op_pos < 0) return false;

    /* Extract from op_pos to end of expr */
    const char *op_str = expr + op_pos;
    while (*op_str && isspace((unsigned char)*op_str)) op_str++;
    if (!*op_str) return false;

    snprintf(out_op, out_sz, "%s", op_str);
    size_t out_len = strlen(out_op);
    while (out_len > 0 && isspace((unsigned char)out_op[out_len - 1])) {
        out_op[--out_len] = '\0';
    }
    return (out_len > 1);
}

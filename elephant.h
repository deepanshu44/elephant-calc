#ifndef ELEPHANT_H
#define ELEPHANT_H

#include <stdbool.h>
#include <stddef.h>
#include "brackets.h"

#define MAX_HISTORY 128

typedef enum {
    FMT_INTL = 0,
    FMT_INDIAN,
    FMT_PLAIN
} CommaFormat;

typedef enum {
    OUTPUT_COMP_ONLY = 0,
    OUTPUT_ALL,
    OUTPUT_SIMPLE
} OutputMode;

extern CommaFormat comma_fmt;
extern int decimal_places;
extern OutputMode output_mode;
extern char custom_config_path[512];

/* ==========================================================================
 * Dimensions & Units
 * ========================================================================== */
#define MAX_ALIASES 8

typedef enum {
    DIM_UNKNOWN = 0,
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
} Dimension;

typedef struct {
    char symbol[32];
    char aliases[MAX_ALIASES][32];
    int alias_count;
    Dimension dim;
    double to_base; /* Multiply to reach base unit */
} UnitItem;

const UnitItem *find_unit(const char *name);
const char *dim_name(Dimension d);

/* ==========================================================================
 * Storage & Persistence (storage.c)
 * ========================================================================== */
void get_elephant_file_path(const char *filename, char *out, size_t out_sz);
double load_ans(void);
void save_ans(double val);
int load_history(char history[MAX_HISTORY][256]);
void save_history_entry(const char *entry);
CommaFormat load_settings(int *decimals_out, double *font_scale_out, OutputMode *output_mode_out);
void save_settings(CommaFormat fmt, int decimals, double font_scale, OutputMode mode);

/* ==========================================================================
 * Variables, Functions & Persistence (parser.c / storage.c)
 * ========================================================================== */
#define MAX_PARAMS 4
#define MAX_SYMBOLS 128

typedef struct {
    char name[32];
    bool is_func;
    int param_count;
    char params[MAX_PARAMS][16];
    char expr[128];         /* dynamic formula expression or function body */
    double cached_val;      /* evaluated scalar or timestamp */
    char unit[32];          /* unit or timezone */
    bool is_date;
    bool is_time;
    bool is_persistent;     /* true if saved across sessions (via :=) */
} CalcSymbol;

bool define_symbol(const char *name, const char *expr, bool is_func, int param_count, const char params[][16], bool persistent);
bool define_symbol_ex(const char *name, const char *expr, bool is_func, int param_count, const char params[][16],
                      bool persistent, double cached_val, const char *unit, bool is_date, bool is_time);
bool get_symbol(const char *name, CalcSymbol *out);
bool delete_symbol(const char *name);
void clear_all_symbols(void);
int get_all_symbols(CalcSymbol out[], int max_count);
int format_saved_symbols_list(char *buf, size_t buf_sz);

void load_saved_symbols(void);
void save_saved_symbols(void);

bool set_variable(const char *name, double val);
bool get_variable(const char *name, double *val);
void load_variables(void);
void save_variables(void);
void clear_variables(void);
int get_all_variables(char names[][32], double *vals, int max_count);

/* ==========================================================================
 * Math Expression Parser (parser.c)
 * ========================================================================== */
bool evaluate_equation(const char *input, double ans_val, double *result, const char **err_msg);
bool evaluate_equation_ex(const char *input, double ans_val, double *result,
                          char *unit_symbol, size_t unit_sz,
                          bool *is_date_out, bool *is_time_out, const char **err_msg);
void set_ans_date_time(bool is_date, bool is_time);

/* ==========================================================================
 * Formatting & Words (format.c)
 * ========================================================================== */
void format_result(double val, char *out, size_t out_sz);
void format_result_ex(double val, CommaFormat fmt, char *out, size_t out_sz);
void format_date_result(double val, char *out, size_t out_sz);
void format_time_result(double val, bool is_24h, char *out, size_t out_sz);
void format_compact_number(double val, bool indian, char *out, size_t out_sz);
void format_computing_row(double val, OutputMode mode, const char *unit_sym, const char *words_sfx, bool is_date, bool is_time, char *out, size_t out_sz);
void format_subtitle_preview(double val, const char *unit_sym, const char *words_sfx, bool is_date, bool is_time, char *out, size_t out_sz);
bool format_compound_unit(double val, const char *unit_sym, char *out, size_t out_sz);
bool format_compound_unit_downward(double val, const char *unit_sym, char *out, size_t out_sz);
bool format_compound_subtitle(double val, const char *unit_sym, char *out, size_t out_sz);
bool are_units_same_compound_system(const char *u1, const char *u2);
bool format_compound_duration(double val, const char *unit_sym, char *out, size_t out_sz);
bool format_duration_subtitle(double val, const char *unit_sym, char *out, size_t out_sz);
void format_number_token(const char *raw, char *out, size_t out_sz);
void reformat_buffer_numbers(char *buf, size_t buf_sz, size_t *cursor_pos, size_t *input_len);
void maybe_format_number_before_cursor(char *buf, size_t buf_sz, size_t *cursor_pos, size_t *input_len, char next_char);
void number_to_words(double val, bool indian, char *out, size_t sz);
void format_bin_string(long long val, char *buf, size_t buf_sz);
bool extract_trailing_binary_op(const char *expr, char *out_op, size_t out_sz);

/* ==========================================================================
 * Unit Conversion Engine (convert.c)
 * ========================================================================== */
void init_units(void);
void print_units_help(void);
bool get_conversion_hints(const char *input, char *hint_out, size_t hint_sz);
bool try_convert_expression(const char *input, double ans_val,
                            double *converted_val, char *result_buf, size_t res_sz,
                            char *unit_symbol, size_t unit_sz,
                            char *words_suffix, size_t suffix_sz, const char **err_msg);
bool update_currency_rate(const char *symbol, double rate_per_usd);
void load_cached_currency_rates(void);

#endif /* ELEPHANT_H */

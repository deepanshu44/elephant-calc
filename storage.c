#define _POSIX_C_SOURCE 200809L
#include "elephant.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>
#include <sys/types.h>

char custom_config_path[512] = "";

void get_elephant_file_path(const char *filename, char *out, size_t out_sz) {
    if (!out || out_sz == 0) return;
    if (strcmp(filename, "settings.conf") == 0 && custom_config_path[0] != '\0') {
        snprintf(out, out_sz, "%s", custom_config_path);
        return;
    }

    const char *config_home = getenv("XDG_CONFIG_HOME");
    const char *home = getenv("HOME");
    if (config_home && *config_home) {
        char dir[384];
        snprintf(dir, sizeof(dir), "%s/elephant-calc", config_home);
        mkdir(dir, 0755);
        snprintf(out, out_sz, "%s/%s", dir, filename);
    } else if (home && *home) {
        char base[384];
        snprintf(base, sizeof(base), "%s/.config", home);
        mkdir(base, 0755);
        char dir[384];
        snprintf(dir, sizeof(dir), "%s/.config/elephant-calc", home);
        mkdir(dir, 0755);
        snprintf(out, out_sz, "%s/%s", dir, filename);
    } else {
        snprintf(out, out_sz, "/tmp/.elephant_calc_%s", filename);
    }
}

double load_ans(void) {
    double val = 0.0;
    char path[512];
    get_elephant_file_path("ans", path, sizeof(path));
    FILE *f = fopen(path, "r");
    if (f) {
        if (fscanf(f, "%lf", &val) != 1) {
            val = 0.0;
        }
        fclose(f);
    }
    return val;
}

void save_ans(double val) {
    char path[512];
    get_elephant_file_path("ans", path, sizeof(path));
    FILE *f = fopen(path, "w");
    if (f) {
        fprintf(f, "%.14g\n", val);
        fclose(f);
    }
}

int load_history(char history[MAX_HISTORY][256]) {
    int count = 0;
    char path[512];
    get_elephant_file_path("history", path, sizeof(path));
    FILE *f = fopen(path, "r");
    if (!f) return 0;

    char line[256];
    while (fgets(line, sizeof(line), f)) {
        size_t l = strlen(line);
        while (l > 0 && (line[l - 1] == '\n' || line[l - 1] == '\r')) {
            line[--l] = '\0';
        }
        if (l > 0) {
            if (count < MAX_HISTORY) {
                snprintf(history[count++], sizeof(history[0]), "%s", line);
            } else {
                memmove(history[0], history[1], (MAX_HISTORY - 1) * sizeof(history[0]));
                snprintf(history[MAX_HISTORY - 1], sizeof(history[0]), "%s", line);
            }
        }
    }
    fclose(f);
    return count;
}

void save_history_entry(const char *entry) {
    if (!entry || strlen(entry) == 0) return;

    char path[512];
    get_elephant_file_path("history", path, sizeof(path));
    FILE *f = fopen(path, "a+");
    if (!f) return;

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    if (sz > 0) {
        long read_sz = sz > 512 ? 512 : sz;
        fseek(f, -read_sz, SEEK_END);
        char buf[513];
        size_t n = fread(buf, 1, (size_t)read_sz, f);
        buf[n] = '\0';
        char *last_nl = strrchr(buf, '\n');
        if (last_nl && *(last_nl + 1) != '\0') {
            if (strcmp(last_nl + 1, entry) == 0) {
                fclose(f);
                return;
            }
        } else if (last_nl && last_nl > buf) {
            *last_nl = '\0';
            char *prev_nl = strrchr(buf, '\n');
            char *last_line = prev_nl ? prev_nl + 1 : buf;
            if (strcmp(last_line, entry) == 0) {
                fclose(f);
                return;
            }
        }
    }

    fseek(f, 0, SEEK_END);
    fprintf(f, "%s\n", entry);
    fclose(f);
}

void save_settings(CommaFormat fmt, int decimals, double font_scale, OutputMode mode) {
    char path[512];
    get_elephant_file_path("settings.conf", path, sizeof(path));
    FILE *f = fopen(path, "w");
    if (f) {
        fprintf(f, "comma_format=%s\n", (fmt == FMT_INDIAN) ? "indian" : (fmt == FMT_PLAIN) ? "plain" : "intl");
        if (decimals < 0) {
            fprintf(f, "decimal_places=auto\n");
        } else {
            fprintf(f, "decimal_places=%d\n", decimals);
        }
        if (font_scale <= 0.0) font_scale = 1.0;
        fprintf(f, "font_scale=%.2f\n", font_scale);
        const char *m_str = "comp_only";
        if (mode == OUTPUT_ALL) m_str = "all";
        else if (mode == OUTPUT_SIMPLE) m_str = "simple";
        fprintf(f, "output_mode=%s\n", m_str);
        fclose(f);
    }
}

CommaFormat load_settings(int *decimals_out, double *font_scale_out, OutputMode *output_mode_out) {
    char path[512];
    get_elephant_file_path("settings.conf", path, sizeof(path));
    if (decimals_out) *decimals_out = -1;
    if (font_scale_out) *font_scale_out = 1.0;
    if (output_mode_out) *output_mode_out = OUTPUT_COMP_ONLY;
    FILE *f = fopen(path, "r");
    if (!f) return FMT_INTL;
    char line[128];
    CommaFormat res = FMT_INTL;
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "comma_format=indian", 19) == 0) res = FMT_INDIAN;
        else if (strncmp(line, "comma_format=plain", 18) == 0) res = FMT_PLAIN;
        else if (strncmp(line, "comma_format=intl", 17) == 0) res = FMT_INTL;
        else if (strncmp(line, "decimal_places=auto", 19) == 0) {
            if (decimals_out) *decimals_out = -1;
        } else if (strncmp(line, "decimal_places=", 15) == 0) {
            if (decimals_out) *decimals_out = atoi(line + 15);
        } else if (strncmp(line, "font_scale=", 11) == 0) {
            if (font_scale_out) {
                double sc = atof(line + 11);
                if (sc < 0.8) sc = 0.8;
                if (sc > 1.8) sc = 1.8;
                *font_scale_out = sc;
            }
        } else if (strncmp(line, "output_mode=simple", 18) == 0) {
            if (output_mode_out) *output_mode_out = OUTPUT_SIMPLE;
        } else if (strncmp(line, "output_mode=all", 15) == 0) {
            if (output_mode_out) *output_mode_out = OUTPUT_ALL;
        } else if (strncmp(line, "output_mode=comp_only", 21) == 0 ||
                   strncmp(line, "output_mode=computing", 21) == 0) {
            if (output_mode_out) *output_mode_out = OUTPUT_COMP_ONLY;
        }
    }
    fclose(f);
    return res;
}

void save_saved_symbols(void) {
    CalcSymbol syms[MAX_SYMBOLS];
    int count = get_all_symbols(syms, MAX_SYMBOLS);

    char path[512];
    get_elephant_file_path("vars.conf", path, sizeof(path));
    FILE *f = fopen(path, "w");
    if (!f) return;

    fprintf(f, "# Elephant Calculator - Persistent Symbols\n");
    for (int i = 0; i < count; i++) {
        if (!syms[i].is_persistent) continue;
        if (syms[i].is_func) {
            fprintf(f, "%s(", syms[i].name);
            for (int p = 0; p < syms[i].param_count; p++) {
                if (p > 0) fprintf(f, ", ");
                fprintf(f, "%s", syms[i].params[p]);
            }
            fprintf(f, ") := %s\n", syms[i].expr);
        } else {
            if (syms[i].expr[0] != '\0') {
                fprintf(f, "%s := %s\n", syms[i].name, syms[i].expr);
            } else {
                fprintf(f, "%s := %.14g\n", syms[i].name, syms[i].cached_val);
            }
        }
    }
    fclose(f);
}

void load_saved_symbols(void) {
    char path[512];
    get_elephant_file_path("vars.conf", path, sizeof(path));
    FILE *f = fopen(path, "r");
    if (!f) return;

    char line[256];
    while (fgets(line, sizeof(line), f)) {
        size_t l = strlen(line);
        while (l > 0 && (line[l - 1] == '\n' || line[l - 1] == '\r')) {
            line[--l] = '\0';
        }
        if (l == 0 || line[0] == '#') continue;

        char *assign = strstr(line, ":=");
        size_t op_len = 2;
        if (!assign) {
            assign = strchr(line, '=');
            op_len = 1;
        }
        if (!assign) continue;

        *assign = '\0';
        char *lhs = line;
        char *rhs = assign + op_len;
        while (*lhs && isspace((unsigned char)*lhs)) lhs++;
        char *lhs_end = lhs + strlen(lhs);
        while (lhs_end > lhs && isspace((unsigned char)*(lhs_end - 1))) lhs_end--;
        *lhs_end = '\0';

        while (*rhs && isspace((unsigned char)*rhs)) rhs++;
        char *rhs_end = rhs + strlen(rhs);
        while (rhs_end > rhs && isspace((unsigned char)*(rhs_end - 1))) rhs_end--;
        *rhs_end = '\0';

        if (lhs[0] == '\0' || rhs[0] == '\0') continue;

        char *open_p = strchr(lhs, '(');
        char *close_p = strrchr(lhs, ')');
        if (open_p && close_p && close_p > open_p) {
            *open_p = '\0';
            char *fn_name = lhs;
            while (*fn_name && isspace((unsigned char)*fn_name)) fn_name++;
            char *fn_end = fn_name + strlen(fn_name);
            while (fn_end > fn_name && isspace((unsigned char)*(fn_end - 1))) fn_end--;
            *fn_end = '\0';

            char *param_str = open_p + 1;
            *close_p = '\0';

            char params[MAX_PARAMS][16];
            int param_count = 0;
            char *token = strtok(param_str, ",");
            while (token && param_count < MAX_PARAMS) {
                while (*token && isspace((unsigned char)*token)) token++;
                char *tok_end = token + strlen(token);
                while (tok_end > token && isspace((unsigned char)*(tok_end - 1))) tok_end--;
                *tok_end = '\0';
                if (*token) {
                    snprintf(params[param_count++], 16, "%s", token);
                }
                token = strtok(NULL, ",");
            }
            define_symbol(fn_name, rhs, true, param_count, (const char (*)[16])params, true);
        } else {
            define_symbol(lhs, rhs, false, 0, NULL, true);
        }
    }
    fclose(f);
}

void save_variables(void) {
    save_saved_symbols();
}

void load_variables(void) {
    load_saved_symbols();
}

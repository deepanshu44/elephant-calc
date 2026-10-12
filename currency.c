#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE
#include "currency.h"
#include "elephant.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/stat.h>
#include <time.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>

void currency_init_state(CurrencyState *state) {
    if (!state) return;
    memset(state, 0, sizeof(*state));
    state->status = CURRENCY_IDLE;

    char path[512];
    get_elephant_file_path("currency_rates.conf", path, sizeof(path));
    struct stat st;
    if (stat(path, &st) == 0 && st.st_size > 50) {
        struct tm tm_buf;
        localtime_r(&st.st_mtime, &tm_buf);
        strftime(state->timestamp_str, sizeof(state->timestamp_str), "%b %d %H:%M", &tm_buf);
        snprintf(state->msg, sizeof(state->msg), "Cached (updated %s)", state->timestamp_str);
    } else {
        snprintf(state->msg, sizeof(state->msg), "Snapshot rates (offline cache)");
    }
}

static bool parse_and_apply_rates(const char *json, CurrencyState *state) {
    const char *rates_ptr = strstr(json, "\"rates\"");
    if (!rates_ptr) return false;
    const char *brace = strchr(rates_ptr, '{');
    if (!brace) return false;
    const char *p = brace + 1;

    char conf_path[512];
    get_elephant_file_path("currency_rates.conf", conf_path, sizeof(conf_path));
    FILE *out = fopen(conf_path, "w");
    if (out) {
        time_t now = time(NULL);
        struct tm tm_buf;
        localtime_r(&now, &tm_buf);
        char t_buf[32];
        strftime(t_buf, sizeof(t_buf), "%Y-%m-%d %H:%M:%S", &tm_buf);
        fprintf(out, "# Elephant Calc Currency Rates Cache\n# Updated: %s\n[currency]\n", t_buf);
    }

    int count = 0;
    state->inr_rate = 0.0;

    while (*p && *p != '}') {
        while (*p && *p != '"' && *p != '}') p++;
        if (*p != '"') break;
        p++; /* Skip opening quote */
        char sym[16] = {0};
        int s_idx = 0;
        while (*p && *p != '"' && s_idx < 15) {
            sym[s_idx++] = (char)tolower((unsigned char)*p);
            p++;
        }
        if (*p != '"') break;
        p++; /* Skip closing quote */
        while (*p && (*p == ' ' || *p == ':')) p++;
        char *endptr = NULL;
        double rate = strtod(p, &endptr);
        if (endptr && endptr != p && rate > 0.0) {
            count++;
            update_currency_rate(sym, rate);
            if (out) {
                fprintf(out, "%s = %.6f\n", sym, rate);
            }
            if (strcmp(sym, "inr") == 0) {
                state->inr_rate = rate;
            }
            p = endptr;
        }
        while (*p && *p != ',' && *p != '}') p++;
        if (*p == ',') p++;
    }

    if (out) fclose(out);

    if (count > 0) {
        time_t now = time(NULL);
        struct tm tm_buf;
        localtime_r(&now, &tm_buf);
        strftime(state->timestamp_str, sizeof(state->timestamp_str), "%b %d %H:%M", &tm_buf);
        if (state->inr_rate > 0.0) {
            snprintf(state->msg, sizeof(state->msg), "✓ Updated (1 $ = %.2f ₹)", state->inr_rate);
        } else {
            snprintf(state->msg, sizeof(state->msg), "✓ Updated (%s)", state->timestamp_str);
        }
        return true;
    }
    return false;
}

bool currency_fetch_sync(CurrencyState *state) {
    if (!state) return false;

    FILE *pipe = popen("curl -s --max-time 4 'https://open.er-api.com/v6/latest/USD' 2>/dev/null", "r");
    if (!pipe) {
        state->status = CURRENCY_FAILED;
        snprintf(state->msg, sizeof(state->msg), "⚠ Offline (using cached rates)");
        return false;
    }

    char *buf = malloc(65536);
    if (!buf) {
        pclose(pipe);
        state->status = CURRENCY_FAILED;
        snprintf(state->msg, sizeof(state->msg), "⚠ Memory error");
        return false;
    }

    size_t n = fread(buf, 1, 65535, pipe);
    buf[n] = '\0';
    int ret = pclose(pipe);

    if (ret != 0 || n == 0 || !strstr(buf, "\"rates\"")) {
        free(buf);
        state->status = CURRENCY_FAILED;
        snprintf(state->msg, sizeof(state->msg), "⚠ Offline (using cached rates)");
        return false;
    }

    bool ok = parse_and_apply_rates(buf, state);
    free(buf);

    if (ok) {
        state->status = CURRENCY_SUCCESS;
        return true;
    } else {
        state->status = CURRENCY_FAILED;
        snprintf(state->msg, sizeof(state->msg), "⚠ Parse failed (cached rates kept)");
        return false;
    }
}

static void *currency_worker_thread(void *arg) {
    CurrencyState *state = (CurrencyState *)arg;
    if (!state) return NULL;

    currency_fetch_sync(state);
    state->is_fetching = false;

    if (state->win) {
        Display *tdpy = XOpenDisplay(NULL);
        if (tdpy) {
            XClientMessageEvent cme = {0};
            cme.type = ClientMessage;
            cme.window = state->win;
            cme.message_type = XInternAtom(tdpy, "ELEPHANT_CURRENCY_UPDATE", False);
            cme.format = 32;
            XSendEvent(tdpy, state->win, False, 0, (XEvent *)&cme);
            XFlush(tdpy);
            XCloseDisplay(tdpy);
        }
    }

    return NULL;
}

bool currency_refresh_async(CurrencyState *state, Window win) {
    if (!state || state->is_fetching) return false;

    state->is_fetching = true;
    state->status = CURRENCY_FETCHING;
    state->win = win;
    snprintf(state->msg, sizeof(state->msg), "Updating rates...");

    pthread_t tid;
    if (pthread_create(&tid, NULL, currency_worker_thread, state) != 0) {
        state->is_fetching = false;
        state->status = CURRENCY_FAILED;
        snprintf(state->msg, sizeof(state->msg), "⚠ Thread creation failed");
        return false;
    }
    pthread_detach(tid);
    return true;
}

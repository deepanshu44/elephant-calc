#ifndef ELEPHANT_CURRENCY_H
#define ELEPHANT_CURRENCY_H

#include <stdbool.h>
#include <time.h>
#include <X11/Xlib.h>

typedef enum {
    CURRENCY_IDLE = 0,
    CURRENCY_FETCHING,
    CURRENCY_SUCCESS,
    CURRENCY_FAILED
} CurrencyRefreshStatus;

typedef struct {
    CurrencyRefreshStatus status;
    char msg[80];
    char timestamp_str[32];
    double inr_rate;
    bool is_fetching;
    Window win;
} CurrencyState;

/* Initialize currency state from disk cache timestamp or default snapshot */
void currency_init_state(CurrencyState *state);

/* Launch asynchronous currency refresh in a detached background thread */
bool currency_refresh_async(CurrencyState *state, Window win);

/* Synchronous rate fetch and update */
bool currency_fetch_sync(CurrencyState *state);

#endif /* ELEPHANT_CURRENCY_H */

#ifndef APP_UI_DISPATCH_H
#define APP_UI_DISPATCH_H

#include <stdbool.h>

#include "freertos/FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*app_ui_dispatch_fn_t)(void *ctx);

bool app_ui_dispatch(app_ui_dispatch_fn_t fn, void *ctx, TickType_t timeout_ticks);

#ifdef __cplusplus
}
#endif

#endif

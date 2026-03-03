#ifndef LOGIN_APP_H
#define LOGIN_APP_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*login_app_result_cb)(bool success, const char *token, const char *user);

/**
 * @brief Show the login UI (username + password + button).
 *
 * Creates the screen if needed and loads it as the active screen.
 */
void login_app_show(void);

/**
 * @brief Destroy login UI objects (safe to call even if not created).
 */
void login_app_destroy(void);

/**
 * @brief Register a callback to receive login result.
 */
void login_app_set_result_cb(login_app_result_cb cb);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // LOGIN_APP_H

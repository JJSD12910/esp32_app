#ifndef QUIZ_APP_H
#define QUIZ_APP_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Create and display the quiz application UI.
 *
 * Call this once after LVGL is ready. It builds the home screen with
 * Download, Start Test, and View Results actions.
 */
void quiz_app_create_ui(void);

/**
 * @brief Set current user id for quiz requests.
 */
void quiz_app_set_user_id(const char *user_id);

/**
 * @brief Set current auth token for quiz download requests.
 */
void quiz_app_set_auth_token(const char *token);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // QUIZ_APP_H

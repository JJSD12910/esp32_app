#include "app_flow.h"

#include "esp_log.h"
#include "login_app.h"
#include "quiz_app.h"

static const char *TAG = "app_flow";

static void app_flow_on_login(bool success, const char *token, const char *user)
{
    if (!success)
    {
        return;
    }

    ESP_LOGI(TAG, "Login callback: user=%s token=%s", user ? user : "(null)", (token && token[0]) ? "yes" : "no");
    quiz_app_set_user_id(user);
    quiz_app_set_auth_token(token);
    login_app_destroy();
    quiz_app_create_ui();
}

void app_flow_start(void)
{
    login_app_set_result_cb(app_flow_on_login);
    login_app_show();
}

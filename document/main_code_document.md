# Main + components source code bundle

## main/app_flow.c

```c
#include "app_flow.h"

#include "login_app.h"
#include "quiz_app.h"

static void app_flow_on_login(bool success, const char *token, const char *user)
{
    (void)token;

    if (!success)
    {
        return;
    }

    quiz_app_set_user_id(user);
    login_app_destroy();
    quiz_app_create_ui();
}

void app_flow_start(void)
{
    login_app_set_result_cb(app_flow_on_login);
    login_app_show();
}
```

## main/app_flow.h

```c
#ifndef APP_FLOW_H
#define APP_FLOW_H

#ifdef __cplusplus
extern "C" {
#endif

void app_flow_start(void);

#ifdef __cplusplus
}
#endif

#endif
```

## main/app_network.c

```c
#include "app_network.h"

#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "freertos/event_groups.h"
#include "user_config.h"

#define WIFI_CONNECTED_BIT BIT0

static const char *TAG = "app_network";

static EventGroupHandle_t s_wifi_event_group;
static bool s_wifi_started;

static void app_network_init_netif(void)
{
    static bool netif_ready = false;
    static bool event_loop_ready = false;

    if (!netif_ready)
    {
        esp_err_t ret = esp_netif_init();
        if (ret == ESP_OK || ret == ESP_ERR_INVALID_STATE)
        {
            netif_ready = true;
        }
        else
        {
            ESP_ERROR_CHECK(ret);
        }
    }

    if (!event_loop_ready)
    {
        esp_err_t ret = esp_event_loop_create_default();
        if (ret == ESP_OK || ret == ESP_ERR_INVALID_STATE)
        {
            event_loop_ready = true;
        }
        else
        {
            ESP_ERROR_CHECK(ret);
        }
    }
}

static void app_network_wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void)arg;
    (void)event_data;

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        esp_wifi_connect();
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        esp_wifi_connect();
        if (s_wifi_event_group)
        {
            xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        }
        ESP_LOGW(TAG, "Wi-Fi disconnected, reconnecting...");
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        if (s_wifi_event_group)
        {
            xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        }
    }
}

static void app_network_start_wifi(void)
{
    if (s_wifi_started)
    {
        return;
    }

    if (!s_wifi_event_group)
    {
        s_wifi_event_group = xEventGroupCreate();
        if (!s_wifi_event_group)
        {
            ESP_LOGE(TAG, "Failed to create Wi-Fi event group");
            return;
        }
    }

    esp_err_t nvs_ret = nvs_flash_init();
    if (nvs_ret == ESP_ERR_NVS_NO_FREE_PAGES || nvs_ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    app_network_init_netif();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &app_network_wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &app_network_wifi_event_handler, NULL));

    wifi_config_t wifi_config = {0};
    strlcpy((char *)wifi_config.sta.ssid, APP_WIFI_SSID, sizeof(wifi_config.sta.ssid));
    strlcpy((char *)wifi_config.sta.password, APP_WIFI_PASS, sizeof(wifi_config.sta.password));
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    s_wifi_started = true;
    ESP_LOGI(TAG, "Wi-Fi connecting to %s...", APP_WIFI_SSID);
}

bool app_network_wait_for_wifi(TickType_t timeout_ticks)
{
    app_network_start_wifi();
    if (!s_wifi_event_group)
    {
        ESP_LOGE(TAG, "Wi-Fi event group not ready");
        return false;
    }
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT, pdFALSE, pdTRUE, timeout_ticks);
    return (bits & WIFI_CONNECTED_BIT) != 0;
}
```

## main/app_network.h

```c
#ifndef APP_NETWORK_H
#define APP_NETWORK_H

#include <stdbool.h>
#include "freertos/FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

bool app_network_wait_for_wifi(TickType_t timeout_ticks);

#ifdef __cplusplus
}
#endif

#endif
```

## main/login_app.c

```c
#include "login_app.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_err.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "lvgl.h"

#include "app_network.h"
#include "user_config.h"

static const char *TAG = "login_app";

static lv_obj_t *s_login_screen;
static lv_obj_t *s_account_ta;
static lv_obj_t *s_password_ta;
static lv_obj_t *s_digit_roller;
static lv_obj_t *s_ok_btn;
static lv_obj_t *s_del_btn;
static lv_obj_t *s_signin_btn;
static lv_obj_t *s_status_label;
static bool s_request_inflight;
static login_app_result_cb s_result_cb;

typedef enum
{
    INPUT_TARGET_ACCOUNT = 0,
    INPUT_TARGET_PASSWORD,
} input_target_t;

static input_target_t s_active_target;

static char s_account_buf[64];
static char s_password_buf[64];

static int s_current_digit;

static void login_app_update_status(const char *text)
{
    if (s_status_label && text)
    {
        lv_label_set_text(s_status_label, text);
    }
}

static void login_app_set_loading(bool loading)
{
    s_request_inflight = loading;
    if (!s_signin_btn)
    {
        return;
    }
    if (loading)
    {
        lv_obj_add_state(s_signin_btn, LV_STATE_DISABLED);
    }
    else
    {
        lv_obj_clear_state(s_signin_btn, LV_STATE_DISABLED);
    }
}

static void login_app_textarea_select_event(lv_event_t *e)
{
    lv_obj_t *ta = lv_event_get_target(e);

    if (ta == s_account_ta)
    {
        s_active_target = INPUT_TARGET_ACCOUNT;
        lv_obj_add_state(s_account_ta, LV_STATE_FOCUSED);
        lv_obj_clear_state(s_password_ta, LV_STATE_FOCUSED);
    }
    else if (ta == s_password_ta)
    {
        s_active_target = INPUT_TARGET_PASSWORD;
        lv_obj_add_state(s_password_ta, LV_STATE_FOCUSED);
        lv_obj_clear_state(s_account_ta, LV_STATE_FOCUSED);
    }
}

static void login_app_roller_event(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED)
    {
        return;
    }

    char buf[4];
    lv_roller_get_selected_str(lv_event_get_target(e), buf, sizeof(buf));
    s_current_digit = atoi(buf);
}

static void login_app_roller_snap_event(lv_event_t *e)
{

    if (lv_event_get_code(e) != LV_EVENT_SCROLL_END)
    {
        return;
    }

    lv_obj_t *roller = lv_event_get_target(e);

    uint16_t selected = lv_roller_get_selected(roller);

    lv_roller_set_selected(roller, selected, LV_ANIM_OFF);
}

static void login_app_ok_event(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
    {
        return;
    }

    char digit_char = '0' + s_current_digit;

    if (s_active_target == INPUT_TARGET_ACCOUNT)
    {
        size_t len = strlen(s_account_buf);
        s_account_buf[len] = digit_char;
        s_account_buf[len + 1] = '\0';
        lv_textarea_set_text(s_account_ta, s_account_buf);
    }
    else
    {
        size_t len = strlen(s_password_buf);
        s_password_buf[len] = digit_char;
        s_password_buf[len + 1] = '\0';
        lv_textarea_set_text(s_password_ta, s_password_buf);
    }
}

static void login_app_del_event(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
    {
        return;
    }

    char *buf = (s_active_target == INPUT_TARGET_ACCOUNT)
                    ? s_account_buf
                    : s_password_buf;

    lv_obj_t *ta = (s_active_target == INPUT_TARGET_ACCOUNT)
                       ? s_account_ta
                       : s_password_ta;

    size_t len = strlen(buf);
    if (len == 0)
    {
        return;
    }

    buf[len - 1] = '\0';
    lv_textarea_set_text(ta, buf);
}

static esp_err_t login_app_do_auth(const char *username,
                                   const char *password,
                                   int *out_status)
{
    if (!username || !password || !out_status)
    {
        return ESP_ERR_INVALID_ARG;
    }

    *out_status = 0;

    if (!app_network_wait_for_wifi(pdMS_TO_TICKS(10000)))
    {
        ESP_LOGE(TAG, "Wi-Fi not ready");
        return ESP_ERR_TIMEOUT;
    }

    char payload[128];
    snprintf(payload, sizeof(payload),
             "{\"username\":\"%s\",\"password\":\"%s\"}",
             username, password);

    esp_http_client_config_t config = {
        .host = APP_SERVER_HOST,
        .port = APP_SERVER_PORT,
        .path = APP_API_LOGIN_PATH,
        .timeout_ms = 8000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client)
    {
        return ESP_FAIL;
    }

    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, payload, strlen(payload));

    esp_err_t err = esp_http_client_perform(client);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "HTTP perform failed: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return err;
    }

    int status = esp_http_client_get_status_code(client);
    *out_status = status;
    ESP_LOGI(TAG, "HTTP status = %d", status);

    esp_http_client_cleanup(client);
    return ESP_OK;
}

static void login_app_handle_submit(lv_event_t *e)
{
    (void)e;

    if (s_request_inflight)
    {
        return;
    }

    const char *account = s_account_buf;
    const char *password = s_password_buf;

    if (!account || account[0] == '\0' || !password || password[0] == '\0')
    {
        login_app_update_status("Account or password empty");
        return;
    }

    login_app_set_loading(true);
    login_app_update_status("Signing in...");

    int http_status = 0;
    esp_err_t err = login_app_do_auth(account, password, &http_status);

    login_app_set_loading(false);

    if (err != ESP_OK)
    {
        login_app_update_status("Network request failed");
        if (s_result_cb)
        {
            s_result_cb(false, NULL, NULL);
        }
        return;
    }

    if (http_status == 200)
    {
        login_app_update_status("Login successful");
        if (s_result_cb)
        {
            s_result_cb(true, NULL, account);
        }
    }
    else if (http_status == 401)
    {
        login_app_update_status("Invalid account or password");
        s_account_buf[0] = '\0';
        s_password_buf[0] = '\0';
        if (s_account_ta)
        {
            lv_textarea_set_text(s_account_ta, "");
        }
        if (s_password_ta)
        {
            lv_textarea_set_text(s_password_ta, "");
        }
        if (s_result_cb)
        {
            s_result_cb(false, NULL, NULL);
        }
    }
    else
    {
        login_app_update_status("Login failed");
        if (s_result_cb)
        {
            s_result_cb(false, NULL, NULL);
        }
    }

}

static void login_app_build_ui(void)
{
    s_login_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_login_screen, lv_color_hex(0xf6f8fb), LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_login_screen, 4, LV_PART_MAIN);

    lv_obj_t *row = lv_obj_create(s_login_screen);
    lv_obj_set_size(row, lv_pct(100), lv_pct(100));
    lv_obj_align(row, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_pad_all(row, 4, LV_PART_MAIN);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row,
                          LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    lv_obj_t *left = lv_obj_create(row);
    lv_obj_set_flex_grow(left, 4);
    lv_obj_set_flex_flow(left, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_bg_opa(left, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(left, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(left, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(left, LV_OPA_100, LV_PART_MAIN);
    lv_obj_set_style_radius(left, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_all(left, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_gap(left, 10, LV_PART_MAIN);

    s_account_ta = lv_textarea_create(left);
    lv_textarea_set_one_line(s_account_ta, true);
    lv_textarea_set_placeholder_text(s_account_ta, "Account");
    lv_obj_set_height(s_account_ta, 48);
    lv_obj_set_width(s_account_ta, lv_pct(92));
    lv_obj_add_event_cb(s_account_ta, login_app_textarea_select_event, LV_EVENT_CLICKED, NULL);

    s_password_ta = lv_textarea_create(left);
    lv_textarea_set_one_line(s_password_ta, true);
    lv_textarea_set_password_mode(s_password_ta, true);
    lv_textarea_set_placeholder_text(s_password_ta, "Password");
    lv_obj_set_height(s_password_ta, 48);
    lv_obj_set_width(s_password_ta, lv_pct(92));
    lv_obj_add_event_cb(s_password_ta, login_app_textarea_select_event, LV_EVENT_CLICKED, NULL);

    s_active_target = INPUT_TARGET_ACCOUNT;
    lv_obj_add_state(s_account_ta, LV_STATE_FOCUSED);
    lv_obj_clear_state(s_password_ta, LV_STATE_FOCUSED);

    lv_obj_t *mid = lv_obj_create(row);
    lv_obj_set_flex_grow(mid, 2);
    lv_obj_set_style_bg_opa(mid, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(mid, 0, LV_PART_MAIN);

    s_digit_roller = lv_roller_create(mid);
    lv_obj_set_width(s_digit_roller, lv_pct(100));
    lv_obj_set_height(s_digit_roller, lv_pct(80));
    lv_obj_align(s_digit_roller, LV_ALIGN_CENTER, 0, 0);

    lv_obj_set_style_bg_color(s_digit_roller, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_digit_roller, LV_OPA_100, LV_PART_MAIN);
    lv_obj_set_style_radius(s_digit_roller, 10, LV_PART_MAIN);

    lv_obj_set_style_text_font(s_digit_roller, &lv_font_montserrat_32, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_digit_roller,
                                lv_palette_main(LV_PALETTE_GREY),
                                LV_PART_MAIN);

    lv_obj_set_style_text_font(s_digit_roller, &lv_font_montserrat_48, LV_PART_SELECTED);
    lv_obj_set_style_text_color(s_digit_roller,
                                lv_color_black(),
                                LV_PART_SELECTED);

    lv_obj_set_style_bg_color(s_digit_roller,
                              lv_palette_lighten(LV_PALETTE_BLUE, 4),
                              LV_PART_SELECTED);
    lv_obj_set_style_bg_opa(s_digit_roller,
                            LV_OPA_30,
                            LV_PART_SELECTED);
    lv_obj_set_style_radius(s_digit_roller,
                            6,
                            LV_PART_SELECTED);

    lv_obj_set_style_pad_top(s_digit_roller, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(s_digit_roller, 0, LV_PART_MAIN);

    lv_roller_set_options(s_digit_roller,
                          "0\n1\n2\n3\n4\n5\n6\n7\n8\n9",
                          LV_ROLLER_MODE_INFINITE);
    lv_roller_set_visible_row_count(s_digit_roller, 3);
    lv_roller_set_selected(s_digit_roller, 0, LV_ANIM_OFF);

    lv_obj_clear_flag(s_digit_roller, LV_OBJ_FLAG_SCROLL_ELASTIC);

    lv_obj_add_event_cb(s_digit_roller,
                        login_app_roller_event,
                        LV_EVENT_VALUE_CHANGED,
                        NULL);
    lv_obj_add_event_cb(s_digit_roller,
                        login_app_roller_snap_event,
                        LV_EVENT_SCROLL_END,
                        NULL);

    s_current_digit = 0;

    lv_obj_t *right = lv_obj_create(row);
    lv_obj_set_flex_grow(right, 2);
    lv_obj_set_height(right, lv_pct(100));
    lv_obj_set_flex_flow(right, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(right, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(right, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(right, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(right, 0, LV_PART_MAIN);
    lv_obj_set_flex_align(right,
                          LV_FLEX_ALIGN_SPACE_EVENLY,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    s_ok_btn = lv_btn_create(right);
    lv_obj_add_event_cb(s_ok_btn, login_app_ok_event, LV_EVENT_CLICKED, NULL);
    lv_label_create(s_ok_btn);
    lv_label_set_text(lv_obj_get_child(s_ok_btn, -1), "OK");

    s_del_btn = lv_btn_create(right);
    lv_obj_add_event_cb(s_del_btn, login_app_del_event, LV_EVENT_CLICKED, NULL);
    lv_label_create(s_del_btn);
    lv_label_set_text(lv_obj_get_child(s_del_btn, -1), "DEL");

    s_signin_btn = lv_btn_create(right);
    lv_obj_add_event_cb(s_signin_btn, login_app_handle_submit, LV_EVENT_CLICKED, NULL);
    lv_label_create(s_signin_btn);
    lv_label_set_text(lv_obj_get_child(s_signin_btn, -1), "Sign In");
    const int BTN_H = 44;
    lv_obj_set_width(s_ok_btn, lv_pct(90));
    lv_obj_set_width(s_del_btn, lv_pct(90));
    lv_obj_set_width(s_signin_btn, lv_pct(90));
    lv_obj_set_height(s_ok_btn, BTN_H);
    lv_obj_set_height(s_del_btn, BTN_H);
    lv_obj_set_height(s_signin_btn, 52);
    lv_obj_set_style_bg_color(s_signin_btn, lv_palette_main(LV_PALETTE_BLUE), LV_PART_MAIN);
    lv_obj_set_style_text_color(s_signin_btn, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_radius(s_signin_btn, 10, LV_PART_MAIN);
    lv_obj_set_style_radius(s_ok_btn, 10, LV_PART_MAIN);
    lv_obj_set_style_radius(s_del_btn, 10, LV_PART_MAIN);

    s_account_buf[0] = '\0';
    s_password_buf[0] = '\0';
    lv_textarea_set_text(s_account_ta, "");
    lv_textarea_set_text(s_password_ta, "");

    s_status_label = lv_label_create(s_login_screen);
    lv_label_set_text(s_status_label, "Enter account and password");
    lv_obj_set_style_text_color(s_status_label, lv_palette_main(LV_PALETTE_GREY), LV_PART_MAIN);
    lv_obj_set_width(s_status_label, lv_pct(100));
    lv_label_set_long_mode(s_status_label, LV_LABEL_LONG_WRAP);
    lv_obj_align(s_status_label, LV_ALIGN_BOTTOM_MID, 0, -4);
}

void login_app_show(void)
{
    if (!s_login_screen)
    {
        login_app_build_ui();
    }
    lv_scr_load(s_login_screen);
    login_app_update_status("Enter account and password");
}

void login_app_destroy(void)
{
    if (s_login_screen)
    {
        lv_obj_del_async(s_login_screen);
        s_login_screen = NULL;
        s_account_ta = NULL;
        s_password_ta = NULL;
        s_digit_roller = NULL;
        s_ok_btn = NULL;
        s_del_btn = NULL;
        s_signin_btn = NULL;
        s_status_label = NULL;
    }
    s_request_inflight = false;
    s_account_buf[0] = '\0';
    s_password_buf[0] = '\0';
}

void login_app_set_result_cb(login_app_result_cb cb)
{
    s_result_cb = cb;
}
```

## main/login_app.h

```c
#ifndef LOGIN_APP_H
#define LOGIN_APP_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*login_app_result_cb)(bool success, const char *token, const char *user);

void login_app_show(void);

void login_app_destroy(void);

void login_app_set_result_cb(login_app_result_cb cb);

#ifdef __cplusplus
}
#endif

#endif
```

## main/main.cpp

```cpp
#include <stdio.h>

extern "C" {
#include "app_flow.h"
}

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "driver/spi_master.h"
#include "esp_timer.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "driver/gpio.h"

#include "lvgl.h"
#include "lv_demos.h"

#include "user_config.h"
#include "esp_lcd_axs15231b.h"
#include "i2c_bsp.h"
#include "button_bsp.h"
#include "esp_io_expander_tca9554.h"
#include "lcd_bl_pwm_bsp.h"

static const char *TAG = "example";

static SemaphoreHandle_t lvgl_mux = NULL;

static uint16_t *lvgl_dma_buf = NULL;

static SemaphoreHandle_t lvgl_flush_semap;

#if (Rotated == USER_DISP_ROT_90)
uint16_t* rotat_ptr = NULL;
#endif
static esp_io_expander_handle_t io_expander = NULL;
static bool is_vbatpowerflag = false;

#define LCD_BIT_PER_PIXEL (16)

static void example_lvgl_touch_cb(lv_indev_drv_t *drv, lv_indev_data_t *data);

static bool example_lvgl_lock(int timeout_ms);

static void example_lvgl_unlock(void);

static void example_lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map);

void example_lvgl_port_task(void *arg);

static void example_backlight_loop_task(void *arg);
static void power_Test(void *arg);
static void tca9554_init(void);
static void example_button_pwr_task(void *arg);

static const axs15231b_lcd_init_cmd_t lcd_init_cmds[] =
{
    {0x11, (uint8_t []){0x00}, 0, 100},
    {0x29, (uint8_t []){0x00}, 0, 100},
};

static bool example_notify_lvgl_flush_ready(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_io_event_data_t *edata, void *user_ctx)
{
    BaseType_t TaskWoken;

    xSemaphoreGiveFromISR(lvgl_flush_semap, &TaskWoken);
    return false;
}

static void example_increase_lvgl_tick(void *arg)
{

    lv_tick_inc(EXAMPLE_LVGL_TICK_PERIOD_MS);
}

static void power_Test(void *arg)
{
    if (gpio_get_level(GPIO_NUM_16))
    {
        is_vbatpowerflag = true;
    }
    vTaskDelete(NULL);
}

static void tca9554_init(void)
{
    i2c_master_bus_handle_t tca9554_i2c_bus_ = NULL;
    ESP_ERROR_CHECK(i2c_master_get_bus_handle(0, &tca9554_i2c_bus_));
    esp_io_expander_new_i2c_tca9554(tca9554_i2c_bus_, ESP_IO_EXPANDER_I2C_TCA9554_ADDRESS_000, &io_expander);
    esp_io_expander_set_dir(io_expander, IO_EXPANDER_PIN_NUM_6, IO_EXPANDER_OUTPUT);
    esp_io_expander_set_level(io_expander, IO_EXPANDER_PIN_NUM_6, 1);
}

static void example_button_pwr_task(void* arg)
{
    for (;;)
    {
        EventBits_t even = xEventGroupWaitBits(pwr_groups, set_bit_all, pdTRUE, pdFALSE, pdMS_TO_TICKS(2 * 1000));
        if (get_bit_button(even, 0))
        {

        }
        else if (get_bit_button(even, 1))
        {
            if (is_vbatpowerflag)
            {
                is_vbatpowerflag = false;
                esp_io_expander_set_level(io_expander, IO_EXPANDER_PIN_NUM_6, 0);
            }
        }
        else if (get_bit_button(even, 2))
        {
            if (!is_vbatpowerflag)
            {
                is_vbatpowerflag = true;
            }
        }
    }
}

extern "C" void app_main(void)
{

    lcd_bl_pwm_bsp_init(LCD_PWM_MODE_255);

    #if (Rotated == USER_DISP_ROT_90)
    rotat_ptr = (uint16_t*)heap_caps_malloc(EXAMPLE_LCD_H_RES * EXAMPLE_LCD_V_RES * sizeof(uint16_t), MALLOC_CAP_SPIRAM);
    assert(rotat_ptr);
    #endif

    lvgl_flush_semap = xSemaphoreCreateBinary();

    i2c_master_Init();
    tca9554_init();
    button_Init();
    xTaskCreatePinnedToCore(example_button_pwr_task, "example_button_pwr_task", 4 * 1024, NULL, 2, NULL, 1);
    xTaskCreatePinnedToCore(power_Test, "power_Test", 4 * 1024, NULL, 3, NULL, 1);

    static lv_disp_draw_buf_t disp_buf;
    static lv_disp_drv_t disp_drv;

    ESP_LOGI(TAG, "Initialize LCD RESET GPIO");

    gpio_config_t gpio_conf = {};
        gpio_conf.intr_type = GPIO_INTR_DISABLE;
        gpio_conf.mode = GPIO_MODE_OUTPUT;
        gpio_conf.pin_bit_mask = ((uint64_t)0X01<<EXAMPLE_PIN_NUM_LCD_RST);
        gpio_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
        gpio_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    ESP_ERROR_CHECK_WITHOUT_ABORT(gpio_config(&gpio_conf));

    ESP_LOGI(TAG, "Initialize QSPI bus");

    spi_bus_config_t buscfg = {};
        buscfg.data0_io_num = EXAMPLE_PIN_NUM_LCD_DATA0;
        buscfg.data1_io_num = EXAMPLE_PIN_NUM_LCD_DATA1;
        buscfg.sclk_io_num = EXAMPLE_PIN_NUM_LCD_PCLK;
        buscfg.data2_io_num = EXAMPLE_PIN_NUM_LCD_DATA2;
        buscfg.data3_io_num = EXAMPLE_PIN_NUM_LCD_DATA3;
        buscfg.max_transfer_sz = LVGL_DMA_BUFF_LEN;
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO));

    ESP_LOGI(TAG, "Install panel IO");

    esp_lcd_panel_io_handle_t panel_io = NULL;
    esp_lcd_panel_handle_t panel = NULL;

    esp_lcd_panel_io_spi_config_t io_config = {};
        io_config.cs_gpio_num = EXAMPLE_PIN_NUM_LCD_CS;
        io_config.dc_gpio_num = -1;
        io_config.spi_mode = 3;
        io_config.pclk_hz = 40 * 1000 * 1000;
        io_config.trans_queue_depth = 10;
        io_config.on_color_trans_done = example_notify_lvgl_flush_ready;
        io_config.lcd_cmd_bits = 32;
        io_config.lcd_param_bits = 8;
        io_config.flags.quad_mode = true;

    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(LCD_HOST, &io_config, &panel_io));

    axs15231b_vendor_config_t vendor_config = {};
        vendor_config.flags.use_qspi_interface = 1;
        vendor_config.init_cmds = lcd_init_cmds;
        vendor_config.init_cmds_size = sizeof(lcd_init_cmds) / sizeof(lcd_init_cmds[0]);

    esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = -1;
        panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
        panel_config.bits_per_pixel = LCD_BIT_PER_PIXEL;
        panel_config.vendor_config = &vendor_config;

    ESP_LOGI(TAG, "Install panel driver");

    ESP_ERROR_CHECK(esp_lcd_new_panel_axs15231b(panel_io, &panel_config, &panel));

    ESP_ERROR_CHECK(gpio_set_level(EXAMPLE_PIN_NUM_LCD_RST, 1));
    vTaskDelay(pdMS_TO_TICKS(30));
    ESP_ERROR_CHECK(gpio_set_level(EXAMPLE_PIN_NUM_LCD_RST, 0));
    vTaskDelay(pdMS_TO_TICKS(250));
    ESP_ERROR_CHECK(gpio_set_level(EXAMPLE_PIN_NUM_LCD_RST, 1));
    vTaskDelay(pdMS_TO_TICKS(30));

    ESP_ERROR_CHECK(esp_lcd_panel_init(panel));

    lv_init();

    lvgl_dma_buf = (uint16_t *)heap_caps_malloc(LVGL_DMA_BUFF_LEN, MALLOC_CAP_DMA);
    assert(lvgl_dma_buf);

    lv_color_t *buffer_1 = (lv_color_t *)heap_caps_malloc(LVGL_SPIRAM_BUFF_LEN, MALLOC_CAP_SPIRAM);
    lv_color_t *buffer_2 = (lv_color_t *)heap_caps_malloc(LVGL_SPIRAM_BUFF_LEN, MALLOC_CAP_SPIRAM);
    assert(buffer_1);
    assert(buffer_2);

    lv_disp_draw_buf_init(&disp_buf, buffer_1, buffer_2, EXAMPLE_LCD_H_RES * EXAMPLE_LCD_V_RES);

    ESP_LOGI(TAG, "Register display driver to LVGL");

    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = EXAMPLE_LCD_H_RES;
    disp_drv.ver_res = EXAMPLE_LCD_V_RES;
    disp_drv.flush_cb = example_lvgl_flush_cb;
    disp_drv.draw_buf = &disp_buf;
    disp_drv.full_refresh = 1;
    disp_drv.user_data = panel;

    lv_disp_drv_register(&disp_drv);

    ESP_LOGI(TAG, "Install LVGL tick timer");

    esp_timer_create_args_t lvgl_tick_timer_args = {};
        lvgl_tick_timer_args.callback = &example_increase_lvgl_tick;
        lvgl_tick_timer_args.name = "lvgl_tick";
    esp_timer_handle_t lvgl_tick_timer = NULL;
    ESP_ERROR_CHECK(esp_timer_create(&lvgl_tick_timer_args, &lvgl_tick_timer));

    ESP_ERROR_CHECK(esp_timer_start_periodic(lvgl_tick_timer, EXAMPLE_LVGL_TICK_PERIOD_MS * 1000));

    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = example_lvgl_touch_cb;
    lv_indev_drv_register(&indev_drv);

    lvgl_mux = xSemaphoreCreateMutex();
    assert(lvgl_mux);

    xTaskCreatePinnedToCore(example_lvgl_port_task, "LVGL", 4000, NULL, 4, NULL, 0);

    xTaskCreatePinnedToCore(example_backlight_loop_task, "example_backlight_loop_task", 4 * 1024, NULL, 2, NULL, 0);

    if (example_lvgl_lock(-1))
    {
        app_flow_start();
        example_lvgl_unlock();
    }

}

static void example_lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map)
{
    #if (Rotated == USER_DISP_ROT_90)

    uint32_t index = 0;
    uint16_t *data_ptr = (uint16_t *)color_map;
    for (uint16_t j = 0; j < EXAMPLE_LCD_H_RES; j++)
    {
        for (uint16_t i = 0; i < EXAMPLE_LCD_V_RES; i++)
        {

            rotat_ptr[index++] = data_ptr[EXAMPLE_LCD_H_RES * (EXAMPLE_LCD_V_RES - i - 1) + j];
        }
    }
    #endif

    esp_lcd_panel_handle_t panel_handle = (esp_lcd_panel_handle_t) drv->user_data;

    const int flush_coun = (LVGL_SPIRAM_BUFF_LEN / LVGL_DMA_BUFF_LEN);
    const int offgap = (LCD_NOROT_VRES / flush_coun);
    const int dmalen = (LVGL_DMA_BUFF_LEN / 2);

    int offsetx1 = 0;
    int offsety1 = 0;
    int offsetx2 = LCD_NOROT_HRES;
    int offsety2 = offgap;

    #if (Rotated == USER_DISP_ROT_90)
    uint16_t *map = (uint16_t *)rotat_ptr;
    #else
    uint16_t *map = (uint16_t *)color_map;
    #endif

    xSemaphoreGive(lvgl_flush_semap);

    for(int i = 0; i < flush_coun; i++)
    {

        xSemaphoreTake(lvgl_flush_semap, portMAX_DELAY);

        memcpy(lvgl_dma_buf, map, LVGL_DMA_BUFF_LEN);

        esp_lcd_panel_draw_bitmap(panel_handle, offsetx1, offsety1, offsetx2, offsety2, lvgl_dma_buf);

        offsety1 += offgap;
        offsety2 += offgap;
        map += dmalen;
    }

    xSemaphoreTake(lvgl_flush_semap, portMAX_DELAY);

    lv_disp_flush_ready(drv);
}

static bool example_lvgl_lock(int timeout_ms)
{

    const TickType_t timeout_ticks = (timeout_ms == -1) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    return xSemaphoreTake(lvgl_mux, timeout_ticks) == pdTRUE;
}

static void example_lvgl_unlock(void)
{
    assert(lvgl_mux && "bsp_display_start must be called first");
    xSemaphoreGive(lvgl_mux);
}

void example_lvgl_port_task(void *arg)
{

    uint32_t task_delay_ms = EXAMPLE_LVGL_TASK_MAX_DELAY_MS;

    for(;;)
    {

        if (example_lvgl_lock(-1))
        {

            task_delay_ms = lv_timer_handler();

            example_lvgl_unlock();
        }

        if (task_delay_ms > EXAMPLE_LVGL_TASK_MAX_DELAY_MS)
        {
            task_delay_ms = EXAMPLE_LVGL_TASK_MAX_DELAY_MS;
        }
        else if (task_delay_ms < EXAMPLE_LVGL_TASK_MIN_DELAY_MS)
        {
            task_delay_ms = EXAMPLE_LVGL_TASK_MIN_DELAY_MS;
        }

        vTaskDelay(pdMS_TO_TICKS(task_delay_ms));
    }
}

static void example_lvgl_touch_cb(lv_indev_drv_t *drv, lv_indev_data_t *data)
{

    uint8_t read_touchpad_cmd[11] = {0xb5, 0xab, 0xa5, 0x5a, 0x0, 0x0, 0x0, 0x0e, 0x0, 0x0, 0x0};
    uint8_t buff[32] = {0};

    memset(buff, 0, 32);

    esp_err_t touch_err = i2c_master_write_read_dev(disp_touch_dev_handle, read_touchpad_cmd, 11, buff, 32);
    if (touch_err != ESP_OK)
    {

        data->state = LV_INDEV_STATE_REL;
        return;
    }

    uint16_t pointX;
    uint16_t pointY;
    pointX = (((uint16_t)buff[2] & 0x0f) << 8) | (uint16_t)buff[3];
    pointY = (((uint16_t)buff[4] & 0x0f) << 8) | (uint16_t)buff[5];

    if (buff[1] > 0 && buff[1] < 5)
    {
        data->state = LV_INDEV_STATE_PR;

        #if (Rotated == USER_DISP_ROT_NONO)

        if (pointX > EXAMPLE_LCD_V_RES) pointX = EXAMPLE_LCD_V_RES;
        if (pointY > EXAMPLE_LCD_H_RES) pointY = EXAMPLE_LCD_H_RES;
        data->point.x = pointY;
        data->point.y = (EXAMPLE_LCD_V_RES - pointX);
        #else

        if (pointX > EXAMPLE_LCD_H_RES) pointX = EXAMPLE_LCD_H_RES;
        if (pointY > EXAMPLE_LCD_V_RES) pointY = EXAMPLE_LCD_V_RES;
        data->point.x = (EXAMPLE_LCD_H_RES - pointX);
        data->point.y = (EXAMPLE_LCD_V_RES - pointY);
        #endif
    }
    else
    {
        data->state = LV_INDEV_STATE_REL;
    }
}

static void example_backlight_loop_task(void *arg)
{
    for(;;)
    {
        #if (Backlight_Testing == true)

        vTaskDelay(pdMS_TO_TICKS(1500));
        setUpduty(LCD_PWM_MODE_255);
        vTaskDelay(pdMS_TO_TICKS(1500));
        setUpduty(LCD_PWM_MODE_175);
        vTaskDelay(pdMS_TO_TICKS(1500));
        setUpduty(LCD_PWM_MODE_125);
        vTaskDelay(pdMS_TO_TICKS(1500));
        setUpduty(LCD_PWM_MODE_0);
        #else

        vTaskDelay(pdMS_TO_TICKS(2000));
        #endif
    }
}
```

## main/quiz_app.c

```c
#include "quiz_app.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "lvgl.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "app_network.h"
#include "user_config.h"

#define UI_FONT_LARGE   lv_theme_get_font_large(NULL)
#define UI_FONT_NORMAL  lv_theme_get_font_normal(NULL)
#define UI_FONT_SMALL   lv_theme_get_font_small(NULL)

#define OPTION_TEXT_NORMAL_COLOR   lv_color_hex(0x333333)
#define OPTION_TEXT_ACTIVE_COLOR   lv_palette_main(LV_PALETTE_BLUE)

#define QUIZ_MAX_QUESTIONS     12
#define QUIZ_OPTION_COUNT      4
#define QUIZ_TEXT_LEN          192
#define QUIZ_OPTION_LEN        96

typedef struct
{
    char id[24];
    int correct;
    int your;
} quiz_wrong_item_t;

typedef struct
{
    char id[24];
    char stem[QUIZ_TEXT_LEN];
    char options[QUIZ_OPTION_COUNT][QUIZ_OPTION_LEN];
    uint8_t correct_index;
} quiz_question_t;

typedef struct
{
    quiz_question_t questions[QUIZ_MAX_QUESTIONS];
    uint8_t answers[QUIZ_MAX_QUESTIONS];
    uint8_t question_count;
    uint8_t current_question;
    bool questions_ready;
    bool test_finished;
    int server_score;
    int server_total;
    uint8_t server_wrong_count;
    quiz_wrong_item_t server_wrong[QUIZ_MAX_QUESTIONS];
} quiz_state_t;

static const char *TAG = "quiz_app";

static quiz_state_t s_state;
static char s_user_id[16];
static char s_quiz_id[40];

static lv_obj_t *s_home_screen;
static lv_obj_t *s_result_screen;
static lv_obj_t *s_test_screen;

static lv_obj_t *s_scroll;

static lv_obj_t *s_result_scroll;
static lv_obj_t *s_progress_label;
static lv_obj_t *s_question_label;
static lv_obj_t *s_option_labels[QUIZ_OPTION_COUNT];

static lv_obj_t *s_bottom_bar;
static lv_obj_t *s_option_btns[QUIZ_OPTION_COUNT];
static lv_obj_t *s_submit_btn;

static lv_obj_t *s_toast_label;
static lv_timer_t *s_toast_timer;

static void quiz_handle_download(lv_event_t *e);
static void quiz_handle_start_test(lv_event_t *e);
static void quiz_handle_view_results(lv_event_t *e);
static void quiz_handle_option(lv_event_t *e);
static void quiz_handle_submit(lv_event_t *e);
static void quiz_hide_toast_cb(lv_timer_t *t);
static void quiz_back_to_home(lv_event_t *e);
static esp_err_t quiz_http_get_questions(char **out_json);
static esp_err_t quiz_http_post_results(const char *payload);
static void quiz_load_question(uint8_t index);
static void quiz_build_result_screen(void);
static void quiz_show_results_screen(void);
static void quiz_finish_and_upload(void);
static void quiz_reset_server_result(void);
static const quiz_question_t *quiz_find_question_by_id(const char *id);

static void quiz_show_toast(const char *text, uint32_t duration_ms)
{
    if (!text || text[0] == '\0')
    {
        return;
    }

    if (!s_toast_label)
    {
        s_toast_label = lv_label_create(lv_layer_top());
        lv_obj_set_style_bg_opa(s_toast_label, LV_OPA_70, LV_PART_MAIN);
        lv_obj_set_style_bg_color(s_toast_label, lv_color_black(), LV_PART_MAIN);
        lv_obj_set_style_text_color(s_toast_label, lv_color_white(), LV_PART_MAIN);
        lv_obj_set_style_text_font(s_toast_label, UI_FONT_NORMAL, LV_PART_MAIN);
        lv_obj_set_style_pad_all(s_toast_label, 10, LV_PART_MAIN);
        lv_obj_set_style_radius(s_toast_label, 10, LV_PART_MAIN);
    }

    lv_label_set_text(s_toast_label, text);
    lv_obj_center(s_toast_label);
    lv_obj_clear_flag(s_toast_label, LV_OBJ_FLAG_HIDDEN);

    if (!s_toast_timer)
    {
        s_toast_timer = lv_timer_create(quiz_hide_toast_cb, duration_ms, NULL);
    }
    else
    {
        lv_timer_set_period(s_toast_timer, duration_ms);
        lv_timer_reset(s_toast_timer);
    }
    lv_timer_set_repeat_count(s_toast_timer, 1);
}

static esp_err_t quiz_http_get_questions(char **out_json)
{
    if (!out_json)
    {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_user_id[0] == '\0')
    {
        ESP_LOGE(TAG, "User ID missing, cannot download questions");
        return ESP_ERR_INVALID_STATE;
    }

    if (!app_network_wait_for_wifi(pdMS_TO_TICKS(10000)))
    {
        return ESP_ERR_TIMEOUT;
    }

    *out_json = NULL;

    char url[128];
    snprintf(url, sizeof(url),
             "http://%s:%d/questions?user_id=%s",
             APP_SERVER_HOST,
             APP_SERVER_PORT,
             s_user_id);

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = 8000,
        .disable_auto_redirect = false,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client)
    {
        ESP_LOGE(TAG, "Failed to init client");
        return ESP_FAIL;
    }

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "HTTP open failed: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return err;
    }

    int content_length = esp_http_client_fetch_headers(client);
    int status = esp_http_client_get_status_code(client);
    if (status != 200)
    {
        ESP_LOGE(TAG, "Questions request failed, status=%d", status);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_FAIL;
    }

    if (content_length <= 0)
    {
        ESP_LOGE(TAG, "Invalid content length: %d", content_length);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "HTTP Content-Length = %d", content_length);

    char *buffer = malloc(content_length + 1);
    if (!buffer)
    {
        ESP_LOGE(TAG, "malloc failed");
        esp_http_client_cleanup(client);
        return ESP_ERR_NO_MEM;
    }

    int total_read = 0;
    while (total_read < content_length)
    {
        int read_len = esp_http_client_read(client, buffer + total_read,
                                            content_length - total_read);
        if (read_len < 0)
        {
            ESP_LOGE(TAG, "Read error");
            free(buffer);
            esp_http_client_cleanup(client);
            return ESP_FAIL;
        }
        if (read_len == 0)
        {
            break;
        }
        total_read += read_len;
    }

    buffer[total_read] = '\0';
    ESP_LOGW(TAG, "HTTP BODY RECEIVED: %s", buffer);

    *out_json = buffer;

    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    return ESP_OK;
}

static esp_err_t quiz_http_post_results(const char *payload)
{
    if (!payload)
    {
        return ESP_ERR_INVALID_ARG;
    }

    if (!app_network_wait_for_wifi(pdMS_TO_TICKS(10000)))
    {
        return ESP_ERR_TIMEOUT;
    }

    esp_http_client_config_t config = {
        .host = APP_SERVER_HOST,
        .port = APP_SERVER_PORT,
        .path = APP_API_SUBMIT_PATH,
        .transport_type = HTTP_TRANSPORT_OVER_TCP,
        .timeout_ms = 12000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client)
    {
        return ESP_FAIL;
    }

    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, payload, strlen(payload));

    esp_err_t err = esp_http_client_perform(client);
    if (err != ESP_OK)
    {
        esp_http_client_cleanup(client);
        return err;
    }

    int status = esp_http_client_get_status_code(client);
    if (status != 200)
    {
        esp_http_client_cleanup(client);
        return ESP_FAIL;
    }

    esp_http_client_cleanup(client);
    return ESP_OK;
}

static bool quiz_parse_questions(const char *json)
{
    if (!json)
    {
        return false;
    }

    cJSON *root = cJSON_Parse(json);
    if (!root)
    {
        ESP_LOGE(TAG, "Failed to parse JSON");
        return false;
    }

    s_quiz_id[0] = '\0';
    cJSON *quiz_id_js = cJSON_GetObjectItem(root, "quiz_id");
    if (cJSON_IsString(quiz_id_js))
    {
        strncpy(s_quiz_id, quiz_id_js->valuestring, sizeof(s_quiz_id) - 1);
        s_quiz_id[sizeof(s_quiz_id) - 1] = '\0';
    }

    cJSON *questions = cJSON_GetObjectItemCaseSensitive(root, "questions");
    if (!cJSON_IsArray(questions))
    {
        cJSON_Delete(root);
        ESP_LOGE(TAG, "Questions field is missing");
        return false;
    }

    memset(&s_state, 0, sizeof(s_state));
    s_state.question_count = 0;

    cJSON *item = NULL;
    cJSON_ArrayForEach(item, questions)
    {
        if (s_state.question_count >= QUIZ_MAX_QUESTIONS)
        {
            break;
        }

        cJSON *id = cJSON_GetObjectItemCaseSensitive(item, "id");
        cJSON *stem = cJSON_GetObjectItemCaseSensitive(item, "stem");
        cJSON *options = cJSON_GetObjectItemCaseSensitive(item, "options");
        cJSON *answer = cJSON_GetObjectItemCaseSensitive(item, "answer");

        if (!cJSON_IsArray(options) || cJSON_GetArraySize(options) < QUIZ_OPTION_COUNT)
        {
            ESP_LOGW(TAG, "Skip malformed question");
            continue;
        }

        quiz_question_t *q = &s_state.questions[s_state.question_count];
        strncpy(q->id, cJSON_IsString(id) ? id->valuestring : "NA", sizeof(q->id) - 1);
        strncpy(q->stem, cJSON_IsString(stem) ? stem->valuestring : "Untitled question", sizeof(q->stem) - 1);

        for (int i = 0; i < QUIZ_OPTION_COUNT; i++)
        {
            cJSON *opt = cJSON_GetArrayItem(options, i);
            strncpy(q->options[i], cJSON_IsString(opt) ? opt->valuestring : "N/A", sizeof(q->options[i]) - 1);
        }

        q->correct_index = (uint8_t)(cJSON_IsNumber(answer) ? answer->valueint : 0);
        if (q->correct_index >= QUIZ_OPTION_COUNT)
        {
            q->correct_index = 0;
        }

        s_state.question_count++;
    }

    cJSON_Delete(root);
    s_state.questions_ready = (s_state.question_count > 0);
    s_state.test_finished = false;
    s_state.current_question = 0;
    memset(s_state.answers, 0xFF, sizeof(s_state.answers));
    ESP_LOGI(TAG, "Loaded %d questions", s_state.question_count);
    return s_state.questions_ready;
}

static bool quiz_download_questions(void)
{
    char *json = NULL;
    esp_err_t err = quiz_http_get_questions(&json);
    if (err != ESP_OK || !json)
    {
        quiz_show_toast("Download failed", 1800);
        return false;
    }

    ESP_LOGW(TAG, "RAW JSON RECEIVED: %s", json);

    bool ok = quiz_parse_questions(json);
    free(json);

    if (!ok)
    {
        ESP_LOGE(TAG, "Parse JSON failed, abort UI");
        quiz_show_toast("Invalid data", 1800);
        return false;
    }

    return true;
}

static void quiz_handle_download(lv_event_t *e)
{
    LV_UNUSED(e);
    if (!quiz_download_questions())
    {
        ESP_LOGE(TAG, "Download failed, not entering question screen");
        return;
    }
}

static void quiz_handle_start_test(lv_event_t *e)
{
    LV_UNUSED(e);

    if (!s_state.questions_ready)
    {
        quiz_show_toast("Please download questions first", 2000);
        return;
    }

    s_state.current_question = 0;
    s_state.test_finished = false;
    quiz_reset_server_result();
    memset(s_state.answers, 0xFF, sizeof(s_state.answers));

    lv_scr_load(s_test_screen);

    quiz_load_question(s_state.current_question);
}

static void quiz_handle_view_results(lv_event_t *e)
{
    LV_UNUSED(e);
    if (!s_state.test_finished)
    {
        quiz_show_toast("No test taken yet", 2000);
        return;
    }

    quiz_show_results_screen();
}

static void quiz_create_home_screen(void)
{
    s_home_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_home_screen, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_color(s_home_screen, lv_color_hex(0x111111), LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_home_screen, 12, LV_PART_MAIN);

    lv_obj_t *row = lv_obj_create(s_home_screen);
    lv_obj_set_size(row, lv_pct(100), 100);
    lv_obj_align(row, LV_ALIGN_CENTER, 0, -10);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(row, LV_OPA_0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(row, 10, LV_PART_MAIN);

    static const char *btn_texts[3] = {"Download", "Start Test", "View Results"};
    lv_event_cb_t handlers[3] = {quiz_handle_download, quiz_handle_start_test, quiz_handle_view_results};
    for (int i = 0; i < 3; i++)
    {
        lv_obj_t *btn = lv_btn_create(row);
        lv_obj_set_size(btn, 120, 70);
        lv_obj_add_event_cb(btn, handlers[i], LV_EVENT_CLICKED, NULL);
        lv_obj_set_style_bg_color(btn, lv_palette_main(LV_PALETTE_BLUE), LV_PART_MAIN);
        lv_obj_set_style_radius(btn, 10, LV_PART_MAIN);
        lv_obj_set_style_border_width(btn, 1, LV_PART_MAIN);
        lv_obj_set_style_border_color(btn, lv_color_hex(0x2f6ee2), LV_PART_MAIN);

        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, btn_texts[i]);
        lv_obj_set_style_text_font(lbl, UI_FONT_NORMAL, LV_PART_MAIN);
        lv_obj_center(lbl);
    }
}

static void quiz_build_test_screen(void)
{

    s_test_screen = lv_obj_create(NULL);
    lv_obj_set_size(s_test_screen, 640, 172);
    lv_obj_clear_flag(s_test_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_test_screen, lv_color_white(), 0);

    s_scroll = lv_obj_create(s_test_screen);
    lv_obj_set_size(s_scroll, 640, 172);
    lv_obj_align(s_scroll, LV_ALIGN_TOP_MID, 0, 0);

    lv_obj_set_scroll_dir(s_scroll, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_scroll, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_clear_flag(s_scroll, LV_OBJ_FLAG_SCROLL_ELASTIC);

    lv_obj_set_flex_flow(s_scroll, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_scroll,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);

    lv_obj_set_style_pad_all(s_scroll, 0, 0);
    lv_obj_set_style_pad_gap(s_scroll, 8, 0);
    lv_obj_set_style_bg_opa(s_scroll, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_scroll, 0, 0);

    lv_obj_t *q_row = lv_obj_create(s_scroll);
    lv_obj_set_width(q_row, LV_PCT(100));
    lv_obj_set_height(q_row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(q_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(q_row,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER);

    lv_obj_set_style_bg_opa(q_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(q_row, 0, 0);

    lv_obj_set_style_pad_left(q_row, 8, 0);
    lv_obj_set_style_pad_right(q_row, 8, 0);
    lv_obj_set_style_pad_top(q_row, 8, 0);
    lv_obj_set_style_pad_bottom(q_row, 2, 0);
    lv_obj_set_style_pad_gap(q_row, 8, 0);

    lv_obj_set_style_min_height(q_row, 0, 0);
    lv_obj_clear_flag(q_row, LV_OBJ_FLAG_SCROLLABLE);

    s_question_label = lv_label_create(q_row);
    lv_obj_set_flex_grow(s_question_label, 1);
    lv_obj_set_width(s_question_label, LV_PCT(100));
    lv_label_set_long_mode(s_question_label, LV_LABEL_LONG_WRAP);
    lv_label_set_text(s_question_label, "Loading...");

    lv_obj_set_style_text_font(s_question_label, UI_FONT_LARGE, 0);

    s_progress_label = lv_label_create(q_row);
    lv_label_set_text(s_progress_label, "0 / 0");
    lv_obj_set_style_text_font(s_progress_label, UI_FONT_SMALL, 0);
    lv_obj_set_style_text_align(s_progress_label, LV_TEXT_ALIGN_RIGHT, 0);

    for (int i = 0; i < QUIZ_OPTION_COUNT; i++)
    {
        s_option_labels[i] = lv_label_create(s_scroll);
        lv_obj_set_width(s_option_labels[i], LV_PCT(100));
        lv_label_set_long_mode(s_option_labels[i], LV_LABEL_LONG_WRAP);
        lv_label_set_text_fmt(s_option_labels[i], "%c. ", 'A' + i);

        lv_obj_set_style_pad_left(s_option_labels[i], 10, 0);
        lv_obj_set_style_pad_right(s_option_labels[i], 10, 0);
        lv_obj_set_style_pad_top(s_option_labels[i], 2, 0);
        lv_obj_set_style_pad_bottom(s_option_labels[i], 2, 0);

        lv_obj_set_style_text_font(s_option_labels[i], UI_FONT_LARGE, 0);
        lv_obj_set_style_text_color(s_option_labels[i], OPTION_TEXT_NORMAL_COLOR, 0);
    }

    lv_obj_t *btn_row = lv_obj_create(s_scroll);
    lv_obj_set_width(btn_row, LV_PCT(100));
    lv_obj_set_height(btn_row, 172);
    lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_row,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    lv_obj_set_style_pad_left(btn_row, 8, 0);
    lv_obj_set_style_pad_right(btn_row, 8, 0);
    lv_obj_set_style_pad_top(btn_row, 6, 0);
    lv_obj_set_style_pad_bottom(btn_row, 8, 0);
    lv_obj_set_style_pad_gap(btn_row, 8, 0);

    lv_obj_set_style_bg_opa(btn_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btn_row, 0, 0);
    lv_obj_clear_flag(btn_row, LV_OBJ_FLAG_SCROLLABLE);

    const lv_color_t border_col = lv_color_white();
    const lv_color_t checked_bg = lv_palette_main(LV_PALETTE_GREEN);

    for (int i = 0; i < QUIZ_OPTION_COUNT; i++)
    {
        s_option_btns[i] = lv_btn_create(btn_row);

        lv_obj_set_flex_grow(s_option_btns[i], 1);
        lv_obj_set_height(s_option_btns[i], 172);

        lv_obj_add_flag(s_option_btns[i], LV_OBJ_FLAG_CHECKABLE);

        lv_obj_set_style_border_width(s_option_btns[i], 2, LV_PART_MAIN);
        lv_obj_set_style_border_color(s_option_btns[i], border_col, LV_PART_MAIN);
        lv_obj_set_style_border_opa(s_option_btns[i], LV_OPA_100, LV_PART_MAIN);

        lv_obj_set_style_bg_color(s_option_btns[i], checked_bg, LV_PART_MAIN | LV_STATE_CHECKED);
        lv_obj_set_style_bg_opa(s_option_btns[i], LV_OPA_100, LV_PART_MAIN | LV_STATE_CHECKED);

        lv_obj_set_style_text_color(s_option_btns[i], lv_color_white(), LV_PART_MAIN | LV_STATE_CHECKED);

        lv_obj_add_event_cb(
            s_option_btns[i],
            quiz_handle_option,
            LV_EVENT_CLICKED,
            (void *)(intptr_t)i);

        lv_obj_t *lbl = lv_label_create(s_option_btns[i]);
        lv_label_set_text_fmt(lbl, "%c", 'A' + i);
        lv_obj_center(lbl);
        lv_obj_set_style_text_font(lbl, UI_FONT_LARGE, 0);
    }

    s_submit_btn = lv_btn_create(btn_row);
    lv_obj_set_flex_grow(s_submit_btn, 1);
    lv_obj_set_height(s_submit_btn, 172);

    lv_obj_set_style_border_width(s_submit_btn, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_submit_btn, border_col, LV_PART_MAIN);
    lv_obj_set_style_border_opa(s_submit_btn, LV_OPA_100, LV_PART_MAIN);

    lv_obj_add_event_cb(s_submit_btn, quiz_handle_submit, LV_EVENT_CLICKED, NULL);

    lv_obj_t *submit_lbl = lv_label_create(s_submit_btn);
    lv_label_set_text(submit_lbl, "Submit");
    lv_obj_center(submit_lbl);
    lv_obj_set_style_text_font(submit_lbl, UI_FONT_LARGE, 0);
}

static void quiz_build_result_screen(void)
{
    if (s_result_screen)
    {
        lv_obj_clean(s_result_screen);
    }
    else
    {
        s_result_screen = lv_obj_create(NULL);
    }

    lv_obj_set_size(s_result_screen, 640, 172);
    lv_obj_clear_flag(s_result_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_result_screen, lv_color_white(), 0);
    lv_obj_set_style_pad_all(s_result_screen, 0, 0);

    s_result_scroll = lv_obj_create(s_result_screen);
    lv_obj_set_size(s_result_scroll, 640, 172);
    lv_obj_align(s_result_scroll, LV_ALIGN_TOP_MID, 0, 0);

    lv_obj_set_scroll_dir(s_result_scroll, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_result_scroll, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_clear_flag(s_result_scroll, LV_OBJ_FLAG_SCROLL_ELASTIC);

    lv_obj_set_flex_flow(s_result_scroll, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(s_result_scroll, 8, 0);
    lv_obj_set_style_pad_gap(s_result_scroll, 6, 0);
    lv_obj_set_style_bg_opa(s_result_scroll, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_result_scroll, 0, 0);

    uint8_t local_correct = 0;
    for (uint8_t i = 0; i < s_state.question_count; i++)
    {
        if (s_state.answers[i] < QUIZ_OPTION_COUNT &&
            s_state.answers[i] == s_state.questions[i].correct_index)
        {
            local_correct++;
        }
    }

    int show_score = (s_state.server_score >= 0) ? s_state.server_score : local_correct;
    int show_total = (s_state.server_total > 0) ? s_state.server_total : s_state.question_count;
    if (show_total <= 0) show_total = 1;

    int wrong_cnt = show_total - show_score;
    if (wrong_cnt < 0) wrong_cnt = 0;

    int acc = (show_score * 100) / show_total;

    lv_obj_t *title = lv_label_create(s_result_scroll);
    lv_label_set_text(title, "Result");
    lv_obj_set_style_text_font(title, UI_FONT_NORMAL, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0x111111), 0);

    lv_obj_t *sum_row = lv_obj_create(s_result_scroll);
    lv_obj_clear_flag(sum_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_width(sum_row, LV_PCT(100));
    lv_obj_set_height(sum_row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(sum_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(sum_row,
                          LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(sum_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(sum_row, 0, 0);

    lv_obj_t *score_lbl = lv_label_create(sum_row);
    lv_label_set_text_fmt(score_lbl, "Score: %d/%d", show_score, show_total);
    lv_obj_set_style_text_font(score_lbl, UI_FONT_LARGE, 0);
    lv_obj_set_style_text_color(score_lbl, lv_color_hex(0x111111), 0);

    lv_obj_t *acc_lbl = lv_label_create(sum_row);
    lv_label_set_text_fmt(acc_lbl, "Acc: %d%%", acc);
    lv_obj_set_style_text_font(acc_lbl, UI_FONT_LARGE, 0);
    lv_obj_set_style_text_color(acc_lbl, lv_color_hex(0x111111), 0);

    lv_obj_t *time_lbl = lv_label_create(sum_row);
    lv_label_set_text(time_lbl, "Time: --:--");
    lv_obj_set_style_text_font(time_lbl, UI_FONT_LARGE, 0);
    lv_obj_set_style_text_color(time_lbl, lv_color_hex(0x111111), 0);

    lv_obj_t *cw_row = lv_obj_create(s_result_scroll);
    lv_obj_clear_flag(cw_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_width(cw_row, LV_PCT(100));
    lv_obj_set_height(cw_row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(cw_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(cw_row,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(cw_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(cw_row, 0, 0);
    lv_obj_set_style_pad_gap(cw_row, 18, 0);

    lv_obj_t *c_lbl = lv_label_create(cw_row);
    lv_label_set_text_fmt(c_lbl, "Correct: %d", show_score);
    lv_obj_set_style_text_font(c_lbl, UI_FONT_NORMAL, 0);
    lv_obj_set_style_text_color(c_lbl, lv_color_hex(0x1a7f37), 0);

    lv_obj_t *w_lbl = lv_label_create(cw_row);
    lv_label_set_text_fmt(w_lbl, "Wrong: %d", wrong_cnt);
    lv_obj_set_style_text_font(w_lbl, UI_FONT_NORMAL, 0);
    lv_obj_set_style_text_color(w_lbl, lv_color_hex(0xb42318), 0);

    lv_obj_t *divider = lv_obj_create(s_result_scroll);
    lv_obj_clear_flag(divider, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_width(divider, LV_PCT(100));
    lv_obj_set_height(divider, 1);
    lv_obj_set_style_bg_color(divider, lv_color_hex(0xE6E6E6), 0);
    lv_obj_set_style_bg_opa(divider, LV_OPA_100, 0);
    lv_obj_set_style_pad_all(divider, 0, 0);

    lv_obj_t *wl_title = lv_label_create(s_result_scroll);
    lv_label_set_text(wl_title, "Wrong list:");
    lv_obj_set_style_text_font(wl_title, UI_FONT_NORMAL, 0);
    lv_obj_set_style_text_color(wl_title, lv_color_hex(0x111111), 0);

    bool has_wrong = false;

#define CREATE_WRONG_ROW(_qno, _your_c, _ans_c)                                      \
    do                                                                              \
    {                                                                               \
        lv_obj_t *row = lv_obj_create(s_result_scroll);                             \
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);                             \
        lv_obj_set_width(row, LV_PCT(100));                                         \
        lv_obj_set_height(row, LV_SIZE_CONTENT);                                    \
                                                                                    \
        lv_obj_set_style_pad_all(row, 8, 0);                                        \
        lv_obj_set_style_radius(row, 10, 0);                                        \
        lv_obj_set_style_bg_color(row, lv_color_hex(0xF6F7F9), 0);                  \
        lv_obj_set_style_bg_opa(row, LV_OPA_100, 0);                                \
        lv_obj_set_style_border_width(row, 1, 0);                                   \
        lv_obj_set_style_border_color(row, lv_color_hex(0xE6E6E6), 0);              \
                                                                                    \
                                       \
        lv_obj_clear_flag(row, LV_OBJ_FLAG_CLICKABLE);                              \
                                                                                    \
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);                                \
        lv_obj_set_flex_align(row,                                                  \
                              LV_FLEX_ALIGN_SPACE_BETWEEN,                          \
                              LV_FLEX_ALIGN_CENTER,                                 \
                              LV_FLEX_ALIGN_CENTER);                                \
                                                                                    \
        lv_obj_t *left = lv_label_create(row);                                      \
        lv_label_set_text_fmt(left, "Q%d   Your:%c  Ans:%c", (_qno), (_your_c), (_ans_c)); \
        lv_obj_set_style_text_font(left, UI_FONT_NORMAL, 0);                        \
        lv_obj_set_style_text_color(left, lv_color_hex(0x111111), 0);               \
                                                                                    \
                              \
    } while (0)

    if (s_state.server_wrong_count > 0)
    {
        for (uint8_t i = 0; i < s_state.server_wrong_count; i++)
        {
            const quiz_wrong_item_t *wrong = &s_state.server_wrong[i];

            int q_index = -1;
            for (uint8_t k = 0; k < s_state.question_count; k++)
            {
                if (strncmp(s_state.questions[k].id, wrong->id, sizeof(s_state.questions[k].id)) == 0)
                {
                    q_index = (int)k;
                    break;
                }
            }
            if (q_index < 0) q_index = 0;

            char your_c = (wrong->your >= 0 && wrong->your < QUIZ_OPTION_COUNT) ? ('A' + wrong->your) : '-';
            char ans_c  = (wrong->correct >= 0 && wrong->correct < QUIZ_OPTION_COUNT) ? ('A' + wrong->correct) : '-';

            CREATE_WRONG_ROW(q_index + 1, your_c, ans_c);
            has_wrong = true;
        }
    }
    else
    {
        for (uint8_t i = 0; i < s_state.question_count; i++)
        {
            int your = (s_state.answers[i] < QUIZ_OPTION_COUNT) ? (int)s_state.answers[i] : -1;
            int corr = (int)s_state.questions[i].correct_index;

            if (your == corr)
            {
                continue;
            }

            char your_c = (your >= 0 && your < QUIZ_OPTION_COUNT) ? ('A' + your) : '-';
            char ans_c  = (corr >= 0 && corr < QUIZ_OPTION_COUNT) ? ('A' + corr) : '-';

            CREATE_WRONG_ROW(i + 1, your_c, ans_c);
            has_wrong = true;
        }
    }

#undef CREATE_WRONG_ROW

    if (!has_wrong)
    {
        lv_obj_t *all_good = lv_label_create(s_result_scroll);
        lv_label_set_text(all_good, "All answers correct!");
        lv_obj_set_style_text_font(all_good, UI_FONT_NORMAL, 0);
        lv_obj_set_style_text_color(all_good, lv_color_hex(0x1a7f37), 0);
    }

    lv_obj_t *btn_row = lv_obj_create(s_result_scroll);
    lv_obj_clear_flag(btn_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_width(btn_row, LV_PCT(100));
    lv_obj_set_height(btn_row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_row,
                          LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    lv_obj_set_style_bg_opa(btn_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(btn_row, 0, 0);
    lv_obj_set_style_pad_gap(btn_row, 10, 0);

    lv_obj_t *retry_btn = lv_btn_create(btn_row);
    lv_obj_set_height(retry_btn, 44);
    lv_obj_set_flex_grow(retry_btn, 1);
    lv_obj_set_style_bg_color(retry_btn, lv_palette_main(LV_PALETTE_BLUE), 0);
    lv_obj_set_style_radius(retry_btn, 10, 0);
    lv_obj_set_style_border_width(retry_btn, 1, 0);
    lv_obj_set_style_border_color(retry_btn, lv_color_hex(0x2f6ee2), 0);
    lv_obj_add_event_cb(retry_btn, quiz_handle_start_test, LV_EVENT_CLICKED, NULL);

    lv_obj_t *retry_lbl = lv_label_create(retry_btn);
    lv_label_set_text(retry_lbl, "Retry");
    lv_obj_set_style_text_font(retry_lbl, UI_FONT_NORMAL, 0);
    lv_obj_set_style_text_color(retry_lbl, lv_color_white(), 0);
    lv_obj_center(retry_lbl);

    lv_obj_t *back_btn = lv_btn_create(btn_row);
    lv_obj_set_height(back_btn, 44);
    lv_obj_set_flex_grow(back_btn, 1);
    lv_obj_set_style_bg_color(back_btn, lv_color_hex(0xEDEFF2), 0);
    lv_obj_set_style_radius(back_btn, 10, 0);
    lv_obj_set_style_border_width(back_btn, 1, 0);
    lv_obj_set_style_border_color(back_btn, lv_color_hex(0xD0D5DD), 0);
    lv_obj_add_event_cb(back_btn, quiz_back_to_home, LV_EVENT_CLICKED, NULL);

    lv_obj_t *back_lbl = lv_label_create(back_btn);
    lv_label_set_text(back_lbl, "Back");
    lv_obj_set_style_text_font(back_lbl, UI_FONT_NORMAL, 0);
    lv_obj_set_style_text_color(back_lbl, lv_color_hex(0x111111), 0);
    lv_obj_center(back_lbl);
}

static void quiz_show_results_screen(void)
{
    quiz_build_result_screen();
    lv_scr_load(s_result_screen);

    if (s_result_scroll)
    {
        lv_obj_scroll_to_y(s_result_scroll, 0, LV_ANIM_OFF);
    }
}

static void quiz_handle_option(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
    {
        return;
    }

    int index = (int)(intptr_t)lv_event_get_user_data(e);
    if (index < 0 || index >= QUIZ_OPTION_COUNT)
    {
        return;
    }

    for (int i = 0; i < QUIZ_OPTION_COUNT; i++)
    {
        if (i == index)
        {
            lv_obj_add_state(s_option_btns[i], LV_STATE_CHECKED);
        }
        else
        {
            lv_obj_clear_state(s_option_btns[i], LV_STATE_CHECKED);
        }
    }

    for (int i = 0; i < QUIZ_OPTION_COUNT; i++)
    {
        if (i == index)
        {
            lv_obj_set_style_text_color(
                s_option_labels[i],
                OPTION_TEXT_ACTIVE_COLOR,
                0);
        }
        else
        {
            lv_obj_set_style_text_color(
                s_option_labels[i],
                OPTION_TEXT_NORMAL_COLOR,
                0);
        }
    }

    s_state.answers[s_state.current_question] = (uint8_t)index;
}

static void quiz_sanitize_text(char *dst, size_t dst_sz, const char *src)
{
    if (!dst || dst_sz == 0) return;

    size_t w = 0;
    int last_space = 1;

    for (size_t r = 0; src && src[r] != '\0' && w + 1 < dst_sz; r++)
    {
        char c = src[r];
        if (c == '\r' || c == '\n' || c == '\t') c = ' ';

        if (c == ' ')
        {
            if (last_space) continue;
            last_space = 1;
        }
        else
        {
            last_space = 0;
        }
        dst[w++] = c;
    }

    while (w > 0 && dst[w - 1] == ' ') w--;
    dst[w] = '\0';
}

static void quiz_load_question(uint8_t index)
{
    if (index >= s_state.question_count)
    {
        return;
    }

    lv_label_set_text_fmt(
        s_progress_label,
        "%d / %d",
        index + 1,
        s_state.question_count
    );

    char stem_clean[QUIZ_TEXT_LEN + 1];
    quiz_sanitize_text(stem_clean, sizeof(stem_clean), s_state.questions[index].stem);

    char q_line[QUIZ_TEXT_LEN + 16];
    snprintf(q_line, sizeof(q_line), "Q%d: %s", index + 1, stem_clean);
    lv_label_set_text(s_question_label, q_line);

    for (int i = 0; i < QUIZ_OPTION_COUNT; i++)
    {
        char opt_clean[QUIZ_TEXT_LEN + 1];
        quiz_sanitize_text(opt_clean, sizeof(opt_clean), s_state.questions[index].options[i]);

        lv_label_set_text_fmt(
            s_option_labels[i],
            "%c. %s",
            'A' + i,
            opt_clean
        );

        lv_obj_clear_state(s_option_btns[i], LV_STATE_CHECKED);

        lv_obj_set_style_text_color(s_option_labels[i], OPTION_TEXT_NORMAL_COLOR, 0);
    }

    if (s_state.answers[index] < QUIZ_OPTION_COUNT)
    {
        uint8_t sel = s_state.answers[index];
        lv_obj_add_state(s_option_btns[sel], LV_STATE_CHECKED);

        lv_obj_set_style_text_color(s_option_labels[sel], OPTION_TEXT_ACTIVE_COLOR, 0);
    }

    lv_obj_scroll_to_y(s_scroll, 0, LV_ANIM_OFF);
}

static void quiz_finish_and_upload(void)
{
    s_state.test_finished = true;
    quiz_reset_server_result();

    if (s_user_id[0] == '\0')
    {
        quiz_show_toast("User id missing", 1600);
        return;
    }

    if (s_quiz_id[0] == '\0')
    {
        quiz_show_toast("Quiz id missing", 1600);
        return;
    }

    uint8_t correct = 0;
    s_state.server_wrong_count = 0;
    for (uint8_t i = 0; i < s_state.question_count; i++)
    {
        int your_choice = (s_state.answers[i] < QUIZ_OPTION_COUNT) ? s_state.answers[i] : -1;
        int correct_choice = s_state.questions[i].correct_index;
        if (your_choice == correct_choice)
        {
            correct++;
        }
        else if (s_state.server_wrong_count < QUIZ_MAX_QUESTIONS)
        {
            quiz_wrong_item_t *dst = &s_state.server_wrong[s_state.server_wrong_count];
            memset(dst, 0, sizeof(*dst));
            strncpy(dst->id, s_state.questions[i].id, sizeof(dst->id) - 1);
            dst->correct = correct_choice;
            dst->your = your_choice;
            s_state.server_wrong_count++;
        }
    }

    s_state.server_score = correct;
    s_state.server_total = s_state.question_count;

    char payload[256];
    snprintf(payload, sizeof(payload),
             "{\"user_id\":\"%s\",\"quiz_id\":\"%s\",\"score\":%d}",
             s_user_id, s_quiz_id, s_state.server_score);

    esp_err_t err = quiz_http_post_results(payload);
    if (err == ESP_OK)
    {
        quiz_show_toast("Results uploaded. View results from home.", 1800);
        if (s_home_screen)
        {
            lv_scr_load(s_home_screen);
        }
    }
    else
    {
        quiz_show_toast("Upload failed", 1800);
    }
}

static void quiz_hide_toast_cb(lv_timer_t *t)
{
    LV_UNUSED(t);
    if (s_toast_label)
    {
        lv_obj_add_flag(s_toast_label, LV_OBJ_FLAG_HIDDEN);
    }
}

static void quiz_back_to_home(lv_event_t *e)
{
    LV_UNUSED(e);
    if (s_home_screen)
    {
        lv_scr_load(s_home_screen);
    }
}

static void quiz_handle_submit(lv_event_t *e)
{
    LV_UNUSED(e);
    uint8_t idx = s_state.current_question;
    if (idx >= s_state.question_count)
    {
        return;
    }

    if (s_state.answers[idx] >= QUIZ_OPTION_COUNT)
    {
        quiz_show_toast("Select an option", 1600);
        return;
    }

    if (idx + 1 >= s_state.question_count)
    {
        quiz_finish_and_upload();
    }
    else
    {
        s_state.current_question++;
        s_state.answers[s_state.current_question] = 0xFF;
        quiz_load_question(s_state.current_question);
    }
}

void quiz_app_create_ui(void)
{
    if (s_home_screen)
    {
        lv_scr_load(s_home_screen);
        return;
    }

    memset(&s_state, 0, sizeof(s_state));
    memset(s_state.answers, 0xFF, sizeof(s_state.answers));
    quiz_reset_server_result();

    quiz_create_home_screen();
    quiz_build_test_screen();

    lv_scr_load(s_home_screen);
}

void quiz_app_set_user_id(const char *user_id)
{
    if (user_id)
    {
        strncpy(s_user_id, user_id, sizeof(s_user_id) - 1);
        s_user_id[sizeof(s_user_id) - 1] = '\0';
    }
    else
    {
        s_user_id[0] = '\0';
    }

    s_quiz_id[0] = '\0';
}

static void quiz_reset_server_result(void)
{
    s_state.server_score = -1;
    s_state.server_total = s_state.question_count;
    s_state.server_wrong_count = 0;
    memset(s_state.server_wrong, 0, sizeof(s_state.server_wrong));
}

static const quiz_question_t *quiz_find_question_by_id(const char *id)
{
    if (!id)
    {
        return NULL;
    }
    for (uint8_t i = 0; i < s_state.question_count; i++)
    {
        if (strncmp(s_state.questions[i].id, id, sizeof(s_state.questions[i].id)) == 0)
        {
            return &s_state.questions[i];
        }
    }
    return NULL;
}
```

## main/quiz_app.h

```c
#ifndef QUIZ_APP_H
#define QUIZ_APP_H

#ifdef __cplusplus
extern "C" {
#endif

void quiz_app_create_ui(void);

void quiz_app_set_user_id(const char *user_id);

#ifdef __cplusplus
}
#endif

#endif
```

## main/user_config.h

```c
#ifndef USER_CONFIG_H
#define USER_CONFIG_H

#define LCD_HOST SPI3_HOST

#define Touch_SCL_NUM (GPIO_NUM_18)
#define Touch_SDA_NUM (GPIO_NUM_17)

#define ESP_SCL_NUM (GPIO_NUM_48)
#define ESP_SDA_NUM (GPIO_NUM_47)

#define EXAMPLE_PIN_NUM_LCD_CS     (GPIO_NUM_9)
#define EXAMPLE_PIN_NUM_LCD_PCLK   (GPIO_NUM_10)
#define EXAMPLE_PIN_NUM_LCD_DATA0  (GPIO_NUM_11)
#define EXAMPLE_PIN_NUM_LCD_DATA1  (GPIO_NUM_12)
#define EXAMPLE_PIN_NUM_LCD_DATA2  (GPIO_NUM_13)
#define EXAMPLE_PIN_NUM_LCD_DATA3  (GPIO_NUM_14)
#define EXAMPLE_PIN_NUM_LCD_RST    (GPIO_NUM_21)
#define EXAMPLE_PIN_NUM_BK_LIGHT   (GPIO_NUM_8)

#define EXAMPLE_PIN_NUM_TOUCH_ADDR        0x3b
#define EXAMPLE_PIN_NUM_TOUCH_RST         (-1)
#define EXAMPLE_PIN_NUM_TOUCH_INT         (-1)

#define EXAMPLE_LVGL_TICK_PERIOD_MS    5
#define EXAMPLE_LVGL_TASK_MAX_DELAY_MS 500
#define EXAMPLE_LVGL_TASK_MIN_DELAY_MS 5

#define EXAMPLE_RTC_ADDR 0x51

#define EXAMPLE_IMU_ADDR 0x6b

#define USER_DISP_ROT_90    1
#define USER_DISP_ROT_NONO  0
#define Rotated USER_DISP_ROT_90

#define Backlight_Testing 0

#if (Rotated == USER_DISP_ROT_NONO)
#define EXAMPLE_LCD_H_RES 172
#define EXAMPLE_LCD_V_RES 640
#else
#define EXAMPLE_LCD_H_RES 640
#define EXAMPLE_LCD_V_RES 172
#endif

#define LCD_NOROT_HRES     172
#define LCD_NOROT_VRES     640
#define LVGL_DMA_BUFF_LEN (LCD_NOROT_HRES * 64 * 2)
#define LVGL_SPIRAM_BUFF_LEN (EXAMPLE_LCD_H_RES * EXAMPLE_LCD_V_RES * 2)

#ifndef APP_WIFI_SSID
#define APP_WIFI_SSID "JD"
#endif

#ifndef APP_WIFI_PASS
#define APP_WIFI_PASS "123456789"
#endif

#ifndef APP_SERVER_HOST
#define APP_SERVER_HOST "10.57.246.86"
#endif

#ifndef APP_SERVER_PORT
#define APP_SERVER_PORT 8000
#endif

#ifndef APP_API_LOGIN_PATH
#define APP_API_LOGIN_PATH "/client/login"
#endif

#ifndef APP_API_STATUS_PATH
#define APP_API_STATUS_PATH "/auth/status"
#endif

#ifndef APP_API_QUESTIONS_PATH
#define APP_API_QUESTIONS_PATH "/questions"
#endif

#ifndef APP_API_SUBMIT_PATH
#define APP_API_SUBMIT_PATH "/submit"
#endif

#endif
```

## components/button_bsp/button_bsp.c

```c
#include "button_bsp.h"
#include "multi_button.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"

EventGroupHandle_t boot_groups;
EventGroupHandle_t pwr_groups;

static Button button1;
#define USER_KEY_1 0
#define button1_id 1
#define button1_active 0

static Button button2;
#define USER_KEY_2 16
#define button2_id 2
#define button2_active 0

static void on_button2_press_repeat(Button* btn_handle);
static void on_button2_single_click(Button* btn_handle);
static void on_button2_double_click(Button* btn_handle);
static void on_button2_long_press_start(Button* btn_handle);
static void on_button2_long_press_hold(Button* btn_handle);
static void on_button2_press_down(Button* btn_handle);
static void on_button2_press_up(Button* btn_handle);

static void on_boot_single_click(Button* btn_handle);
static void on_boot_double_click(Button* btn_handle);
static void on_boot_long_press_start(Button* btn_handle);
static void on_boot_press_up(Button* btn_handle);

static void clock_task_callback(void *arg)
{
  button_ticks();
}
static uint8_t read_button_GPIO(uint8_t button_id)
{
	switch (button_id)
  {
    case button1_id:
      return gpio_get_level(USER_KEY_1);
    case button2_id:
      return gpio_get_level(USER_KEY_2);
    default:
      break;
  }
  return 1;
}

static void gpio_init(void)
{
  gpio_config_t gpio_conf = {};
  gpio_conf.intr_type = GPIO_INTR_DISABLE;
  gpio_conf.mode = GPIO_MODE_INPUT;
  gpio_conf.pin_bit_mask = ((uint64_t)0x01<<USER_KEY_2) | ((uint64_t)0x01<<USER_KEY_1);
  gpio_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
  gpio_conf.pull_up_en = GPIO_PULLUP_ENABLE;

  ESP_ERROR_CHECK_WITHOUT_ABORT(gpio_config(&gpio_conf));
}

void button_Init(void)
{
  boot_groups = xEventGroupCreate();
  pwr_groups = xEventGroupCreate();
  gpio_init();

  button_init(&button1, read_button_GPIO, button1_active , button1_id);
  button_attach(&button1,BTN_SINGLE_CLICK,on_boot_single_click);
  button_attach(&button1,BTN_LONG_PRESS_START,on_boot_long_press_start);
  button_attach(&button1,BTN_PRESS_REPEAT,on_boot_double_click);
  button_attach(&button1,BTN_PRESS_UP,on_boot_press_up);

  button_init(&button2, read_button_GPIO, button2_active , button2_id);
  button_attach(&button2,BTN_PRESS_REPEAT,on_button2_press_repeat);
  button_attach(&button2,BTN_SINGLE_CLICK,on_button2_single_click);
  button_attach(&button2,BTN_DOUBLE_CLICK,on_button2_double_click);
  button_attach(&button2,BTN_LONG_PRESS_START,on_button2_long_press_start);
  button_attach(&button2,BTN_PRESS_DOWN,on_button2_press_down);
  button_attach(&button2,BTN_PRESS_UP,on_button2_press_up);
  button_attach(&button2,BTN_LONG_PRESS_HOLD,on_button2_long_press_hold);

  const esp_timer_create_args_t clock_tick_timer_args =
  {
    .callback = &clock_task_callback,
    .name = "clock_task",
    .arg = NULL,
  };
  esp_timer_handle_t clock_tick_timer = NULL;
  ESP_ERROR_CHECK(esp_timer_create(&clock_tick_timer_args, &clock_tick_timer));
  ESP_ERROR_CHECK(esp_timer_start_periodic(clock_tick_timer, 1000 * 5));
  button_start(&button2);
  button_start(&button1);
}

static void on_button2_press_repeat(Button* btn_handle)
{
  xEventGroupSetBits(pwr_groups,set_bit_button(0));
}

static void on_button2_single_click(Button* btn_handle)
{
  xEventGroupSetBits(pwr_groups,set_bit_button(0));
}

static void on_button2_double_click(Button* btn_handle)
{

}

static void on_button2_long_press_start(Button* btn_handle)
{
  xEventGroupSetBits(pwr_groups,set_bit_button(1));
}

static void on_button2_long_press_hold(Button* btn_handle)
{

}

static void on_button2_press_down(Button* btn_handle)
{

}

static void on_button2_press_up(Button* btn_handle)
{
  xEventGroupSetBits(pwr_groups,set_bit_button(2));
}

static void on_boot_single_click(Button* btn_handle)
{
  xEventGroupSetBits(boot_groups,set_bit_button(0));
}

static void on_boot_double_click(Button* btn_handle)
{
  xEventGroupSetBits(boot_groups,set_bit_button(1));
}

static void on_boot_long_press_start(Button* btn_handle)
{
  xEventGroupSetBits(boot_groups,set_bit_button(2));
}

static void on_boot_press_up(Button* btn_handle)
{
  xEventGroupSetBits(boot_groups,set_bit_button(3));
}

uint8_t user_button_get_repeat_count(void)
{
  return (button_get_repeat_count(&button2));
}

uint8_t user_boot_get_repeat_count(void)
{
  return (button_get_repeat_count(&button1));
}
```

## components/button_bsp/button_bsp.h

```c
#ifndef BUTTON_BSP_H
#define BUTTON_BSP_H
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#ifdef __cplusplus
extern "C" {
#endif

extern EventGroupHandle_t boot_groups;
extern EventGroupHandle_t pwr_groups;
#define set_bit_button(x) ((uint32_t)(0x01)<<(x))
#define get_bit_button(x,y) (((uint32_t)(x)>>(y)) & 0x01)
#define set_bit_all 0x00ffffff

#define set_bit_data(x,y) (x |= (0x01<<y))
#define clr_bit_data(x,y) (x &= ~(0x01<<y))
#define get_bit_data(x,y) ((x>>y) & 0x01)
#define rset_bit_data(x) ((uint32_t)0x01<<(x))

void button_Init(void);
uint8_t user_button_get_repeat_count(void);
uint8_t user_boot_get_repeat_count(void);

#ifdef __cplusplus
}
#endif
#endif
```

## components/button_bsp/multi_button.c

```c
#include "multi_button.h"

#define EVENT_CB(ev)   do { if(handle->cb[ev]) handle->cb[ev](handle); } while(0)

static Button* head_handle = NULL;

static void button_handler(Button* handle);
static inline uint8_t button_read_level(Button* handle);

void button_init(Button* handle, uint8_t(*pin_level)(uint8_t), uint8_t active_level, uint8_t button_id)
{
	if (!handle || !pin_level) return;

	memset(handle, 0, sizeof(Button));
	handle->event = (uint8_t)BTN_NONE_PRESS;
	handle->hal_button_level = pin_level;
	handle->button_level = !active_level;
	handle->active_level = active_level;
	handle->button_id = button_id;
	handle->state = BTN_STATE_IDLE;
}

void button_attach(Button* handle, ButtonEvent event, BtnCallback cb)
{
	if (!handle || event >= BTN_EVENT_COUNT) return;
	handle->cb[event] = cb;
}

void button_detach(Button* handle, ButtonEvent event)
{
	if (!handle || event >= BTN_EVENT_COUNT) return;
	handle->cb[event] = NULL;
}

ButtonEvent button_get_event(Button* handle)
{
	if (!handle) return BTN_NONE_PRESS;
	return (ButtonEvent)(handle->event);
}

uint8_t button_get_repeat_count(Button* handle)
{
	if (!handle) return 0;
	return handle->repeat;
}

void button_reset(Button* handle)
{
	if (!handle) return;
	handle->state = BTN_STATE_IDLE;
	handle->ticks = 0;
	handle->repeat = 0;
	handle->event = (uint8_t)BTN_NONE_PRESS;
	handle->debounce_cnt = 0;
}

int button_is_pressed(Button* handle)
{
	if (!handle) return -1;
	return (handle->button_level == handle->active_level) ? 1 : 0;
}

static inline uint8_t button_read_level(Button* handle)
{
	return handle->hal_button_level(handle->button_id);
}

static void button_handler(Button* handle)
{
	uint8_t read_gpio_level = button_read_level(handle);

	if (handle->state > BTN_STATE_IDLE) {
		handle->ticks++;
	}

	if (read_gpio_level != handle->button_level) {

		if (++(handle->debounce_cnt) >= DEBOUNCE_TICKS) {
			handle->button_level = read_gpio_level;
			handle->debounce_cnt = 0;
		}
	} else {

		handle->debounce_cnt = 0;
	}

	switch (handle->state) {
	case BTN_STATE_IDLE:
		if (handle->button_level == handle->active_level) {

			handle->event = (uint8_t)BTN_PRESS_DOWN;
			EVENT_CB(BTN_PRESS_DOWN);
			handle->ticks = 0;
			handle->repeat = 1;
			handle->state = BTN_STATE_PRESS;
		} else {
			handle->event = (uint8_t)BTN_NONE_PRESS;
		}
		break;

	case BTN_STATE_PRESS:
		if (handle->button_level != handle->active_level) {

			handle->event = (uint8_t)BTN_PRESS_UP;
			EVENT_CB(BTN_PRESS_UP);
			handle->ticks = 0;
			handle->state = BTN_STATE_RELEASE;
		} else if (handle->ticks > LONG_TICKS) {

			handle->event = (uint8_t)BTN_LONG_PRESS_START;
			EVENT_CB(BTN_LONG_PRESS_START);
			handle->state = BTN_STATE_LONG_HOLD;
		}
		break;

	case BTN_STATE_RELEASE:
		if (handle->button_level == handle->active_level) {

			handle->event = (uint8_t)BTN_PRESS_DOWN;
			EVENT_CB(BTN_PRESS_DOWN);
			if (handle->repeat < PRESS_REPEAT_MAX_NUM) {
				handle->repeat++;
			}
			EVENT_CB(BTN_PRESS_REPEAT);
			handle->ticks = 0;
			handle->state = BTN_STATE_REPEAT;
		} else if (handle->ticks > SHORT_TICKS) {

			if (handle->repeat == 1) {
				handle->event = (uint8_t)BTN_SINGLE_CLICK;
				EVENT_CB(BTN_SINGLE_CLICK);
			} else if (handle->repeat == 2) {
				handle->event = (uint8_t)BTN_DOUBLE_CLICK;
				EVENT_CB(BTN_DOUBLE_CLICK);
			}
			handle->state = BTN_STATE_IDLE;
		}
		break;

	case BTN_STATE_REPEAT:
		if (handle->button_level != handle->active_level) {

			handle->event = (uint8_t)BTN_PRESS_UP;
			EVENT_CB(BTN_PRESS_UP);
			if (handle->ticks < SHORT_TICKS) {
				handle->ticks = 0;
				handle->state = BTN_STATE_RELEASE;
			} else {
				handle->state = BTN_STATE_IDLE;
			}
		} else if (handle->ticks > SHORT_TICKS) {

			handle->state = BTN_STATE_PRESS;
		}
		break;

	case BTN_STATE_LONG_HOLD:
		if (handle->button_level == handle->active_level) {

			handle->event = (uint8_t)BTN_LONG_PRESS_HOLD;
			EVENT_CB(BTN_LONG_PRESS_HOLD);
		} else {

			handle->event = (uint8_t)BTN_PRESS_UP;
			EVENT_CB(BTN_PRESS_UP);
			handle->state = BTN_STATE_IDLE;
		}
		break;

	default:

		handle->state = BTN_STATE_IDLE;
		break;
	}
}

int button_start(Button* handle)
{
	if (!handle) return -2;

	Button* target = head_handle;
	while (target) {
		if (target == handle) return -1;
		target = target->next;
	}

	handle->next = head_handle;
	head_handle = handle;
	return 0;
}

void button_stop(Button* handle)
{
	if (!handle) return;

	Button** curr;
	for (curr = &head_handle; *curr; ) {
		Button* entry = *curr;
		if (entry == handle) {
			*curr = entry->next;
			entry->next = NULL;
			return;
		} else {
			curr = &entry->next;
		}
	}
}

void button_ticks(void)
{
	Button* target;
	for (target = head_handle; target; target = target->next) {
		button_handler(target);
	}
}
```

## components/button_bsp/multi_button.h

```c
#ifndef _MULTI_BUTTON_H_
#define _MULTI_BUTTON_H_

#include <stdint.h>
#include <string.h>

#define TICKS_INTERVAL          5
#define DEBOUNCE_TICKS          3
#define SHORT_TICKS             (300 / TICKS_INTERVAL)
#define LONG_TICKS              (1000 / TICKS_INTERVAL)
#define PRESS_REPEAT_MAX_NUM    15

typedef struct _Button Button;

typedef void (*BtnCallback)(Button* btn_handle);

typedef enum {
	BTN_PRESS_DOWN = 0,
	BTN_PRESS_UP,
	BTN_PRESS_REPEAT,
	BTN_SINGLE_CLICK,
	BTN_DOUBLE_CLICK,
	BTN_LONG_PRESS_START,
	BTN_LONG_PRESS_HOLD,
	BTN_EVENT_COUNT,
	BTN_NONE_PRESS
} ButtonEvent;

typedef enum {
	BTN_STATE_IDLE = 0,
	BTN_STATE_PRESS,
	BTN_STATE_RELEASE,
	BTN_STATE_REPEAT,
	BTN_STATE_LONG_HOLD
} ButtonState;

struct _Button {
	uint16_t ticks;
	uint8_t  repeat : 4;
	uint8_t  event : 4;
	uint8_t  state : 3;
	uint8_t  debounce_cnt : 3;
	uint8_t  active_level : 1;
	uint8_t  button_level : 1;
	uint8_t  button_id;
	uint8_t  (*hal_button_level)(uint8_t button_id);
	BtnCallback cb[BTN_EVENT_COUNT];
	Button* next;
};

#ifdef __cplusplus
extern "C" {
#endif

void button_init(Button* handle, uint8_t(*pin_level)(uint8_t), uint8_t active_level, uint8_t button_id);
void button_attach(Button* handle, ButtonEvent event, BtnCallback cb);
void button_detach(Button* handle, ButtonEvent event);
ButtonEvent button_get_event(Button* handle);
int  button_start(Button* handle);
void button_stop(Button* handle);
void button_ticks(void);

uint8_t button_get_repeat_count(Button* handle);
void button_reset(Button* handle);
int button_is_pressed(Button* handle);

#ifdef __cplusplus
}
#endif

#endif
```

## components/i2c_bsp/i2c_bsp.c

```c
#include <stdio.h>
#include "i2c_bsp.h"
#include "user_config.h"
#include "freertos/FreeRTOS.h"

static i2c_master_bus_handle_t user_i2c_port0_handle = NULL;
static i2c_master_bus_handle_t user_i2c_port1_handle = NULL;
i2c_master_dev_handle_t disp_touch_dev_handle = NULL;
i2c_master_dev_handle_t rtc_dev_handle = NULL;
i2c_master_dev_handle_t imu_dev_handle = NULL;

static uint32_t i2c_data_pdMS_TICKS = 0;
static uint32_t i2c_done_pdMS_TICKS = 0;

void i2c_master_Init(void)
{
  i2c_data_pdMS_TICKS = pdMS_TO_TICKS(5000);
  i2c_done_pdMS_TICKS = pdMS_TO_TICKS(1000);

  i2c_master_bus_config_t i2c_bus_config =
  {
    .clk_source = I2C_CLK_SRC_DEFAULT,
    .i2c_port = I2C_NUM_0,
    .scl_io_num = ESP_SCL_NUM,
    .sda_io_num = ESP_SDA_NUM,
    .glitch_ignore_cnt = 7,
    .flags = {
      .enable_internal_pullup = true,
    },
  };
  ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_config, &user_i2c_port0_handle));
  i2c_bus_config.scl_io_num = Touch_SCL_NUM;
  i2c_bus_config.sda_io_num = Touch_SDA_NUM;
  i2c_bus_config.i2c_port = I2C_NUM_1;
  ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_config, &user_i2c_port1_handle));

  i2c_device_config_t dev_cfg =
  {
    .dev_addr_length = I2C_ADDR_BIT_LEN_7,
    .scl_speed_hz = 300000,
  };
  dev_cfg.device_address = EXAMPLE_RTC_ADDR;
  ESP_ERROR_CHECK(i2c_master_bus_add_device(user_i2c_port0_handle, &dev_cfg, &rtc_dev_handle));

  dev_cfg.device_address = EXAMPLE_IMU_ADDR;
  ESP_ERROR_CHECK(i2c_master_bus_add_device(user_i2c_port0_handle, &dev_cfg, &imu_dev_handle));

  dev_cfg.device_address = EXAMPLE_PIN_NUM_TOUCH_ADDR;
  ESP_ERROR_CHECK(i2c_master_bus_add_device(user_i2c_port1_handle, &dev_cfg, &disp_touch_dev_handle));

}

uint8_t i2c_writr_buff(i2c_master_dev_handle_t dev_handle,int reg,uint8_t *buf,uint8_t len)
{
  uint8_t ret;
  uint8_t *pbuf = NULL;
  ret = i2c_master_bus_wait_all_done(user_i2c_port0_handle,i2c_done_pdMS_TICKS);
  if(ret != ESP_OK)
  return ret;
  if(reg == -1)
  {
    ret = i2c_master_transmit(dev_handle,buf,len,i2c_data_pdMS_TICKS);
  }
  else
  {
    pbuf = (uint8_t*)malloc(len+1);
    pbuf[0] = reg;
    for(uint8_t i = 0; i<len; i++)
    {
      pbuf[i+1] = buf[i];
    }
    ret = i2c_master_transmit(dev_handle,pbuf,len+1,i2c_data_pdMS_TICKS);
    free(pbuf);
    pbuf = NULL;
  }
  return ret;
}
uint8_t i2c_master_write_read_dev(i2c_master_dev_handle_t dev_handle,uint8_t *writeBuf,uint8_t writeLen,uint8_t *readBuf,uint8_t readLen)
{
  uint8_t ret;
  ret = i2c_master_bus_wait_all_done(user_i2c_port0_handle,i2c_done_pdMS_TICKS);
  if(ret != ESP_OK)
  return ret;
  ret = i2c_master_transmit_receive(dev_handle,writeBuf,writeLen,readBuf,readLen,i2c_data_pdMS_TICKS);
  return ret;
}
uint8_t i2c_read_buff(i2c_master_dev_handle_t dev_handle,int reg,uint8_t *buf,uint8_t len)
{
  uint8_t ret;
  uint8_t addr = 0;
  ret = i2c_master_bus_wait_all_done(user_i2c_port0_handle,i2c_done_pdMS_TICKS);
  if(ret != ESP_OK)
  return ret;
  if( reg == -1 )
  {ret = i2c_master_receive(dev_handle, buf,len, i2c_data_pdMS_TICKS);}
  else
  {addr = (uint8_t)reg; ret = i2c_master_transmit_receive(dev_handle,&addr,1,buf,len,i2c_data_pdMS_TICKS);}
  return ret;
}

uint8_t i2c_master_touch_write_read(i2c_master_dev_handle_t dev_handle,uint8_t *writeBuf,uint8_t writeLen,uint8_t *readBuf,uint8_t readLen)
{
  uint8_t ret;
  ret = i2c_master_bus_wait_all_done(user_i2c_port1_handle,i2c_done_pdMS_TICKS);
  if(ret != ESP_OK)
  return ret;
  ret = i2c_master_transmit_receive(dev_handle,writeBuf,writeLen,readBuf,readLen,i2c_data_pdMS_TICKS);
  return ret;
}
```

## components/i2c_bsp/i2c_bsp.h

```c
#ifndef I2C_BSP_H
#define I2C_BSP_H
#include "driver/i2c_master.h"

extern i2c_master_dev_handle_t disp_touch_dev_handle;
extern i2c_master_dev_handle_t rtc_dev_handle;
extern i2c_master_dev_handle_t imu_dev_handle;

#ifdef __cplusplus
extern "C" {
#endif

void i2c_master_Init(void);
uint8_t i2c_writr_buff(i2c_master_dev_handle_t dev_handle,int reg,uint8_t *buf,uint8_t len);
uint8_t i2c_master_write_read_dev(i2c_master_dev_handle_t dev_handle,uint8_t *writeBuf,uint8_t writeLen,uint8_t *readBuf,uint8_t readLen);
uint8_t i2c_read_buff(i2c_master_dev_handle_t dev_handle,int reg,uint8_t *buf,uint8_t len);
uint8_t i2c_master_touch_write_read(i2c_master_dev_handle_t dev_handle,uint8_t *writeBuf,uint8_t writeLen,uint8_t *readBuf,uint8_t readLen);

#ifdef __cplusplus
}
#endif

#endif
```

## components/lcd_bl_pwm_bsp/lcd_bl_pwm_bsp.c

```c
#include <stdio.h>
#include "lcd_bl_pwm_bsp.h"
#include "esp_err.h"
#include "driver/ledc.h"
#include "driver/gpio.h"
#include "user_config.h"

void gpio_init(void)
{
  gpio_config_t gpio_conf = {};
  gpio_conf.intr_type = GPIO_INTR_DISABLE;
  gpio_conf.mode = GPIO_MODE_OUTPUT;
  gpio_conf.pin_bit_mask = ((uint64_t)0X01<<EXAMPLE_PIN_NUM_BK_LIGHT);
  gpio_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
  gpio_conf.pull_up_en = GPIO_PULLUP_ENABLE;

  ESP_ERROR_CHECK_WITHOUT_ABORT(gpio_config(&gpio_conf));
}
void lcd_bl_pwm_bsp_init(uint16_t duty)
{
  ledc_timer_config_t timer_conf =
  {
    .speed_mode =  LEDC_LOW_SPEED_MODE,
    .duty_resolution = LEDC_TIMER_8_BIT,
    .timer_num =  LEDC_TIMER_3,
    .freq_hz = 50 * 1000,
    .clk_cfg = LEDC_SLOW_CLK_RC_FAST,
  };
  ledc_channel_config_t ledc_conf =
  {
    .gpio_num = EXAMPLE_PIN_NUM_BK_LIGHT,
    .speed_mode = LEDC_LOW_SPEED_MODE,
    .channel =  LEDC_CHANNEL_1,
    .intr_type =  LEDC_INTR_DISABLE,
    .timer_sel = LEDC_TIMER_3,
    .duty = duty,
    .hpoint = 0,
  };
  ESP_ERROR_CHECK_WITHOUT_ABORT(ledc_timer_config(&timer_conf));
  ESP_ERROR_CHECK_WITHOUT_ABORT(ledc_channel_config(&ledc_conf));
}

void setUpduty(uint16_t duty)
{
  ESP_ERROR_CHECK_WITHOUT_ABORT(ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, duty));
  ESP_ERROR_CHECK_WITHOUT_ABORT(ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1));
}
```

## components/lcd_bl_pwm_bsp/lcd_bl_pwm_bsp.h

```c
#ifndef LCD_BL_PWM_BSP_H
#define LCD_BL_PWM_BSP_H

#define  LCD_PWM_MODE_0   (0xff-0)
#define  LCD_PWM_MODE_25  (0xff-25)
#define  LCD_PWM_MODE_50  (0xff-50)
#define  LCD_PWM_MODE_75  (0xff-75)
#define  LCD_PWM_MODE_100 (0xff-100)
#define  LCD_PWM_MODE_125 (0xff-125)
#define  LCD_PWM_MODE_150 (0xff-150)
#define  LCD_PWM_MODE_175 (0xff-175)
#define  LCD_PWM_MODE_200 (0xff-200)
#define  LCD_PWM_MODE_225 (0xff-225)
#define  LCD_PWM_MODE_255 (0xff-255)

#ifdef __cplusplus
extern "C" {
#endif

void lcd_bl_pwm_bsp_init(uint16_t duty);
void setUpduty(uint16_t duty);

#ifdef __cplusplus
}
#endif

#endif
```

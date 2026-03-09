#include "login_app.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_err.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "lvgl.h"
LV_FONT_DECLARE(font_24_cn);
#include "app_network.h"
#include "user_config.h"

static const char *TAG = "login_app";


/* UI objects */
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

/* Input state */
typedef enum
{
    INPUT_TARGET_ACCOUNT = 0,
    INPUT_TARGET_PASSWORD,
} input_target_t;

static input_target_t s_active_target;

/* Buffers */
static char s_account_buf[64];
static char s_password_buf[64];

/* Roller state */
static int s_current_digit;

static inline void set_cn_label_font(lv_obj_t *o)
{
    if (o)
    {
        lv_obj_set_style_text_font(o, &font_24_cn, LV_PART_MAIN);
    }
}

static void login_app_update_status(const char *text)
{
    if (s_status_label && text)
    {
        lv_label_set_text(s_status_label, text);
        set_cn_label_font(s_status_label);
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
    /* 这个判断其实可以省略，但保留也无妨 */
    if (lv_event_get_code(e) != LV_EVENT_SCROLL_END)
    {
        return;
    }

    lv_obj_t *roller = lv_event_get_target(e);

    /* 获取 LVGL 计算好的“最近项” */
    uint16_t selected = lv_roller_get_selected(roller);

    /* 强制对齐到该项：不动画，避免二次滑动感 */
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
        if (len + 1 >= sizeof(s_account_buf))
        {
            return;
        }
        s_account_buf[len] = digit_char;
        s_account_buf[len + 1] = '\0';
        lv_textarea_set_text(s_account_ta, s_account_buf);
    }
    else
    {
        size_t len = strlen(s_password_buf);
        if (len + 1 >= sizeof(s_password_buf))
        {
            return;
        }
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
                                   int *out_status,
                                   char *out_token,
                                   size_t out_token_size)
{
    if (!username || !password || !out_status || !out_token || out_token_size == 0)
    {
        return ESP_ERR_INVALID_ARG;
    }

    *out_status = 0;
    out_token[0] = '\0';

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
    int payload_len = (int)strlen(payload);

    esp_err_t err = esp_http_client_open(client, payload_len);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "HTTP open failed: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return err;
    }

    int written = esp_http_client_write(client, payload, payload_len);
    if (written != payload_len)
    {
        ESP_LOGE(TAG, "HTTP write failed, written=%d expect=%d", written, payload_len);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_FAIL;
    }

    int content_length = esp_http_client_fetch_headers(client);
    int status = esp_http_client_get_status_code(client);
    *out_status = status;

    int capacity = (content_length > 0) ? (content_length + 1) : 1024;
    if (capacity < 256)
    {
        capacity = 256;
    }

    char *body = malloc(capacity);
    if (!body)
    {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_ERR_NO_MEM;
    }

    int total_read = 0;
    while (1)
    {
        if (total_read + 1 >= capacity)
        {
            int new_capacity = capacity * 2;
            char *grown = realloc(body, new_capacity);
            if (!grown)
            {
                free(body);
                esp_http_client_close(client);
                esp_http_client_cleanup(client);
                return ESP_ERR_NO_MEM;
            }
            body = grown;
            capacity = new_capacity;
        }

        int read_len = esp_http_client_read(client, body + total_read, capacity - total_read - 1);
        if (read_len < 0)
        {
            free(body);
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            return ESP_FAIL;
        }
        if (read_len == 0)
        {
            break;
        }
        total_read += read_len;
    }
    body[total_read] = '\0';

    if (status >= 200 && status < 300 && total_read > 0)
    {
        cJSON *root = cJSON_Parse(body);
        if (root)
        {
            cJSON *token_js = cJSON_GetObjectItemCaseSensitive(root, "token");
            if (!cJSON_IsString(token_js))
            {
                token_js = cJSON_GetObjectItemCaseSensitive(root, "access_token");
            }
            if (!cJSON_IsString(token_js))
            {
                token_js = cJSON_GetObjectItemCaseSensitive(root, "accessToken");
            }

            cJSON *data_js = cJSON_GetObjectItemCaseSensitive(root, "data");
            if (!cJSON_IsString(token_js) && cJSON_IsObject(data_js))
            {
                token_js = cJSON_GetObjectItemCaseSensitive(data_js, "token");
                if (!cJSON_IsString(token_js))
                {
                    token_js = cJSON_GetObjectItemCaseSensitive(data_js, "access_token");
                }
                if (!cJSON_IsString(token_js))
                {
                    token_js = cJSON_GetObjectItemCaseSensitive(data_js, "accessToken");
                }
            }

            if (cJSON_IsString(token_js) && token_js->valuestring && token_js->valuestring[0] != '\0')
            {
                strncpy(out_token, token_js->valuestring, out_token_size - 1);
                out_token[out_token_size - 1] = '\0';
            }
            cJSON_Delete(root);
        }
    }

    if (out_token[0] == '\0')
    {
        char *auth_header = NULL;
        if (esp_http_client_get_header(client, "Authorization", &auth_header) == ESP_OK && auth_header && auth_header[0] != '\0')
        {
            if (strncmp(auth_header, "Bearer ", 7) == 0)
            {
                auth_header += 7;
            }
            strncpy(out_token, auth_header, out_token_size - 1);
            out_token[out_token_size - 1] = '\0';
        }
    }

    if (out_token[0] == '\0')
    {
        char *x_token = NULL;
        if (esp_http_client_get_header(client, "X-Access-Token", &x_token) == ESP_OK && x_token && x_token[0] != '\0')
        {
            strncpy(out_token, x_token, out_token_size - 1);
            out_token[out_token_size - 1] = '\0';
        }
    }

    ESP_LOGI(TAG, "Login HTTP status=%d body_len=%d token=%s", status, total_read, out_token[0] ? "yes" : "no");

    free(body);
    esp_http_client_close(client);
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
        login_app_update_status("账号或密码为空");
        return;
    }

    login_app_set_loading(true);
    login_app_update_status("正在登录...");

    int http_status = 0;
    char token[192] = {0};
    esp_err_t err = login_app_do_auth(account, password, &http_status, token, sizeof(token));

    login_app_set_loading(false);

    if (err != ESP_OK)
    {
        login_app_update_status("网络请求失败");
        if (s_result_cb)
        {
            s_result_cb(false, NULL, NULL);
        }
        return;
    }

    if (http_status >= 200 && http_status < 300)
    {
        if (token[0] == '\0')
        {
            login_app_update_status("登录成功，但凭证缺失");
            ESP_LOGE(TAG, "Login succeeded but token is empty");
            if (s_result_cb)
            {
                s_result_cb(false, NULL, NULL);
            }
            return;
        }

        login_app_update_status("登录成功");
        if (s_result_cb)
        {
            s_result_cb(true, token, account);
        }
    }
    else if (http_status == 401)
    {
        login_app_update_status("账号或密码错误");
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
        login_app_update_status("登录失败");
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
    lv_textarea_set_placeholder_text(s_account_ta, "账号");
    lv_obj_set_style_text_font(s_account_ta, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_set_style_text_font(s_account_ta, &font_24_cn, LV_PART_TEXTAREA_PLACEHOLDER);
    lv_obj_clear_flag(s_account_ta, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(s_account_ta, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_height(s_account_ta, 48);
    lv_obj_set_width(s_account_ta, lv_pct(92));
    lv_obj_add_event_cb(s_account_ta, login_app_textarea_select_event, LV_EVENT_CLICKED, NULL);

    s_password_ta = lv_textarea_create(left);
    lv_textarea_set_one_line(s_password_ta, true);
    lv_textarea_set_password_mode(s_password_ta, true);
    lv_textarea_set_placeholder_text(s_password_ta, "密码");
    lv_obj_set_style_text_font(s_password_ta, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_set_style_text_font(s_password_ta, &font_24_cn, LV_PART_TEXTAREA_PLACEHOLDER);
    lv_obj_clear_flag(s_password_ta, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(s_password_ta, LV_SCROLLBAR_MODE_OFF);
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

    /* ===== Appearance ===== */
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

    /* ===== Data & mode ===== */
    lv_roller_set_options(s_digit_roller,
                          "0\n1\n2\n3\n4\n5\n6\n7\n8\n9",
                          LV_ROLLER_MODE_INFINITE);
    lv_roller_set_visible_row_count(s_digit_roller, 3);
    lv_roller_set_selected(s_digit_roller, 0, LV_ANIM_OFF);

    /* Disable elastic to avoid bounce */
    lv_obj_clear_flag(s_digit_roller, LV_OBJ_FLAG_SCROLL_ELASTIC);

    /* ===== Events ===== */
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
    lv_obj_t *ok_label = lv_label_create(s_ok_btn);
    lv_label_set_text(ok_label, "确认");
    set_cn_label_font(ok_label);

    s_del_btn = lv_btn_create(right);
    lv_obj_add_event_cb(s_del_btn, login_app_del_event, LV_EVENT_CLICKED, NULL);
    lv_obj_t *del_label = lv_label_create(s_del_btn);
    lv_label_set_text(del_label, "删除");
    set_cn_label_font(del_label);

    s_signin_btn = lv_btn_create(right);
    lv_obj_add_event_cb(s_signin_btn, login_app_handle_submit, LV_EVENT_CLICKED, NULL);
    lv_obj_t *signin_label = lv_label_create(s_signin_btn);
    lv_label_set_text(signin_label, "登录");
    set_cn_label_font(signin_label);
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
    lv_label_set_text(s_status_label, "输入账号和密码");
    set_cn_label_font(s_status_label);
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
    login_app_update_status("输入账号和密码");
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

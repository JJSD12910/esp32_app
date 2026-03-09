#include "quiz_app.h"

#include <ctype.h>
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
LV_FONT_DECLARE(font_24_cn);

/* ================= UI Font Aliases (Theme-based) ================= */
#define UI_FONT_LARGE   lv_theme_get_font_large(NULL)
#define UI_FONT_NORMAL  lv_theme_get_font_normal(NULL)
#define UI_FONT_SMALL   lv_theme_get_font_small(NULL)

/* Answer area font: prefer 20, fallback to 22, then theme large. */
#if defined(LV_FONT_MONTSERRAT_20) && LV_FONT_MONTSERRAT_20
#define QUIZ_ANSWER_FONT (&lv_font_montserrat_20)
#elif defined(LV_FONT_MONTSERRAT_22) && LV_FONT_MONTSERRAT_22
#define QUIZ_ANSWER_FONT (&lv_font_montserrat_22)
#else
#define QUIZ_ANSWER_FONT UI_FONT_LARGE
#endif

/* ================= Option highlight colors ================= */
#define OPTION_TEXT_NORMAL_COLOR   lv_color_hex(0x333333)
#define OPTION_TEXT_ACTIVE_COLOR   lv_palette_main(LV_PALETTE_BLUE)

#define QUIZ_MAX_QUESTIONS     12
#define QUIZ_OPTION_COUNT      4
#define QUIZ_TEXT_LEN          192
#define QUIZ_OPTION_LEN        96
#define QUIZ_ID_LEN            64

typedef struct
{
    char id[QUIZ_ID_LEN];
    int correct;
    int your;
} quiz_wrong_item_t;

typedef struct
{
    char id[QUIZ_ID_LEN];
    char stem[QUIZ_TEXT_LEN];
    char options[QUIZ_OPTION_COUNT][QUIZ_OPTION_LEN];
    uint8_t correct_index;
} quiz_question_t;

typedef struct
{
    quiz_question_t questions[QUIZ_MAX_QUESTIONS];
    uint8_t answers[QUIZ_MAX_QUESTIONS];
    uint8_t submitted_answers[QUIZ_MAX_QUESTIONS];
    uint8_t question_count;
    uint8_t current_question;
    bool questions_ready;
    bool test_finished;
    bool submit_inflight;
    bool final_submit_success;
<<<<<<< HEAD
    TickType_t attempt_started_tick;
=======
>>>>>>> 0007b3115549354c5d21c3818b49bc74d75d798e
    int server_score;
    int server_total;
    uint8_t server_wrong_count;
    quiz_wrong_item_t server_wrong[QUIZ_MAX_QUESTIONS];
} quiz_state_t;

static const char *TAG = "quiz_app";

static quiz_state_t s_state;
static char s_user_id[16];
static char s_quiz_id[40];
static char s_attempt_id[40];
static char s_auth_token[192];

/* ================= Test Screen UI ================= */
static lv_obj_t *s_home_screen;
static lv_obj_t *s_result_screen;
static lv_obj_t *s_test_screen;

/* scroll content */
static lv_obj_t *s_scroll;
/* Result Screen scroll content */
static lv_obj_t *s_result_scroll;
static lv_obj_t *s_progress_label;
static lv_obj_t *s_question_label;
static lv_obj_t *s_option_labels[QUIZ_OPTION_COUNT];

/* bottom buttons */
static lv_obj_t *s_bottom_bar;
static lv_obj_t *s_option_btns[QUIZ_OPTION_COUNT];
static lv_obj_t *s_submit_btn;
static lv_obj_t *s_submit_label;

/* toast */
static lv_obj_t *s_toast_label;
static lv_timer_t *s_toast_timer;

static void quiz_handle_download(lv_event_t *e);
static void quiz_handle_start_test(lv_event_t *e);
static void quiz_handle_view_results(lv_event_t *e);
static void quiz_handle_option(lv_event_t *e);
static void quiz_handle_submit(lv_event_t *e);
static void quiz_hide_toast_cb(lv_timer_t *t);
static void quiz_back_to_home(lv_event_t *e);
static esp_err_t quiz_http_get_questions(char **out_json, int *out_status, char *out_reason, size_t out_reason_size);
static esp_err_t quiz_http_post_single_answer(const char *payload, int *out_status, char *out_reason, size_t out_reason_size);
static esp_err_t quiz_http_post_results(const char *payload, int *out_status, char *out_reason, size_t out_reason_size);
static esp_err_t quiz_http_request(const char *path,
                                   esp_http_client_method_t method,
                                   const char *auth_token,
                                   const char *payload,
                                   int *out_status,
                                   char **out_body);
static bool quiz_pick_first_exam_id(const char *json, char *exam_id, size_t exam_id_size);
static bool quiz_extract_error_reason(const char *json, char *out_reason, size_t out_reason_size);
static char *quiz_build_single_answer_payload(uint8_t question_index, bool is_final_question);
static char *quiz_build_submit_payload(void);
<<<<<<< HEAD
static uint32_t quiz_get_elapsed_duration_sec(void);
=======
>>>>>>> 0007b3115549354c5d21c3818b49bc74d75d798e
static bool quiz_submit_question_answer(uint8_t question_index, bool is_final_question);
static void quiz_set_submit_loading(bool loading);
static void quiz_update_submit_button_text(uint8_t index);
static void quiz_prepare_local_result(void);
static void quiz_load_question(uint8_t index);
static void quiz_build_result_screen(void);
static void quiz_show_results_screen(void);
static void quiz_finish_and_upload(void);
static void quiz_reset_server_result(void);
static const quiz_question_t *quiz_find_question_by_id(const char *id);
static void quiz_add_wrong_row(uint8_t qno, char your_c, char ans_c);

static inline void quiz_set_cn_font_for_label(lv_obj_t *obj)
{
    if (obj)
    {
        lv_obj_set_style_text_font(obj, &font_24_cn, LV_PART_MAIN);
    }
}

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
        lv_obj_set_style_text_font(s_toast_label, &font_24_cn, LV_PART_MAIN);
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

static void quiz_show_toast_cn(const char *text, uint32_t duration_ms)
{
    quiz_show_toast(text, duration_ms);
    quiz_set_cn_font_for_label(s_toast_label);
}

static void quiz_set_submit_loading(bool loading)
{
<<<<<<< HEAD
    // Avoid LVGL disabled-state transitions here. On this target/theme they can
    // trigger transition animation crashes when toggled around blocking network IO.
    s_state.submit_inflight = loading;
=======
    s_state.submit_inflight = loading;

    if (s_submit_btn)
    {
        if (loading)
        {
            lv_obj_add_state(s_submit_btn, LV_STATE_DISABLED);
        }
        else
        {
            lv_obj_clear_state(s_submit_btn, LV_STATE_DISABLED);
        }
    }

    for (int i = 0; i < QUIZ_OPTION_COUNT; i++)
    {
        if (!s_option_btns[i])
        {
            continue;
        }

        if (loading)
        {
            lv_obj_add_state(s_option_btns[i], LV_STATE_DISABLED);
        }
        else
        {
            lv_obj_clear_state(s_option_btns[i], LV_STATE_DISABLED);
        }
    }
>>>>>>> 0007b3115549354c5d21c3818b49bc74d75d798e
}

static void quiz_update_submit_button_text(uint8_t index)
{
    if (!s_submit_label)
    {
        return;
    }

    lv_label_set_text(s_submit_label,
<<<<<<< HEAD
                      (index + 1 >= s_state.question_count) ? "??" : "???");
    quiz_set_cn_font_for_label(s_submit_label);
}

static uint32_t quiz_get_elapsed_duration_sec(void)
{
    if (s_state.attempt_started_tick == 0)
    {
        return 0;
    }

    TickType_t elapsed_ticks = xTaskGetTickCount() - s_state.attempt_started_tick;
    return (uint32_t)(elapsed_ticks / configTICK_RATE_HZ);
=======
                      (index + 1 >= s_state.question_count) ? "Finish" : "Next");
>>>>>>> 0007b3115549354c5d21c3818b49bc74d75d798e
}

static esp_err_t quiz_http_request(const char *path,
                                   esp_http_client_method_t method,
                                   const char *auth_token,
                                   const char *payload,
                                   int *out_status,
                                   char **out_body)
{
    if (!path || !out_status || !out_body)
    {
        return ESP_ERR_INVALID_ARG;
    }

    if (!app_network_wait_for_wifi(pdMS_TO_TICKS(10000)))
    {
        return ESP_ERR_TIMEOUT;
    }

    *out_status = 0;
    *out_body = NULL;

    esp_http_client_config_t config = {
        .host = APP_SERVER_HOST,
        .port = APP_SERVER_PORT,
        .path = path,
        .transport_type = HTTP_TRANSPORT_OVER_TCP,
        .timeout_ms = 12000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client)
    {
        return ESP_FAIL;
    }

    esp_http_client_set_method(client, method);

    if (auth_token && auth_token[0] != '\0')
    {
        char auth_header[224];
        snprintf(auth_header, sizeof(auth_header), "Bearer %s", auth_token);
        esp_http_client_set_header(client, "Authorization", auth_header);
    }

    int post_len = 0;
    if (payload)
    {
        esp_http_client_set_header(client, "Content-Type", "application/json");
        post_len = (int)strlen(payload);
    }

    esp_err_t err = esp_http_client_open(client, post_len);
    if (err != ESP_OK)
    {
        esp_http_client_cleanup(client);
        return err;
    }

    if (post_len > 0)
    {
        int written = esp_http_client_write(client, payload, post_len);
        if (written != post_len)
        {
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            return ESP_FAIL;
        }
    }

    int content_length = esp_http_client_fetch_headers(client);
    *out_status = esp_http_client_get_status_code(client);

    int capacity = (content_length > 0) ? (content_length + 1) : 1024;
    if (capacity < 256)
    {
        capacity = 256;
    }

    char *buffer = malloc(capacity);
    if (!buffer)
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
            char *grown = realloc(buffer, new_capacity);
            if (!grown)
            {
                free(buffer);
                esp_http_client_close(client);
                esp_http_client_cleanup(client);
                return ESP_ERR_NO_MEM;
            }
            buffer = grown;
            capacity = new_capacity;
        }

        int read_len = esp_http_client_read(client, buffer + total_read, capacity - total_read - 1);
        if (read_len < 0)
        {
            free(buffer);
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

    buffer[total_read] = '\0';
    *out_body = buffer;

    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return ESP_OK;
}

static bool quiz_extract_error_reason(const char *json, char *out_reason, size_t out_reason_size)
{
    if (!out_reason || out_reason_size == 0)
    {
        return false;
    }

    out_reason[0] = '\0';
    if (!json || json[0] == '\0')
    {
        return false;
    }

    cJSON *root = cJSON_Parse(json);
    if (!root)
    {
        return false;
    }

    const cJSON *msg = cJSON_GetObjectItemCaseSensitive(root, "message");
    const cJSON *reason = cJSON_GetObjectItemCaseSensitive(root, "reason");
    const cJSON *data = cJSON_GetObjectItemCaseSensitive(root, "data");
    if (!cJSON_IsString(reason) && cJSON_IsObject(data))
    {
        reason = cJSON_GetObjectItemCaseSensitive(data, "reason");
    }

    const cJSON *chosen = cJSON_IsString(msg) ? msg : reason;
    if (cJSON_IsString(chosen) && chosen->valuestring && chosen->valuestring[0] != '\0')
    {
        strncpy(out_reason, chosen->valuestring, out_reason_size - 1);
        out_reason[out_reason_size - 1] = '\0';
        cJSON_Delete(root);
        return true;
    }

    cJSON_Delete(root);
    return false;
}

static bool quiz_pick_first_exam_id(const char *json, char *exam_id, size_t exam_id_size)
{
    if (!json || !exam_id || exam_id_size == 0)
    {
        return false;
    }

    exam_id[0] = '\0';

    cJSON *root = cJSON_Parse(json);
    if (!root)
    {
        return false;
    }

    cJSON *data = cJSON_GetObjectItemCaseSensitive(root, "data");
    cJSON *items = cJSON_IsObject(data) ? cJSON_GetObjectItemCaseSensitive(data, "items") : NULL;
    if (!cJSON_IsArray(items) || cJSON_GetArraySize(items) <= 0)
    {
        cJSON_Delete(root);
        return false;
    }

    cJSON *first = cJSON_GetArrayItem(items, 0);
    cJSON *exam_id_js = cJSON_IsObject(first) ? cJSON_GetObjectItemCaseSensitive(first, "exam_id") : NULL;
    if (!cJSON_IsString(exam_id_js) && !cJSON_IsNumber(exam_id_js) && cJSON_IsObject(first))
    {
        exam_id_js = cJSON_GetObjectItemCaseSensitive(first, "id");
    }

    if (!cJSON_IsString(exam_id_js) && !cJSON_IsNumber(exam_id_js))
    {
        cJSON_Delete(root);
        return false;
    }

    if (cJSON_IsString(exam_id_js) && exam_id_js->valuestring && exam_id_js->valuestring[0] != '\0')
    {
        strncpy(exam_id, exam_id_js->valuestring, exam_id_size - 1);
    }
    else if (cJSON_IsNumber(exam_id_js))
    {
        snprintf(exam_id, exam_id_size, "%.0f", exam_id_js->valuedouble);
    }
    else
    {
        cJSON_Delete(root);
        return false;
    }
    exam_id[exam_id_size - 1] = '\0';
    cJSON_Delete(root);
    return true;
}

static esp_err_t quiz_http_get_questions(char **out_json, int *out_status, char *out_reason, size_t out_reason_size)
{
    if (!out_json || !out_status || !out_reason || out_reason_size == 0)
    {
        return ESP_ERR_INVALID_ARG;
    }

    *out_json = NULL;
    *out_status = 0;
    out_reason[0] = '\0';

    if (s_auth_token[0] == '\0')
    {
        *out_status = 401;
        strncpy(out_reason, "????????????", out_reason_size - 1);
        out_reason[out_reason_size - 1] = '\0';
        return ESP_ERR_INVALID_STATE;
    }

    char *list_json = NULL;
    int list_status = 0;
    esp_err_t err = quiz_http_request("/api/client/exams?limit=20&offset=0",
                                      HTTP_METHOD_GET,
                                      s_auth_token,
                                      NULL,
                                      &list_status,
                                      &list_json);
    if (err != ESP_OK)
    {
        return err;
    }
    ESP_LOGI(TAG, "Exam list status=%d", list_status);

    if (list_status < 200 || list_status >= 300)
    {
        *out_status = list_status;
        if (!quiz_extract_error_reason(list_json, out_reason, out_reason_size))
        {
            strncpy(out_reason, "????", out_reason_size - 1);
            out_reason[out_reason_size - 1] = '\0';
        }
        free(list_json);
        return ESP_FAIL;
    }

    char exam_id[40];
    if (!quiz_pick_first_exam_id(list_json, exam_id, sizeof(exam_id)))
    {
        *out_status = 404;
        strncpy(out_reason, "???????????", out_reason_size - 1);
        out_reason[out_reason_size - 1] = '\0';
        free(list_json);
        return ESP_FAIL;
    }
    free(list_json);

    strncpy(s_quiz_id, exam_id, sizeof(s_quiz_id) - 1);
    s_quiz_id[sizeof(s_quiz_id) - 1] = '\0';
    s_attempt_id[0] = '\0';

    char start_path[128];
    snprintf(start_path, sizeof(start_path), "/api/client/exams/%s/start", exam_id);

    char *start_json = NULL;
    int start_status = 0;
    err = quiz_http_request(start_path,
                            HTTP_METHOD_POST,
                            s_auth_token,
                            "{}",
                            &start_status,
                            &start_json);
    if (err != ESP_OK)
    {
        return err;
    }
    ESP_LOGI(TAG, "Exam start status=%d exam_id=%s", start_status, exam_id);

    *out_status = start_status;
    if (start_status < 200 || start_status >= 300)
    {
        if (!quiz_extract_error_reason(start_json, out_reason, out_reason_size))
        {
            strncpy(out_reason, "????", out_reason_size - 1);
            out_reason[out_reason_size - 1] = '\0';
        }
        free(start_json);
        return ESP_FAIL;
    }

    *out_json = start_json;
    return ESP_OK;
}

static esp_err_t quiz_http_post_single_answer(const char *payload, int *out_status, char *out_reason, size_t out_reason_size)
{
    if (!payload || !out_status || !out_reason || out_reason_size == 0)
    {
        return ESP_ERR_INVALID_ARG;
    }
    *out_status = 0;
    out_reason[0] = '\0';

    if (s_auth_token[0] == '\0')
    {
        *out_status = 401;
<<<<<<< HEAD
        strncpy(out_reason, "????????????", out_reason_size - 1);
=======
        strncpy(out_reason, "Login token missing, please log in again", out_reason_size - 1);
>>>>>>> 0007b3115549354c5d21c3818b49bc74d75d798e
        out_reason[out_reason_size - 1] = '\0';
        return ESP_ERR_INVALID_STATE;
    }

    if (s_attempt_id[0] == '\0')
    {
        *out_status = 404;
<<<<<<< HEAD
        strncpy(out_reason, "????????????", out_reason_size - 1);
=======
        strncpy(out_reason, "Attempt id missing, please download again", out_reason_size - 1);
>>>>>>> 0007b3115549354c5d21c3818b49bc74d75d798e
        out_reason[out_reason_size - 1] = '\0';
        return ESP_ERR_INVALID_STATE;
    }

    char answer_path[160];
    snprintf(answer_path, sizeof(answer_path), APP_API_ATTEMPT_ANSWER_PATH_FMT, s_attempt_id);

    char *resp_json = NULL;
    esp_err_t err = quiz_http_request(answer_path,
                                      HTTP_METHOD_POST,
                                      s_auth_token,
                                      payload,
                                      out_status,
                                      &resp_json);
    if (err != ESP_OK)
    {
        return err;
    }

    ESP_LOGI(TAG, "Single answer submit status=%d attempt_id=%s", *out_status, s_attempt_id);
    if (*out_status < 200 || *out_status >= 300)
    {
        if (!quiz_extract_error_reason(resp_json, out_reason, out_reason_size))
        {
            if (resp_json && resp_json[0] != '\0')
            {
                strncpy(out_reason, resp_json, out_reason_size - 1);
                out_reason[out_reason_size - 1] = '\0';
            }
            else
            {
<<<<<<< HEAD
                strncpy(out_reason, "??????", out_reason_size - 1);
=======
                strncpy(out_reason, "Single answer submit failed", out_reason_size - 1);
>>>>>>> 0007b3115549354c5d21c3818b49bc74d75d798e
                out_reason[out_reason_size - 1] = '\0';
            }
        }
        free(resp_json);
        return ESP_FAIL;
    }

    free(resp_json);
    return ESP_OK;
}

static esp_err_t quiz_http_post_results(const char *payload, int *out_status, char *out_reason, size_t out_reason_size)
{
    if (!payload || !out_status || !out_reason || out_reason_size == 0)
    {
        return ESP_ERR_INVALID_ARG;
    }
    *out_status = 0;
    out_reason[0] = '\0';

    if (s_auth_token[0] == '\0')
    {
        *out_status = 401;
        strncpy(out_reason, "????????????", out_reason_size - 1);
        out_reason[out_reason_size - 1] = '\0';
        return ESP_ERR_INVALID_STATE;
    }

    if (s_attempt_id[0] == '\0')
    {
        *out_status = 404;
        strncpy(out_reason, "????????????", out_reason_size - 1);
        out_reason[out_reason_size - 1] = '\0';
        return ESP_ERR_INVALID_STATE;
    }

    char submit_path[160];
    snprintf(submit_path, sizeof(submit_path), "/api/client/attempts/%s/submit", s_attempt_id);

    char *resp_json = NULL;
    esp_err_t err = quiz_http_request(submit_path,
                                      HTTP_METHOD_POST,
                                      s_auth_token,
                                      payload,
                                      out_status,
                                      &resp_json);
    if (err != ESP_OK)
    {
        return err;
    }

    ESP_LOGI(TAG, "Submit status=%d attempt_id=%s", *out_status, s_attempt_id);
    if (*out_status < 200 || *out_status >= 300)
    {
        if (!quiz_extract_error_reason(resp_json, out_reason, out_reason_size))
        {
            if (resp_json && resp_json[0] != '\0')
            {
                strncpy(out_reason, resp_json, out_reason_size - 1);
                out_reason[out_reason_size - 1] = '\0';
            }
            else
            {
                strncpy(out_reason, "Upload failed", out_reason_size - 1);
                out_reason[out_reason_size - 1] = '\0';
            }
        }
        free(resp_json);
        return ESP_FAIL;
    }

    if (resp_json && resp_json[0] != '\0')
    {
        cJSON *root = cJSON_Parse(resp_json);
        if (root)
        {
            cJSON *data = cJSON_GetObjectItemCaseSensitive(root, "data");
            if (cJSON_IsObject(data))
            {
                cJSON *score_js = cJSON_GetObjectItemCaseSensitive(data, "score");
                cJSON *total_js = cJSON_GetObjectItemCaseSensitive(data, "total");
                if (cJSON_IsNumber(score_js))
                {
                    s_state.server_score = score_js->valueint;
                }
                if (cJSON_IsNumber(total_js))
                {
                    s_state.server_total = total_js->valueint;
                }
            }
            cJSON_Delete(root);
        }
    }

    free(resp_json);
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

    s_attempt_id[0] = '\0';

    cJSON *questions = NULL;
    cJSON *data = cJSON_GetObjectItemCaseSensitive(root, "data");

    if (cJSON_IsObject(data))
    {
        cJSON *exam_id_js = cJSON_GetObjectItemCaseSensitive(data, "exam_id");
        cJSON *attempt_id_js = cJSON_GetObjectItemCaseSensitive(data, "attempt_id");
        if (cJSON_IsString(exam_id_js) && exam_id_js->valuestring)
        {
            strncpy(s_quiz_id, exam_id_js->valuestring, sizeof(s_quiz_id) - 1);
            s_quiz_id[sizeof(s_quiz_id) - 1] = '\0';
        }
        if (cJSON_IsString(attempt_id_js) && attempt_id_js->valuestring)
        {
            strncpy(s_attempt_id, attempt_id_js->valuestring, sizeof(s_attempt_id) - 1);
            s_attempt_id[sizeof(s_attempt_id) - 1] = '\0';
        }

        questions = cJSON_GetObjectItemCaseSensitive(data, "items");
    }

    if (!cJSON_IsArray(questions))
    {
        cJSON *quiz_id_js = cJSON_GetObjectItemCaseSensitive(root, "quiz_id");
        if (cJSON_IsString(quiz_id_js) && quiz_id_js->valuestring)
        {
            strncpy(s_quiz_id, quiz_id_js->valuestring, sizeof(s_quiz_id) - 1);
            s_quiz_id[sizeof(s_quiz_id) - 1] = '\0';
        }
        questions = cJSON_GetObjectItemCaseSensitive(root, "questions");
    }

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
        if (!cJSON_IsString(id))
        {
            id = cJSON_GetObjectItemCaseSensitive(item, "question_id");
        }

        cJSON *stem = cJSON_GetObjectItemCaseSensitive(item, "stem");
        if (!cJSON_IsString(stem))
        {
            stem = cJSON_GetObjectItemCaseSensitive(item, "question");
        }
        if (!cJSON_IsString(stem))
        {
            stem = cJSON_GetObjectItemCaseSensitive(item, "content");
        }

        cJSON *options = cJSON_GetObjectItemCaseSensitive(item, "options");
        if (!cJSON_IsArray(options))
        {
            options = cJSON_GetObjectItemCaseSensitive(item, "choices");
        }

        cJSON *answer = cJSON_GetObjectItemCaseSensitive(item, "answer");
        if (!cJSON_IsNumber(answer) && !cJSON_IsString(answer))
        {
            answer = cJSON_GetObjectItemCaseSensitive(item, "answer_index");
        }
        if (!cJSON_IsNumber(answer) && !cJSON_IsString(answer))
        {
            answer = cJSON_GetObjectItemCaseSensitive(item, "correct_index");
        }

        if (!cJSON_IsArray(options) || cJSON_GetArraySize(options) < QUIZ_OPTION_COUNT)
        {
            ESP_LOGW(TAG, "Skip malformed question");
            continue;
        }

        quiz_question_t *q = &s_state.questions[s_state.question_count];
        strncpy(q->id, cJSON_IsString(id) ? id->valuestring : "NA", sizeof(q->id) - 1);
        q->id[sizeof(q->id) - 1] = '\0';
        strncpy(q->stem, cJSON_IsString(stem) ? stem->valuestring : "Untitled question", sizeof(q->stem) - 1);
        q->stem[sizeof(q->stem) - 1] = '\0';

        for (int i = 0; i < QUIZ_OPTION_COUNT; i++)
        {
            cJSON *opt = cJSON_GetArrayItem(options, i);
            const char *opt_text = "N/A";
            if (cJSON_IsString(opt) && opt->valuestring)
            {
                opt_text = opt->valuestring;
            }
            else if (cJSON_IsObject(opt))
            {
                cJSON *opt_str = cJSON_GetObjectItemCaseSensitive(opt, "text");
                if (!cJSON_IsString(opt_str))
                {
                    opt_str = cJSON_GetObjectItemCaseSensitive(opt, "content");
                }
                if (cJSON_IsString(opt_str) && opt_str->valuestring)
                {
                    opt_text = opt_str->valuestring;
                }
            }
            strncpy(q->options[i], opt_text, sizeof(q->options[i]) - 1);
            q->options[i][sizeof(q->options[i]) - 1] = '\0';
        }

        int answer_index = 0;
        if (cJSON_IsNumber(answer))
        {
            answer_index = answer->valueint;
        }
        else if (cJSON_IsString(answer) && answer->valuestring && answer->valuestring[0] != '\0')
        {
            char c = (char)toupper((unsigned char)answer->valuestring[0]);
            if (c >= 'A' && c <= 'D')
            {
                answer_index = c - 'A';
            }
        }

        q->correct_index = (uint8_t)answer_index;
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
    s_state.submit_inflight = false;
    s_state.final_submit_success = false;
    memset(s_state.answers, 0xFF, sizeof(s_state.answers));
    memset(s_state.submitted_answers, 0xFF, sizeof(s_state.submitted_answers));
    ESP_LOGI(TAG, "Loaded %d questions", s_state.question_count);
    return s_state.questions_ready;
}

static bool quiz_download_questions(void)
{
    char *json = NULL;
    int http_status = 0;
    char reason[128] = {0};
    esp_err_t err = quiz_http_get_questions(&json, &http_status, reason, sizeof(reason));
    if (err != ESP_OK || !json)
    {
        ESP_LOGE(TAG, "Download request failed: err=%s status=%d reason=%s",
                 esp_err_to_name(err), http_status, reason);
        if (http_status == 401)
        {
            quiz_show_toast_cn("???????????", 2200);
        }
        else if (http_status == 403)
        {
            quiz_show_toast_cn("???????", 2200);
        }
        else if (http_status == 404)
        {
            quiz_show_toast_cn("???????????", 2200);
        }
        else if (http_status == 409)
        {
            quiz_show_toast_cn("??????????", 2200);
        }
        else if (reason[0] != '\0')
        {
            quiz_show_toast_cn(reason, 2200);
        }
        else
        {
            quiz_show_toast_cn("????", 1800);
        }
        return false;
    }

    // Log the raw JSON to help diagnose server data issues
    ESP_LOGW(TAG, "RAW JSON RECEIVED: %s", json);

    bool ok = quiz_parse_questions(json);
    free(json);

    if (!ok)
    {
        ESP_LOGE(TAG, "Parse JSON failed, abort UI");
        quiz_show_toast_cn("????", 1800);
        return false;  // Block UI flow on failure
    }

    return true;
}

static char *quiz_build_single_answer_payload(uint8_t question_index, bool is_final_question)
{
<<<<<<< HEAD
    LV_UNUSED(is_final_question);

=======
>>>>>>> 0007b3115549354c5d21c3818b49bc74d75d798e
    if (question_index >= s_state.question_count)
    {
        return NULL;
    }

    int your_choice = (s_state.answers[question_index] < QUIZ_OPTION_COUNT) ? (int)s_state.answers[question_index] : -1;
    if (your_choice < 0)
    {
        return NULL;
    }

    cJSON *root = cJSON_CreateObject();
    if (!root)
    {
        return NULL;
    }

    cJSON_AddStringToObject(root, "question_id", s_state.questions[question_index].id);
<<<<<<< HEAD
    cJSON_AddNumberToObject(root, "choice", your_choice);
    cJSON_AddNumberToObject(root, "progress_count", question_index + 1);
    cJSON_AddNumberToObject(root, "duration_sec", (double)quiz_get_elapsed_duration_sec());
=======
    cJSON_AddNumberToObject(root, "your", your_choice);
    cJSON_AddNumberToObject(root, "answer_index", your_choice);
    cJSON_AddNumberToObject(root, "question_no", question_index + 1);
    cJSON_AddNumberToObject(root, "total_questions", s_state.question_count);
    cJSON_AddBoolToObject(root, "is_final_question", is_final_question);
>>>>>>> 0007b3115549354c5d21c3818b49bc74d75d798e

    char *payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return payload;
}

static char *quiz_build_submit_payload(void)
{
    cJSON *root = cJSON_CreateObject();
    if (!root)
    {
        return NULL;
    }

    cJSON_AddNumberToObject(root, "duration_sec", (double)quiz_get_elapsed_duration_sec());

    char *payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return payload;
}

static bool quiz_submit_question_answer(uint8_t question_index, bool is_final_question)
{
    if (question_index >= s_state.question_count)
    {
        return false;
    }

    uint8_t answer = s_state.answers[question_index];
    if (answer >= QUIZ_OPTION_COUNT)
    {
        return false;
    }

    if (s_state.submitted_answers[question_index] == answer)
    {
        ESP_LOGI(TAG, "Skip duplicate submit q=%d answer=%d", question_index, answer);
        return true;
    }

    char submit_reason[160] = {0};
    int submit_status = 0;

    for (int attempt = 1; attempt <= APP_SINGLE_ANSWER_RETRY_COUNT; attempt++)
    {
        char *payload = quiz_build_single_answer_payload(question_index, is_final_question);
        if (!payload)
        {
<<<<<<< HEAD
            quiz_show_toast_cn("????", 1800);
=======
            quiz_show_toast("Out of memory", 1800);
>>>>>>> 0007b3115549354c5d21c3818b49bc74d75d798e
            return false;
        }

        esp_err_t err = quiz_http_post_single_answer(payload,
                                                     &submit_status,
                                                     submit_reason,
                                                     sizeof(submit_reason));
        free(payload);

        if (err == ESP_OK)
        {
            s_state.submitted_answers[question_index] = answer;
            if (attempt > 1)
            {
<<<<<<< HEAD
                quiz_show_toast_cn("??????", 1600);
=======
                quiz_show_toast_cn("Saved after retry", 1600);
>>>>>>> 0007b3115549354c5d21c3818b49bc74d75d798e
            }
            return true;
        }

<<<<<<< HEAD
        if (attempt < APP_SINGLE_ANSWER_RETRY_COUNT)
        {
            quiz_show_toast_cn("????...", 1200);
=======
        if (submit_status == 404 || submit_status == 405 || submit_status == 501)
        {
            quiz_show_toast_cn("Single-answer API missing", 2200);
            return false;
        }

        if (attempt < APP_SINGLE_ANSWER_RETRY_COUNT)
        {
            quiz_show_toast_cn("Retrying...", 1200);
>>>>>>> 0007b3115549354c5d21c3818b49bc74d75d798e
            vTaskDelay(pdMS_TO_TICKS(300));
        }
    }

    if (submit_status == 401)
    {
<<<<<<< HEAD
        quiz_show_toast_cn("???????????", 2200);
    }
    else if (submit_status == 403)
    {
        quiz_show_toast_cn("??????", 2200);
    }
    else if (submit_status == 404)
    {
        quiz_show_toast_cn("????????", 2200);
    }
    else if (submit_status == 409)
    {
        quiz_show_toast_cn("?????????", 2200);
    }
    else if (submit_reason[0] != '\0')
    {
        quiz_show_toast_cn(submit_reason, 2200);
    }
    else
    {
        quiz_show_toast_cn("????????", 2200);
=======
        quiz_show_toast("Session expired, please log in again", 2200);
    }
    else if (submit_status == 403)
    {
        quiz_show_toast("No permission to submit answer", 2200);
    }
    else if (submit_reason[0] != '\0')
    {
        quiz_show_toast(submit_reason, 2200);
    }
    else
    {
        quiz_show_toast_cn("Save failed, try again", 2200);
>>>>>>> 0007b3115549354c5d21c3818b49bc74d75d798e
    }

    return false;
}

static void quiz_prepare_local_result(void)
{
    uint8_t correct = 0;
    quiz_reset_server_result();

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
        quiz_show_toast_cn("??????", 2000);
        return;
    }

    s_state.current_question = 0;
    s_state.test_finished = false;
    s_state.submit_inflight = false;
    s_state.final_submit_success = false;
<<<<<<< HEAD
    s_state.attempt_started_tick = xTaskGetTickCount();
=======
>>>>>>> 0007b3115549354c5d21c3818b49bc74d75d798e
    quiz_reset_server_result();
    memset(s_state.answers, 0xFF, sizeof(s_state.answers));
    memset(s_state.submitted_answers, 0xFF, sizeof(s_state.submitted_answers));

    /* switch to test screen first */
    lv_scr_load(s_test_screen);

    /* then populate UI */
    quiz_load_question(s_state.current_question);
}

static void quiz_handle_view_results(lv_event_t *e)
{
    LV_UNUSED(e);
    if (!s_state.test_finished)
    {
        quiz_show_toast_cn("尚未进行测试", 2000);
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

    static const char *btn_texts[3] = {"下载", "开始测试", "查看成绩"};
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
        quiz_set_cn_font_for_label(lbl);
        lv_obj_center(lbl);
    }
}

static void quiz_build_test_screen(void)
{
    /* ========== Root screen (640x172) ========== */
    s_test_screen = lv_obj_create(NULL);
    lv_obj_set_size(s_test_screen, 640, 172);
    lv_obj_clear_flag(s_test_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_test_screen, lv_color_white(), 0);

    /* ========== Single outer scroll container (full screen) ========== */
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

    /* 鍏ㄥ眬闂磋窛锛氭帶鍒堕骞?閫夐」/鎸夐挳鍧椾箣闂寸殑璺濈 */
    lv_obj_set_style_pad_all(s_scroll, 0, 0);
    lv_obj_set_style_pad_gap(s_scroll, 8, 0);          /* 鈫?璁╂暣浣撴洿鑸掑睍涓€鐐?*/
    lv_obj_set_style_bg_opa(s_scroll, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_scroll, 0, 0);

    /* ========== Question row: left (Q + stem), right (progress) ========== */
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
    lv_obj_set_style_pad_bottom(q_row, 2, 0);          /* 鈫?棰樺共鍜岀涓€鏉￠€夐」涔嬮棿鍒尋 */
    lv_obj_set_style_pad_gap(q_row, 8, 0);

    lv_obj_set_style_min_height(q_row, 0, 0);
    lv_obj_clear_flag(q_row, LV_OBJ_FLAG_SCROLLABLE);

    /* Left: question label */
    s_question_label = lv_label_create(q_row);
    lv_obj_set_flex_grow(s_question_label, 1);
    lv_obj_set_width(s_question_label, LV_PCT(100));
    lv_label_set_long_mode(s_question_label, LV_LABEL_LONG_WRAP);
    lv_label_set_text(s_question_label, "Loading...");

    /* 鉁?瀛楀彿锛氶骞插湪杩欓噷璋?*/
    lv_obj_set_style_text_font(s_question_label, QUIZ_ANSWER_FONT, 0);

    /* Right: progress label */
    s_progress_label = lv_label_create(q_row);
    lv_label_set_text(s_progress_label, "0 / 0");
    lv_obj_set_style_text_font(s_progress_label, UI_FONT_SMALL, 0);
    lv_obj_set_style_text_align(s_progress_label, LV_TEXT_ALIGN_RIGHT, 0);

    /* ========== Option text labels ========== */
    for (int i = 0; i < QUIZ_OPTION_COUNT; i++)
    {
        s_option_labels[i] = lv_label_create(s_scroll);
        lv_obj_set_width(s_option_labels[i], LV_PCT(100));
        lv_label_set_long_mode(s_option_labels[i], LV_LABEL_LONG_WRAP);
        lv_label_set_text_fmt(s_option_labels[i], "%c. ", 'A' + i);

        /* 宸﹀彸杈硅窛 + 姣忔潯閫夐」鑷韩涓婁笅鐣欑櫧锛岃閫夐」闂存洿缇庤 */
        lv_obj_set_style_pad_left(s_option_labels[i], 10, 0);
        lv_obj_set_style_pad_right(s_option_labels[i], 10, 0);
        lv_obj_set_style_pad_top(s_option_labels[i], 2, 0);
        lv_obj_set_style_pad_bottom(s_option_labels[i], 2, 0);

        /* 鉁?瀛楀彿锛氶€夐」鍦ㄨ繖閲岃皟 鈥斺€?浣犺鈥滃叏閮ㄥぇ瀛椻€濓紝杩欓噷鏀规垚 LARGE */
        lv_obj_set_style_text_font(s_option_labels[i], QUIZ_ANSWER_FONT, 0);
        lv_obj_set_style_text_color(s_option_labels[i], OPTION_TEXT_NORMAL_COLOR, 0);
    }

    /* ========== Button row (after options) ========== */
    lv_obj_t *btn_row = lv_obj_create(s_scroll);
    lv_obj_set_width(btn_row, LV_PCT(100));
    lv_obj_set_height(btn_row, 172);
    lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_row,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    /* 涓や晶鐣欒竟璺?+ 鎸夐挳涔嬮棿鐣欓棿闅?*/
    lv_obj_set_style_pad_left(btn_row, 8, 0);
    lv_obj_set_style_pad_right(btn_row, 8, 0);
    lv_obj_set_style_pad_top(btn_row, 6, 0);
    lv_obj_set_style_pad_bottom(btn_row, 8, 0);
    lv_obj_set_style_pad_gap(btn_row, 8, 0);

    lv_obj_set_style_bg_opa(btn_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btn_row, 0, 0);
    lv_obj_clear_flag(btn_row, LV_OBJ_FLAG_SCROLLABLE);

    /* Common button styling */
    const lv_color_t border_col = lv_color_white();
    const lv_color_t checked_bg = lv_palette_main(LV_PALETTE_GREEN);

    /* A/B/C/D buttons: checkable + checked->green */
    for (int i = 0; i < QUIZ_OPTION_COUNT; i++)
    {
        s_option_btns[i] = lv_btn_create(btn_row);

        /* 鍧囧垎瀹藉害锛堣€冭檻杈硅窛/闂撮殧鍚庝粛绛夊锛?*/
        lv_obj_set_flex_grow(s_option_btns[i], 1);
        lv_obj_set_height(s_option_btns[i], 172);

        lv_obj_add_flag(s_option_btns[i], LV_OBJ_FLAG_CHECKABLE);

        /* 鐧借竟妗?*/
        lv_obj_set_style_border_width(s_option_btns[i], 2, LV_PART_MAIN);
        lv_obj_set_style_border_color(s_option_btns[i], border_col, LV_PART_MAIN);
        lv_obj_set_style_border_opa(s_option_btns[i], LV_OPA_100, LV_PART_MAIN);

        /* 閫変腑缁胯壊 */
        lv_obj_set_style_bg_color(s_option_btns[i], checked_bg, LV_PART_MAIN | LV_STATE_CHECKED);
        lv_obj_set_style_bg_opa(s_option_btns[i], LV_OPA_100, LV_PART_MAIN | LV_STATE_CHECKED);

        /* 閫変腑鏃舵枃瀛楃櫧鑹叉洿娓呮櫚 */
        lv_obj_set_style_text_color(s_option_btns[i], lv_color_white(), LV_PART_MAIN | LV_STATE_CHECKED);

        lv_obj_add_event_cb(
            s_option_btns[i],
            quiz_handle_option,
            LV_EVENT_CLICKED,
            (void *)(intptr_t)i);

        lv_obj_t *lbl = lv_label_create(s_option_btns[i]);
        lv_label_set_text_fmt(lbl, "%c", 'A' + i);
        lv_obj_center(lbl);
        lv_obj_set_style_text_font(lbl, QUIZ_ANSWER_FONT, 0);
    }

    /* Submit: 涓嶈 checked 鍙樿壊锛堜笉鍔?checkable/涓嶈缃?LV_STATE_CHECKED 鏍峰紡锛?*/
    s_submit_btn = lv_btn_create(btn_row);
    lv_obj_set_flex_grow(s_submit_btn, 1);
    lv_obj_set_height(s_submit_btn, 172);

    lv_obj_set_style_border_width(s_submit_btn, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_submit_btn, border_col, LV_PART_MAIN);
    lv_obj_set_style_border_opa(s_submit_btn, LV_OPA_100, LV_PART_MAIN);

    lv_obj_add_event_cb(s_submit_btn, quiz_handle_submit, LV_EVENT_CLICKED, NULL);
    s_submit_label = lv_label_create(s_submit_btn);
<<<<<<< HEAD
    lv_label_set_text(s_submit_label, "???");
    lv_obj_center(s_submit_label);
=======
    lv_label_set_text(s_submit_label, "Next");
    lv_obj_center(s_submit_label);
    lv_obj_set_style_text_font(s_submit_label, UI_FONT_LARGE, 0);
>>>>>>> 0007b3115549354c5d21c3818b49bc74d75d798e
    quiz_set_cn_font_for_label(s_submit_label);
}

static void quiz_add_wrong_row(uint8_t qno, char your_c, char ans_c)
{
    lv_obj_t *row = lv_obj_create(s_result_scroll);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);

    lv_obj_set_style_pad_all(row, 8, 0);
    lv_obj_set_style_pad_gap(row, 8, 0);
    lv_obj_set_style_radius(row, 10, 0);
    lv_obj_set_style_bg_color(row, lv_color_hex(0xF6F7F9), 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_100, 0);
    lv_obj_set_style_border_width(row, 1, 0);
    lv_obj_set_style_border_color(row, lv_color_hex(0xE6E6E6), 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    lv_obj_t *q_lbl = lv_label_create(row);
    lv_label_set_text_fmt(q_lbl, "Q%d", qno);
    lv_obj_set_style_text_font(q_lbl, LV_FONT_DEFAULT, LV_PART_MAIN);
    lv_obj_set_style_text_color(q_lbl, lv_color_hex(0x111111), 0);

    lv_obj_t *your_key_lbl = lv_label_create(row);
    lv_label_set_text(your_key_lbl, "你的：");
    quiz_set_cn_font_for_label(your_key_lbl);
    lv_obj_set_style_text_color(your_key_lbl, lv_color_hex(0x111111), 0);

    lv_obj_t *your_val_lbl = lv_label_create(row);
    lv_label_set_text_fmt(your_val_lbl, "%c", your_c);
    lv_obj_set_style_text_font(your_val_lbl, LV_FONT_DEFAULT, LV_PART_MAIN);
    lv_obj_set_style_text_color(your_val_lbl, lv_color_hex(0x111111), 0);

    lv_obj_t *ans_key_lbl = lv_label_create(row);
    lv_label_set_text(ans_key_lbl, "答案：");
    quiz_set_cn_font_for_label(ans_key_lbl);
    lv_obj_set_style_text_color(ans_key_lbl, lv_color_hex(0x111111), 0);

    lv_obj_t *ans_val_lbl = lv_label_create(row);
    lv_label_set_text_fmt(ans_val_lbl, "%c", ans_c);
    lv_obj_set_style_text_font(ans_val_lbl, LV_FONT_DEFAULT, LV_PART_MAIN);
    lv_obj_set_style_text_color(ans_val_lbl, lv_color_hex(0x111111), 0);
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

    /* Root */
    lv_obj_set_size(s_result_screen, 640, 172);
    lv_obj_clear_flag(s_result_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_result_screen, lv_color_white(), 0);
    lv_obj_set_style_pad_all(s_result_screen, 0, 0);

    /* ===== Single scroll container (full screen) ===== */
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

    /* ===== Calculate local score (UI only) ===== */
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

    /* ===== Title ===== */
    lv_obj_t *title = lv_label_create(s_result_scroll);
    lv_label_set_text(title, "结果");
    lv_obj_set_style_text_font(title, UI_FONT_NORMAL, 0);
    quiz_set_cn_font_for_label(title);
    lv_obj_set_style_text_color(title, lv_color_hex(0x111111), 0);

    /* ===== Summary row: Score / Accuracy / Time ===== */
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
    lv_obj_set_style_pad_gap(sum_row, 14, 0);

    lv_obj_t *score_item = lv_obj_create(sum_row);
    lv_obj_clear_flag(score_item, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(score_item, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(score_item, 0, 0);
    lv_obj_set_style_pad_all(score_item, 0, 0);
    lv_obj_set_style_pad_gap(score_item, 4, 0);
    lv_obj_set_flex_flow(score_item, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(score_item, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *score_key_lbl = lv_label_create(score_item);
    lv_label_set_text(score_key_lbl, "得分：");
    quiz_set_cn_font_for_label(score_key_lbl);
    lv_obj_set_style_text_color(score_key_lbl, lv_color_hex(0x111111), 0);
    lv_obj_t *score_val_lbl = lv_label_create(score_item);
    lv_label_set_text_fmt(score_val_lbl, "%d/%d", show_score, show_total);
    lv_obj_set_style_text_font(score_val_lbl, LV_FONT_DEFAULT, LV_PART_MAIN);
    lv_obj_set_style_text_color(score_val_lbl, lv_color_hex(0x111111), 0);

    lv_obj_t *acc_item = lv_obj_create(sum_row);
    lv_obj_clear_flag(acc_item, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(acc_item, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(acc_item, 0, 0);
    lv_obj_set_style_pad_all(acc_item, 0, 0);
    lv_obj_set_style_pad_gap(acc_item, 4, 0);
    lv_obj_set_flex_flow(acc_item, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(acc_item, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *acc_key_lbl = lv_label_create(acc_item);
    lv_label_set_text(acc_key_lbl, "正确率：");
    quiz_set_cn_font_for_label(acc_key_lbl);
    lv_obj_set_style_text_color(acc_key_lbl, lv_color_hex(0x111111), 0);
    lv_obj_t *acc_val_lbl = lv_label_create(acc_item);
    lv_label_set_text_fmt(acc_val_lbl, "%d%%", acc);
    lv_obj_set_style_text_font(acc_val_lbl, LV_FONT_DEFAULT, LV_PART_MAIN);
    lv_obj_set_style_text_color(acc_val_lbl, lv_color_hex(0x111111), 0);

    lv_obj_t *time_item = lv_obj_create(sum_row);
    lv_obj_clear_flag(time_item, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(time_item, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(time_item, 0, 0);
    lv_obj_set_style_pad_all(time_item, 0, 0);
    lv_obj_set_style_pad_gap(time_item, 4, 0);
    lv_obj_set_flex_flow(time_item, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(time_item, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *time_key_lbl = lv_label_create(time_item);
    lv_label_set_text(time_key_lbl, "时间：");
    quiz_set_cn_font_for_label(time_key_lbl);
    lv_obj_set_style_text_color(time_key_lbl, lv_color_hex(0x111111), 0);
    lv_obj_t *time_val_lbl = lv_label_create(time_item);
    lv_label_set_text(time_val_lbl, "--:--");
    lv_obj_set_style_text_font(time_val_lbl, LV_FONT_DEFAULT, LV_PART_MAIN);
    lv_obj_set_style_text_color(time_val_lbl, lv_color_hex(0x111111), 0);

    /* ===== Correct / Wrong row ===== */
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
    lv_obj_set_style_pad_gap(cw_row, 12, 0);

    lv_obj_t *c_item = lv_obj_create(cw_row);
    lv_obj_clear_flag(c_item, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(c_item, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(c_item, 0, 0);
    lv_obj_set_style_pad_all(c_item, 0, 0);
    lv_obj_set_style_pad_gap(c_item, 4, 0);
    lv_obj_set_flex_flow(c_item, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(c_item, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *c_key_lbl = lv_label_create(c_item);
    lv_label_set_text(c_key_lbl, "正确数：");
    quiz_set_cn_font_for_label(c_key_lbl);
    lv_obj_set_style_text_color(c_key_lbl, lv_color_hex(0x1a7f37), 0);
    lv_obj_t *c_val_lbl = lv_label_create(c_item);
    lv_label_set_text_fmt(c_val_lbl, "%d", show_score);
    lv_obj_set_style_text_font(c_val_lbl, LV_FONT_DEFAULT, LV_PART_MAIN);
    lv_obj_set_style_text_color(c_val_lbl, lv_color_hex(0x1a7f37), 0);

    lv_obj_t *w_item = lv_obj_create(cw_row);
    lv_obj_clear_flag(w_item, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(w_item, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(w_item, 0, 0);
    lv_obj_set_style_pad_all(w_item, 0, 0);
    lv_obj_set_style_pad_gap(w_item, 4, 0);
    lv_obj_set_flex_flow(w_item, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(w_item, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *w_key_lbl = lv_label_create(w_item);
    lv_label_set_text(w_key_lbl, "错误数：");
    quiz_set_cn_font_for_label(w_key_lbl);
    lv_obj_set_style_text_color(w_key_lbl, lv_color_hex(0xb42318), 0);
    lv_obj_t *w_val_lbl = lv_label_create(w_item);
    lv_label_set_text_fmt(w_val_lbl, "%d", wrong_cnt);
    lv_obj_set_style_text_font(w_val_lbl, LV_FONT_DEFAULT, LV_PART_MAIN);
    lv_obj_set_style_text_color(w_val_lbl, lv_color_hex(0xb42318), 0);

    /* ===== Divider ===== */
    lv_obj_t *divider = lv_obj_create(s_result_scroll);
    lv_obj_clear_flag(divider, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_width(divider, LV_PCT(100));
    lv_obj_set_height(divider, 1);
    lv_obj_set_style_bg_color(divider, lv_color_hex(0xE6E6E6), 0);
    lv_obj_set_style_bg_opa(divider, LV_OPA_100, 0);
    lv_obj_set_style_pad_all(divider, 0, 0);

    /* ===== Wrong list title ===== */
    lv_obj_t *wl_title = lv_label_create(s_result_scroll);
    lv_label_set_text(wl_title, "错题列表");
    lv_obj_set_style_text_font(wl_title, UI_FONT_NORMAL, 0);
    quiz_set_cn_font_for_label(wl_title);
    lv_obj_set_style_text_color(wl_title, lv_color_hex(0x111111), 0);

    /* ===== Wrong list items (STATIC, no click) ===== */
    bool has_wrong = false;

    if (s_state.server_wrong_count > 0)
    {
        for (uint8_t i = 0; i < s_state.server_wrong_count; i++)
        {
            const quiz_wrong_item_t *wrong = &s_state.server_wrong[i];

            /* map id -> q_index (for display Qn) */
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

            quiz_add_wrong_row(q_index + 1, your_c, ans_c);
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

            quiz_add_wrong_row(i + 1, your_c, ans_c);
            has_wrong = true;
        }
    }

    if (!has_wrong)
    {
        lv_obj_t *all_good = lv_label_create(s_result_scroll);
        lv_label_set_text(all_good, "全部答对");
        lv_obj_set_style_text_font(all_good, UI_FONT_NORMAL, 0);
        quiz_set_cn_font_for_label(all_good);
        lv_obj_set_style_text_color(all_good, lv_color_hex(0x1a7f37), 0);
    }

    /* ===== Buttons row: Retry / Back (only two) ===== */
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
    lv_label_set_text(retry_lbl, "重试");
    lv_obj_set_style_text_font(retry_lbl, UI_FONT_NORMAL, 0);
    quiz_set_cn_font_for_label(retry_lbl);
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
    lv_label_set_text(back_lbl, "返回");
    lv_obj_set_style_text_font(back_lbl, UI_FONT_NORMAL, 0);
    quiz_set_cn_font_for_label(back_lbl);
    lv_obj_set_style_text_color(back_lbl, lv_color_hex(0x111111), 0);
    lv_obj_center(back_lbl);
}

static void quiz_show_results_screen(void)
{
    quiz_build_result_screen();
    lv_scr_load(s_result_screen);

    /* 姣忔杩涘叆缁撴灉椤甸兘鍥炲埌椤堕儴 */
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

    /* Highlight bottom buttons */
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

    /* Highlight option text */
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

/* 鎶?\r \n 鍙樼┖鏍硷紝骞跺帇缂╄繛缁┖鏍硷紝閬垮厤棰樺共/閫夐」鍑虹幇鈥滅┖琛岄珮搴︹€?*/
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

    /* Question stem: sanitize to avoid invisible blank lines */
    char stem_clean[QUIZ_TEXT_LEN + 1];
    quiz_sanitize_text(stem_clean, sizeof(stem_clean), s_state.questions[index].stem);

    char q_line[QUIZ_TEXT_LEN + 16];
    snprintf(q_line, sizeof(q_line), "Q%d: %s", index + 1, stem_clean);
    lv_label_set_text(s_question_label, q_line);

    /* Options text */
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

        /* Clear bottom button highlight */
        lv_obj_clear_state(s_option_btns[i], LV_STATE_CHECKED);

        /* Clear option label highlight */
        lv_obj_set_style_text_color(s_option_labels[i], OPTION_TEXT_NORMAL_COLOR, 0);
    }

    /* Restore selected option highlight if exists */
    if (s_state.answers[index] < QUIZ_OPTION_COUNT)
    {
        uint8_t sel = s_state.answers[index];
        lv_obj_add_state(s_option_btns[sel], LV_STATE_CHECKED);

        /* Restore option text highlight together with the button state. */
        lv_obj_set_style_text_color(s_option_labels[sel], OPTION_TEXT_ACTIVE_COLOR, 0);
    }

    quiz_update_submit_button_text(index);
    quiz_set_submit_loading(false);

    /* Always scroll back to the top when loading a new question. */
    lv_obj_scroll_to_y(s_scroll, 0, LV_ANIM_OFF);
}

static void quiz_finish_and_upload(void)
{
    s_state.test_finished = true;
    s_state.final_submit_success = false;
    quiz_prepare_local_result();

    if (s_attempt_id[0] == '\0')
    {
<<<<<<< HEAD
        quiz_show_toast_cn("????????????", 2000);
=======
        quiz_show_toast("Attempt id missing, download again", 2000);
>>>>>>> 0007b3115549354c5d21c3818b49bc74d75d798e
        quiz_show_results_screen();
        return;
    }

    char *payload = quiz_build_submit_payload();
    if (!payload)
    {
<<<<<<< HEAD
        quiz_show_toast_cn("????", 1800);
=======
        quiz_show_toast("Out of memory", 1800);
>>>>>>> 0007b3115549354c5d21c3818b49bc74d75d798e
        quiz_show_results_screen();
        return;
    }

    char upload_reason[160] = {0};
    int upload_status = 0;
    esp_err_t err = quiz_http_post_results(payload, &upload_status, upload_reason, sizeof(upload_reason));
    free(payload);
    if (err == ESP_OK)
    {
        s_state.final_submit_success = true;
<<<<<<< HEAD
        quiz_show_toast_cn("???", 1800);
=======
        quiz_show_toast_cn("Completed", 1800);
>>>>>>> 0007b3115549354c5d21c3818b49bc74d75d798e
    }
    else
    {
        if (upload_status == 401)
        {
            quiz_show_toast_cn("???????????", 2200);
        }
        else if (upload_status == 403)
        {
            quiz_show_toast_cn("????", 2200);
        }
        else if (upload_status == 404)
        {
            quiz_show_toast_cn("?????", 2200);
        }
        else if (upload_status == 409)
        {
            s_state.final_submit_success = true;
<<<<<<< HEAD
            quiz_show_toast_cn("???", 1800);
=======
            quiz_show_toast_cn("Completed", 1800);
>>>>>>> 0007b3115549354c5d21c3818b49bc74d75d798e
        }
        else if (upload_reason[0] != '\0')
        {
            quiz_show_toast_cn(upload_reason, 2200);
        }
        else
        {
<<<<<<< HEAD
            quiz_show_toast_cn("?????????????", 2200);
=======
            quiz_show_toast_cn("Answers saved, final submit failed", 2200);
>>>>>>> 0007b3115549354c5d21c3818b49bc74d75d798e
        }
    }

    quiz_show_results_screen();
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
    if (s_state.submit_inflight)
    {
        return;
    }

    uint8_t idx = s_state.current_question;
    if (idx >= s_state.question_count)
    {
        return;
    }

    if (s_state.answers[idx] >= QUIZ_OPTION_COUNT)
    {
<<<<<<< HEAD
        quiz_show_toast_cn("???????", 1600);
=======
        quiz_show_toast_cn("Select an option", 1600);
>>>>>>> 0007b3115549354c5d21c3818b49bc74d75d798e
        return;
    }

    bool is_final_question = (idx + 1 >= s_state.question_count);

    quiz_set_submit_loading(true);
    if (!quiz_submit_question_answer(idx, is_final_question))
    {
        quiz_set_submit_loading(false);
        return;
    }

    if (is_final_question)
    {
        quiz_finish_and_upload();
        quiz_set_submit_loading(false);
        return;
    }

    s_state.current_question++;
    quiz_load_question(s_state.current_question);
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
    memset(s_state.submitted_answers, 0xFF, sizeof(s_state.submitted_answers));
    s_state.final_submit_success = false;
<<<<<<< HEAD
    s_state.attempt_started_tick = 0;
=======
>>>>>>> 0007b3115549354c5d21c3818b49bc74d75d798e
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

    /* Reset quiz id when user changes to avoid stale submissions */
    s_quiz_id[0] = '\0';
    s_attempt_id[0] = '\0';
}

void quiz_app_set_auth_token(const char *token)
{
    if (token && token[0] != '\0')
    {
        const char *value = token;
        if (strncmp(value, "Bearer ", 7) == 0)
        {
            value += 7;
        }
        strncpy(s_auth_token, value, sizeof(s_auth_token) - 1);
        s_auth_token[sizeof(s_auth_token) - 1] = '\0';
        ESP_LOGI(TAG, "Auth token cached, len=%d", (int)strlen(s_auth_token));
    }
    else
    {
        s_auth_token[0] = '\0';
        ESP_LOGW(TAG, "Auth token cleared");
    }
}

static void quiz_reset_server_result(void)
{
    s_state.server_score = -1;
    s_state.server_total = s_state.question_count;
    s_state.server_wrong_count = 0;
    s_state.final_submit_success = false;
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

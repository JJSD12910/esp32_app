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

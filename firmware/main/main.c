#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <errno.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"

static const char *TAG = "KeyboardServer";

const char *WIFI_SSID = "rajani-2";
const char *WIFI_PASS = "rajani2112";

typedef struct {
    const char *name;
} KeyboardKey;

static const KeyboardKey valid_keys[] = {
    {"escape"}, {"f1"}, {"f2"}, {"f3"}, {"f4"}, {"f5"}, {"f6"},
    {"f7"}, {"f8"}, {"f9"}, {"f10"}, {"f11"}, {"f12"},
    {"backquote"}, {"1"}, {"2"}, {"3"}, {"4"}, {"5"}, {"6"},
    {"7"}, {"8"}, {"9"}, {"0"}, {"minus"}, {"equal"},
    {"tab"}, {"a"}, {"b"}, {"c"}, {"d"}, {"e"}, {"f"},
    {"g"}, {"h"}, {"i"}, {"j"}, {"k"}, {"l"}, {"m"},
    {"n"}, {"o"}, {"p"}, {"q"}, {"r"}, {"s"}, {"t"},
    {"u"}, {"v"}, {"w"}, {"x"}, {"y"}, {"z"}, {"bracketleft"},
    {"bracketright"}, {"backslash"}, {"capslock"}, {"semicolon"},
    {"quote"}, {"enter"}, {"comma"}, {"period"}, {"slash"},
    {"space"}, {"backspace"}, {"insert"}, {"delete"},
    {"home"}, {"end"}, {"pageup"}, {"pagedown"},
    {"arrowup"}, {"arrowdown"}, {"arrowleft"}, {"arrowright"},
    {"numlock"}, {"paddivide"}, {"padmultiply"}, {"padminus"},
    {"pad0"}, {"pad1"}, {"pad2"}, {"pad3"}, {"pad4"},
    {"pad5"}, {"pad6"}, {"pad7"}, {"pad8"}, {"pad9"},
    {"padplus"}, {"padperiod"}, {"padenter"}, {"lshift"},
    {"rshift"}, {"lctrl"}, {"rctrl"}, {"lalt"}, {"ralt"},
    {"lwin"}, {"rwin"}, {"menu"}, {"print"}, {"scrolllock"},
    {"pause"}
};

static const int num_valid_keys = sizeof(valid_keys) / sizeof(valid_keys[0]);

bool is_valid_key(const char *key_name)
{
    for (int i = 0; i < num_valid_keys; i++)
    {
        if (strcmp(valid_keys[i].name, key_name) == 0)
        {
            return true;
        }
    }
    return false;
}

bool is_valid_state(const char *state)
{
    return (strcmp(state, "keydown") == 0 || strcmp(state, "keyup") == 0);
}

bool is_toggle_key(const char *key_name)
{
    static const char *toggle_keys[] = {
        "capslock", "numlock", "scrolllock", "print", "pause"
    };
    static const int num_toggle_keys = sizeof(toggle_keys) / sizeof(toggle_keys[0]);

    for (int i = 0; i < num_toggle_keys; i++)
    {
        if (strcmp(toggle_keys[i], key_name) == 0)
        {
            return true;
        }
    }
    return false;
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        ESP_LOGI(TAG, "Connecting to WiFi...");
        esp_wifi_connect();
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        ESP_LOGI(TAG, "Disconnected, retrying...");
        esp_wifi_connect();
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Connected! IP: " IPSTR, IP2STR(&event->ip_info.ip));
    }
}

void wifi_init_sta(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));

    wifi_config_t wifi_config = {0};
    memcpy(wifi_config.sta.ssid, (uint8_t *)WIFI_SSID, strlen(WIFI_SSID));
    memcpy(wifi_config.sta.password, (uint8_t *)WIFI_PASS, strlen(WIFI_PASS));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi Station started, connecting to %s", WIFI_SSID);
}

void udp_server_task(void *pvParameters)
{
    char rx_buffer[128];
    struct sockaddr_in dest_addr;
    dest_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(4444);

    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock < 0)
    {
        ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
        vTaskDelete(NULL);
        return;
    }

    int err = bind(sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    if (err < 0)
    {
        ESP_LOGE(TAG, "Socket unable to bind: errno %d", errno);
        close(sock);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "UDP server listening on port 4444");

    while (1)
    {
        struct sockaddr_storage source_addr;
        socklen_t socklen = sizeof(source_addr);
        int len = recvfrom(sock, rx_buffer, sizeof(rx_buffer) - 1, 0,
                          (struct sockaddr *)&source_addr, &socklen);

        if (len < 0)
        {
            ESP_LOGE(TAG, "recvfrom failed: errno %d", errno);
            continue;
        }

        rx_buffer[len] = 0;
        
        char key[128] = {0};
        int state_val = -1;

        if (sscanf(rx_buffer, "%127[^,],%d", key, &state_val) != 2)
        {
            ESP_LOGW(TAG, "Invalid format: %s", rx_buffer);
            continue;
        }

        const char *state = (state_val == 1) ? "keydown" : "keyup";

        if (!is_valid_key(key))
        {
            ESP_LOGW(TAG, "Invalid key: %s", key);
            continue;
        }

        if (state_val != 0 && state_val != 1)
        {
            ESP_LOGW(TAG, "Invalid state: %d", state_val);
            continue;
        }

        if (is_toggle_key(key) && state_val == 0)
        {
            ESP_LOGW(TAG, "Toggle key %s does not support keyup", key);
            continue;
        }

        ESP_LOGI(TAG, "Key: %s, State: %s", key, state);
    }

    close(sock);
    vTaskDelete(NULL);
}

void app_main(void)
{
    ESP_LOGI(TAG, "Starting ESP32 Keyboard Server");

    wifi_init_sta();
    vTaskDelay(pdMS_TO_TICKS(5000));

    xTaskCreate(udp_server_task, "udp_server", 4096, NULL, 5, NULL);

    ESP_LOGI(TAG, "UDP Keyboard server running on port 4444");
}

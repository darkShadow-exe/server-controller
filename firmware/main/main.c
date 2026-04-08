#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "esp_http_server.h"

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

esp_err_t keyboard_handler(httpd_req_t *req)
{
    char buffer[512] = {0};
    char key[128] = {0};
    char state[32] = {0};
    bool valid_key = false;
    bool valid_state_val = false;

    if (req->content_len > 0)
    {
        int received = httpd_req_recv(req, buffer, sizeof(buffer) - 1);
        if (received <= 0)
        {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Failed to read request body");
            return ESP_FAIL;
        }

        buffer[received] = '\0';

        const char *key_start = strstr(buffer, "\"key\"");
        if (key_start)
        {
            const char *value_start = strchr(key_start, ':');
            if (value_start)
            {
                const char *quote = strchr(value_start, '"');
                if (quote)
                {
                    sscanf(quote + 1, "%127[^\"]", key);
                }
            }
        }

        const char *state_start = strstr(buffer, "\"state\"");
        if (state_start)
        {
            const char *value_start = strchr(state_start, ':');
            if (value_start)
            {
                const char *quote = strchr(value_start, '"');
                if (quote)
                {
                    sscanf(quote + 1, "%31[^\"]", state);
                }
            }
        }

        valid_key = is_valid_key(key);
        valid_state_val = is_valid_state(state);
    }
    else
    {
        const char *query_string = strchr(req->uri, '?');
        if (query_string)
        {
            query_string++;
            sscanf(query_string, "key=%127s", key);

            const char *state_ptr = strstr(query_string, "state=");
            if (state_ptr)
            {
                sscanf(state_ptr, "state=%31s", state);
            }

            valid_key = is_valid_key(key);
            valid_state_val = is_valid_state(state);
        }
    }

    char response[512];
    int len;

    if (strlen(key) == 0 || strlen(state) == 0)
    {
        len = snprintf(response, sizeof(response),
                       "{\"valid\":false,\"error\":\"Missing key or state\",\"key\":\"%s\",\"state\":\"%s\"}",
                       key, state);
    }
    else if (!valid_key)
    {
        len = snprintf(response, sizeof(response),
                       "{\"valid\":false,\"error\":\"Invalid key\",\"key\":\"%s\",\"state\":\"%s\"}",
                       key, state);
    }
    else if (!valid_state_val)
    {
        len = snprintf(response, sizeof(response),
                       "{\"valid\":false,\"error\":\"Invalid state. Use keydown or keyup\",\"key\":\"%s\",\"state\":\"%s\"}",
                       key, state);
    }
    else if (is_toggle_key(key) && strcmp(state, "keyup") == 0)
    {
        len = snprintf(response, sizeof(response),
                       "{\"valid\":false,\"error\":\"Toggle keys do not support keyup. Use keydown only\",\"key\":\"%s\",\"state\":\"%s\"}",
                       key, state);
    }
    else
    {
        len = snprintf(response, sizeof(response),
                       "{\"valid\":true,\"message\":\"Command accepted\",\"key\":\"%s\",\"state\":\"%s\"}",
                       key, state);
        ESP_LOGI(TAG, "Key: %s, State: %s", key, state);
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, response, len);

    return ESP_OK;
}

esp_err_t root_handler(httpd_req_t *req)
{
    const char resp[] = "ESP32 Keyboard Server\n\n"
                        "Available endpoints:\n"
                        "POST http://<esp32-ip>/key with JSON body:\n"
                        "  {\"key\": \"a\", \"state\": \"keydown\"}\n"
                        "  {\"key\": \"a\", \"state\": \"keyup\"}\n\n"
                        "GET http://<esp32-ip>/key?key=a&state=keydown\n\n"
                        "Valid states: keydown, keyup\n"
                        "Valid keys: a-z, 0-9, arrow keys, function keys, special keys\n";

    httpd_resp_send(req, resp, strlen(resp));
    return ESP_OK;
}

httpd_handle_t start_webserver(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_open_sockets = 2;
    config.max_uri_handlers = 3;
    config.stack_size = 8192;

    httpd_handle_t server = NULL;

    ESP_LOGI(TAG, "Starting HTTP server...");
    
    if (httpd_start(&server, &config) == ESP_OK)
    {
        ESP_LOGI(TAG, "HTTP server started");
        
        httpd_uri_t root = {
            .uri = "/",
            .method = HTTP_GET,
            .handler = root_handler,
        };
        httpd_register_uri_handler(server, &root);
        ESP_LOGI(TAG, "Registered / handler");

        httpd_uri_t key_get = {
            .uri = "/key",
            .method = HTTP_GET,
            .handler = keyboard_handler,
        };
        httpd_register_uri_handler(server, &key_get);
        ESP_LOGI(TAG, "Registered /key GET handler");

        httpd_uri_t key_post = {
            .uri = "/key",
            .method = HTTP_POST,
            .handler = keyboard_handler,
        };
        httpd_register_uri_handler(server, &key_post);
        ESP_LOGI(TAG, "Registered /key POST handler");

        ESP_LOGI(TAG, "Keyboard server ready");
    }
    else
    {
        ESP_LOGE(TAG, "Failed to start HTTP server");
    }

    return server;
}

void app_main(void)
{
    ESP_LOGI(TAG, "Starting ESP32 Keyboard Server");

    wifi_init_sta();
    vTaskDelay(pdMS_TO_TICKS(5000));

    start_webserver();

    ESP_LOGI(TAG, "Keyboard server running");
    ESP_LOGI(TAG, "Send requests to: http://<esp32-ip>/key");
}

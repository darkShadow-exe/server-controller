#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_http_server.h"
#include "cJSON.h"

static const char *TAG = "WebServer";

const char *WIFI_SSID = "rajani-2";
const char *WIFI_PASS = "rajani2112";

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STACONNECTED)
    {
        wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *)event_data;
        ESP_LOGI(TAG, "Station " MACSTR " joined, AID=%d",
                 MAC2STR(event->mac), event->aid);
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STADISCONNECTED)
    {
        wifi_event_ap_stadisconnected_t *event = (wifi_event_ap_stadisconnected_t *)event_data;
        ESP_LOGI(TAG, "Station " MACSTR " left, AID=%d",
                 MAC2STR(event->mac), event->aid);
    }
}

void wifi_init_ap(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = WIFI_SSID,
            .ssid_len = strlen(WIFI_SSID),
            .password = WIFI_PASS,
            .max_connection = 4,
            .authmode = WIFI_AUTH_WPA2_PSK,
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi AP started. SSID:%s password:%s", WIFI_SSID, WIFI_PASS);
}

esp_err_t query_handler(httpd_req_t *req)
{
    char buffer[256];
    char key[128] = {0};

    if (req->content_length > 0)
    {
        int total_len = req->content_len;
        int cur_len = 0;
        int received = 0;

        received = httpd_req_recv(req, buffer, sizeof(buffer) - 1);
        if (received <= 0)
        {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Failed to read request body");
            return ESP_FAIL;
        }

        buffer[received] = '\0';

        cJSON *req_json = cJSON_Parse(buffer);
        if (!req_json)
        {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
            return ESP_FAIL;
        }

        cJSON *key_item = cJSON_GetObjectItem(req_json, "key");
        if (key_item && key_item->valuestring)
        {
            strncpy(key, key_item->valuestring, sizeof(key) - 1);
        }
        cJSON_Delete(req_json);
    }
    else if (req->uri_match_data)
    {
        const char *query_string = strchr(req->uri, '?');
        if (query_string)
        {
            query_string++;
            if (sscanf(query_string, "key=%127s", key) != 1)
            {
                strcpy(key, "unknown");
            }
        }
    }

    cJSON *response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "status", "success");
    cJSON_AddStringToObject(response, "key", key);

    if (strlen(key) == 0)
    {
        cJSON_AddStringToObject(response, "message", "No key provided");
        cJSON_AddNumberToObject(response, "value", 0);
    }
    else if (strcmp(key, "temperature") == 0)
    {
        cJSON_AddStringToObject(response, "message", "Temperature reading");
        cJSON_AddNumberToObject(response, "value", 25.5);
        cJSON_AddStringToObject(response, "unit", "°C");
    }
    else if (strcmp(key, "humidity") == 0)
    {
        cJSON_AddStringToObject(response, "message", "Humidity reading");
        cJSON_AddNumberToObject(response, "value", 60.3);
        cJSON_AddStringToObject(response, "unit", "%");
    }
    else if (strcmp(key, "pressure") == 0)
    {
        cJSON_AddStringToObject(response, "message", "Pressure reading");
        cJSON_AddNumberToObject(response, "value", 1013.25);
        cJSON_AddStringToObject(response, "unit", "hPa");
    }
    else if (strcmp(key, "uptime") == 0)
    {
        cJSON_AddStringToObject(response, "message", "System uptime");
        cJSON_AddNumberToObject(response, "value", esp_log_timestamp());
    }
    else
    {
        cJSON_AddStringToObject(response, "message", "Unknown key. Try: temperature, humidity, pressure, uptime");
        cJSON_AddNumberToObject(response, "value", -1);
    }

    char *json_str = cJSON_Print(response);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_str, strlen(json_str));

    free(json_str);
    cJSON_Delete(response);

    return ESP_OK;
}

esp_err_t root_handler(httpd_req_t *req)
{
    const char resp[] = "ESP32 Web Server\n\n"
                        "Available endpoints:\n"
                        "GET http://192.168.4.1/query?key=<key>\n"
                        "POST http://192.168.4.1/query with JSON body: {\"key\": \"..\"}\n\n"
                        "Valid keys: temperature, humidity, pressure, uptime\n";

    httpd_resp_send(req, resp, strlen(resp));
    return ESP_OK;
}

httpd_handle_t start_webserver(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_open_sockets = 2;
    config.max_uri_handlers = 8;

    httpd_handle_t server = NULL;

    if (httpd_start(&server, &config) == ESP_OK)
    {
        httpd_uri_t root = {
            .uri = "/",
            .method = HTTP_GET,
            .handler = root_handler,
        };
        httpd_register_uri_handler(server, &root);

        httpd_uri_t query_get = {
            .uri = "/query",
            .method = HTTP_GET,
            .handler = query_handler,
        };
        httpd_register_uri_handler(server, &query_get);

        httpd_uri_t query_post = {
            .uri = "/query",
            .method = HTTP_POST,
            .handler = query_handler,
        };
        httpd_register_uri_handler(server, &query_post);

        ESP_LOGI(TAG, "Web server started");
    }

    return server;
}

void app_main(void)
{
    ESP_LOGI(TAG, "Starting ESP32 Web Server");

    wifi_init_ap();
    vTaskDelay(pdMS_TO_TICKS(2000));

    start_webserver();

    ESP_LOGI(TAG, "Server ready. Connect to WiFi SSID: %s", WIFI_SSID);
    ESP_LOGI(TAG, "Access server at: http://192.168.4.1/");
}

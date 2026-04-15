#include "nvs_flash.h"
#include "esp_log.h"
#include "camera_app.h"
#include "calibration.h"
#include "wifi_app.h"
#include "stream_server.h"

static const char *TAG = "main";

#define WIFI_SSID "ghar"
#define WIFI_PASS "Avikkaadda127"

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(calibration_init());

    ESP_LOGI(TAG, "Init camera");
    ESP_ERROR_CHECK(camera_app_init());

    ESP_LOGI(TAG, "Connect Wi-Fi");
    ESP_ERROR_CHECK(wifi_app_init_sta(WIFI_SSID, WIFI_PASS));

    ESP_LOGI(TAG, "Start stream server");
    ESP_ERROR_CHECK(stream_server_start());

    ESP_LOGI(TAG, "Open http://<esp32-ip>/");
}
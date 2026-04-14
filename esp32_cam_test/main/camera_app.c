#include "camera_app.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_camera.h"

static const char *TAG = "camera_app";
static bool s_jpeg_mode = false;

// AI Thinker / common ESP32-CAM pin map
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27

#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

static esp_err_t camera_init_with_format(pixformat_t pixel_format, framesize_t frame_size, int fb_count)
{
    camera_config_t config = {
        .pin_pwdn     = PWDN_GPIO_NUM,
        .pin_reset    = RESET_GPIO_NUM,
        .pin_xclk     = XCLK_GPIO_NUM,
        .pin_sccb_sda = SIOD_GPIO_NUM,
        .pin_sccb_scl = SIOC_GPIO_NUM,

        .pin_d7       = Y9_GPIO_NUM,
        .pin_d6       = Y8_GPIO_NUM,
        .pin_d5       = Y7_GPIO_NUM,
        .pin_d4       = Y6_GPIO_NUM,
        .pin_d3       = Y5_GPIO_NUM,
        .pin_d2       = Y4_GPIO_NUM,
        .pin_d1       = Y3_GPIO_NUM,
        .pin_d0       = Y2_GPIO_NUM,
        .pin_vsync    = VSYNC_GPIO_NUM,
        .pin_href     = HREF_GPIO_NUM,
        .pin_pclk     = PCLK_GPIO_NUM,

        .xclk_freq_hz = 20000000,
        .ledc_timer   = LEDC_TIMER_0,
        .ledc_channel = LEDC_CHANNEL_0,

        .pixel_format = pixel_format,
        .frame_size   = frame_size,
        .jpeg_quality = 15,
        .fb_count     = fb_count,
        .grab_mode    = CAMERA_GRAB_LATEST,
        .fb_location  = CAMERA_FB_IN_PSRAM,
    };

    return esp_camera_init(&config);
}

esp_err_t camera_app_init(void)
{
    esp_err_t err = camera_init_with_format(PIXFORMAT_JPEG, FRAMESIZE_QVGA, 2);
    if (err == ESP_OK) {
        s_jpeg_mode = true;
        ESP_LOGI(TAG, "Camera initialized in JPEG mode");
    } else if (err == ESP_ERR_NOT_SUPPORTED) {
        ESP_LOGW(TAG, "Sensor does not support JPEG, falling back to RGB565");
        esp_camera_deinit();
        err = camera_init_with_format(PIXFORMAT_RGB565, FRAMESIZE_QQVGA, 1);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "RGB565 fallback init failed: %s", esp_err_to_name(err));
            return err;
        }
        s_jpeg_mode = false;
        ESP_LOGI(TAG, "Camera initialized in RGB565 fallback mode");
    } else {
        ESP_LOGE(TAG, "esp_camera_init failed: %s", esp_err_to_name(err));
        return err;
    }

    sensor_t *s = esp_camera_sensor_get();
    if (s) {
        s->set_brightness(s, 0);
        s->set_contrast(s, 0);
        s->set_saturation(s, 0);
    }

    ESP_LOGI(TAG, "Camera initialized");
    return ESP_OK;
}

bool camera_app_is_jpeg_mode(void)
{
    return s_jpeg_mode;
}

camera_fb_t *camera_app_get_frame(void)
{
    return esp_camera_fb_get();
}

void camera_app_return_frame(camera_fb_t *fb)
{
    if (fb) {
        esp_camera_fb_return(fb);
    }
}

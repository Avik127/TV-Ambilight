#include "stream_server.h"
#include <string.h>
#include "esp_log.h"
#include "esp_http_server.h"
#include "camera_app.h"

static const char *TAG = "stream_server";

static esp_err_t jpg_handler(httpd_req_t *req)
{
    camera_fb_t *fb = camera_app_get_frame();
    if (!fb) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Camera capture failed");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "image/jpeg");
    esp_err_t res = httpd_resp_send(req, (const char *)fb->buf, fb->len);
    camera_app_return_frame(fb);
    return res;
}

static esp_err_t stream_handler(httpd_req_t *req)
{
    static const char *STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=frame";
    static const char *BOUNDARY = "\r\n--frame\r\n";
    static const char *PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";
    char part_header[64];

    httpd_resp_set_type(req, STREAM_CONTENT_TYPE);
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

    while (1) {
        camera_fb_t *fb = camera_app_get_frame();
        if (!fb) {
            ESP_LOGW(TAG, "Camera capture failed");
            return ESP_FAIL;
        }

        int header_len = snprintf(part_header, sizeof(part_header), PART, fb->len);
        esp_err_t res = httpd_resp_send_chunk(req, BOUNDARY, strlen(BOUNDARY));
        if (res == ESP_OK) {
            res = httpd_resp_send_chunk(req, part_header, header_len);
        }
        if (res == ESP_OK) {
            res = httpd_resp_send_chunk(req, (const char *)fb->buf, fb->len);
        }

        camera_app_return_frame(fb);
        if (res != ESP_OK) {
            ESP_LOGI(TAG, "Stream client disconnected");
            return res;
        }
    }
}

static esp_err_t root_handler(httpd_req_t *req)
{
    static const char html[] =
        "<!doctype html><html><body>"
        "<h1>ESP32-CAM</h1>"
        "<img id='cam' style='max-width:100%;image-rendering:pixelated;'>"
        "<script>"
        "const img = document.getElementById('cam');"
        "function refresh(){"
        "  img.onerror = () => setTimeout(refresh, 1000);"
        "  img.src = '/stream';"
        "}"
        "refresh();"
        "</script>"
        "</body></html>";

    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, html, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

esp_err_t stream_server_start(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    httpd_handle_t server = NULL;

    if (httpd_start(&server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server");
        return ESP_FAIL;
    }

    httpd_uri_t root_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = root_handler,
        .user_ctx = NULL
    };

    httpd_uri_t jpg_uri = {
        .uri = "/jpg",
        .method = HTTP_GET,
        .handler = jpg_handler,
        .user_ctx = NULL
    };

    httpd_uri_t stream_uri = {
        .uri = "/stream",
        .method = HTTP_GET,
        .handler = stream_handler,
        .user_ctx = NULL
    };

    httpd_register_uri_handler(server, &root_uri);
    httpd_register_uri_handler(server, &jpg_uri);
    httpd_register_uri_handler(server, &stream_uri);

    ESP_LOGI(TAG, "HTTP server started");
    return ESP_OK;
}

#include "stream_server.h"
#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_http_server.h"
#include "camera_app.h"

static const char *TAG = "stream_server";

#define SWAP_RGB565_BYTES 1

static void write_u16_le(uint8_t *buf, int offset, uint16_t value)
{
    buf[offset + 0] = value & 0xFF;
    buf[offset + 1] = (value >> 8) & 0xFF;
}

static void write_u32_le(uint8_t *buf, int offset, uint32_t value)
{
    buf[offset + 0] = value & 0xFF;
    buf[offset + 1] = (value >> 8) & 0xFF;
    buf[offset + 2] = (value >> 16) & 0xFF;
    buf[offset + 3] = (value >> 24) & 0xFF;
}

static inline uint16_t swap_u16(uint16_t v)
{
    return (v >> 8) | (v << 8);
}

static inline void rgb565_to_rgb888(uint16_t pixel, uint8_t *r, uint8_t *g, uint8_t *b)
{
    *r = ((pixel >> 11) & 0x1F) << 3;
    *g = ((pixel >> 5) & 0x3F) << 2;
    *b = (pixel & 0x1F) << 3;
}

static esp_err_t bmp_handler(httpd_req_t *req)
{
    camera_fb_t *fb = camera_app_get_frame();
    if (!fb) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Camera capture failed");
        return ESP_FAIL;
    }

    int width = fb->width;
    int height = fb->height;
    int row_size = ((width * 3 + 3) / 4) * 4;
    int pixel_data_size = row_size * height;
    int file_size = 54 + pixel_data_size;

    uint8_t header[54] = {0};
    header[0] = 'B';
    header[1] = 'M';
    write_u32_le(header, 2, file_size);
    write_u32_le(header, 10, 54);
    write_u32_le(header, 14, 40);
    write_u32_le(header, 18, width);
    write_u32_le(header, 22, height);
    write_u16_le(header, 26, 1);
    write_u16_le(header, 28, 24);
    write_u32_le(header, 34, pixel_data_size);

    httpd_resp_set_type(req, "image/bmp");
    esp_err_t res = httpd_resp_send_chunk(req, (const char *)header, sizeof(header));
    if (res != ESP_OK) {
        camera_app_return_frame(fb);
        httpd_resp_send_chunk(req, NULL, 0);
        return res;
    }

    uint8_t *row = malloc(row_size);
    if (!row) {
        camera_app_return_frame(fb);
        httpd_resp_send_chunk(req, NULL, 0);
        return ESP_FAIL;
    }

    uint16_t *pixels = (uint16_t *)fb->buf;

    for (int y = height - 1; y >= 0; y--) {
        memset(row, 0, row_size);

        for (int x = 0; x < width; x++) {
            uint16_t p = pixels[y * width + x];

#if SWAP_RGB565_BYTES
            p = swap_u16(p);
#endif

            uint8_t r, g, b;
            rgb565_to_rgb888(p, &r, &g, &b);

            row[x * 3 + 0] = b;
            row[x * 3 + 1] = g;
            row[x * 3 + 2] = r;
        }

        esp_err_t res = httpd_resp_send_chunk(req, (const char *)row, row_size);
        if (res != ESP_OK) {
            free(row);
            camera_app_return_frame(fb);
            httpd_resp_send_chunk(req, NULL, 0);
            return res;
        }
    }

    free(row);
    camera_app_return_frame(fb);
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
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
        "  img.onload = () => setTimeout(refresh, 1200);"
        "  img.onerror = () => setTimeout(refresh, 2000);"
        "  img.src = '/bmp?t=' + Date.now();"
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

    httpd_uri_t bmp_uri = {
        .uri = "/bmp",
        .method = HTTP_GET,
        .handler = bmp_handler,
        .user_ctx = NULL
    };

    httpd_register_uri_handler(server, &root_uri);
    httpd_register_uri_handler(server, &bmp_uri);

    ESP_LOGI(TAG, "HTTP server started");
    return ESP_OK;
}
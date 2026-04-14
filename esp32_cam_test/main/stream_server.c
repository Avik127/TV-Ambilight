#include "stream_server.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_http_server.h"
#include "camera_app.h"

static const char *TAG = "stream_server";
static const int SW_JPEG_QUALITY = 20;

static inline void rgb565_to_rgb888(uint16_t pixel, uint8_t *r, uint8_t *g, uint8_t *b)
{
    *r = ((pixel >> 11) & 0x1F) << 3;
    *g = ((pixel >> 5) & 0x3F) << 2;
    *b = (pixel & 0x1F) << 3;
}

static esp_err_t send_frame_as_jpeg(httpd_req_t *req, camera_fb_t *fb, bool chunked)
{
    esp_err_t res;
    if (fb->format == PIXFORMAT_JPEG) {
        return chunked
            ? httpd_resp_send_chunk(req, (const char *)fb->buf, fb->len)
            : httpd_resp_send(req, (const char *)fb->buf, fb->len);
    }

    uint8_t *jpg_buf = NULL;
    size_t jpg_len = 0;
    bool converted = frame2jpg(fb, SW_JPEG_QUALITY, &jpg_buf, &jpg_len);
    if (!converted || !jpg_buf) {
        ESP_LOGE(TAG, "Software JPEG conversion failed");
        return ESP_FAIL;
    }

    res = chunked
        ? httpd_resp_send_chunk(req, (const char *)jpg_buf, jpg_len)
        : httpd_resp_send(req, (const char *)jpg_buf, jpg_len);
    free(jpg_buf);
    return res;
}

static esp_err_t jpg_handler(httpd_req_t *req)
{
    camera_fb_t *fb = camera_app_get_frame();
    if (!fb) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Camera capture failed");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "image/jpeg");
    esp_err_t res = send_frame_as_jpeg(req, fb, false);
    camera_app_return_frame(fb);
    return res;
}

static esp_err_t stream_handler(httpd_req_t *req)
{
    if (!camera_app_is_jpeg_mode()) {
        httpd_resp_send_err(req, HTTPD_503_SERVICE_UNAVAILABLE,
                            "MJPEG stream disabled in RGB565 fallback mode");
        return ESP_FAIL;
    }

    static const char *STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=frame";
    static const char *BOUNDARY = "\r\n--frame\r\n";
    static const char *PART = "Content-Type: image/jpeg\r\n\r\n";

    httpd_resp_set_type(req, STREAM_CONTENT_TYPE);
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

    while (1) {
        camera_fb_t *fb = camera_app_get_frame();
        if (!fb) {
            ESP_LOGW(TAG, "Camera capture failed");
            return ESP_FAIL;
        }

        esp_err_t res = httpd_resp_send_chunk(req, BOUNDARY, strlen(BOUNDARY));
        if (res == ESP_OK) {
            res = httpd_resp_send_chunk(req, PART, strlen(PART));
        }
        if (res == ESP_OK) {
            res = send_frame_as_jpeg(req, fb, true);
        }

        camera_app_return_frame(fb);
        if (res != ESP_OK) {
            ESP_LOGI(TAG, "Stream client disconnected");
            return res;
        }
    }
}

static esp_err_t edge_handler(httpd_req_t *req)
{
    camera_fb_t *fb = camera_app_get_frame();
    if (!fb) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Camera capture failed");
        return ESP_FAIL;
    }

    if (fb->format != PIXFORMAT_RGB565) {
        camera_app_return_frame(fb);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Edge endpoint expects RGB565 mode");
        return ESP_FAIL;
    }

    const int width = fb->width;
    const int height = fb->height;
    const int border = 6;
    uint16_t *pixels = (uint16_t *)fb->buf;

    uint32_t rt = 0, gt = 0, bt = 0, ct = 0;
    uint32_t rb = 0, gb = 0, bb = 0, cb = 0;
    uint32_t rl = 0, gl = 0, bl = 0, cl = 0;
    uint32_t rr = 0, gr = 0, br = 0, cr = 0;

    for (int y = 0; y < border && y < height; y++) {
        for (int x = 0; x < width; x++) {
            uint8_t r, g, b;
            rgb565_to_rgb888(pixels[y * width + x], &r, &g, &b);
            rt += r; gt += g; bt += b; ct++;
        }
    }
    for (int y = height - border; y < height; y++) {
        if (y < 0) continue;
        for (int x = 0; x < width; x++) {
            uint8_t r, g, b;
            rgb565_to_rgb888(pixels[y * width + x], &r, &g, &b);
            rb += r; gb += g; bb += b; cb++;
        }
    }
    for (int x = 0; x < border && x < width; x++) {
        for (int y = 0; y < height; y++) {
            uint8_t r, g, b;
            rgb565_to_rgb888(pixels[y * width + x], &r, &g, &b);
            rl += r; gl += g; bl += b; cl++;
        }
    }
    for (int x = width - border; x < width; x++) {
        if (x < 0) continue;
        for (int y = 0; y < height; y++) {
            uint8_t r, g, b;
            rgb565_to_rgb888(pixels[y * width + x], &r, &g, &b);
            rr += r; gr += g; br += b; cr++;
        }
    }

    char payload[220];
    int n = snprintf(
        payload, sizeof(payload),
        "{\"mode\":\"rgb565\",\"top\":{\"r\":%u,\"g\":%u,\"b\":%u},"
        "\"bottom\":{\"r\":%u,\"g\":%u,\"b\":%u},"
        "\"left\":{\"r\":%u,\"g\":%u,\"b\":%u},"
        "\"right\":{\"r\":%u,\"g\":%u,\"b\":%u}}",
        ct ? (unsigned)(rt / ct) : 0, ct ? (unsigned)(gt / ct) : 0, ct ? (unsigned)(bt / ct) : 0,
        cb ? (unsigned)(rb / cb) : 0, cb ? (unsigned)(gb / cb) : 0, cb ? (unsigned)(bb / cb) : 0,
        cl ? (unsigned)(rl / cl) : 0, cl ? (unsigned)(gl / cl) : 0, cl ? (unsigned)(bl / cl) : 0,
        cr ? (unsigned)(rr / cr) : 0, cr ? (unsigned)(gr / cr) : 0, cr ? (unsigned)(br / cr) : 0
    );

    camera_app_return_frame(fb);

    if (n <= 0 || n >= sizeof(payload)) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to build edge payload");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, payload, n);
}

static esp_err_t root_handler(httpd_req_t *req)
{
    static const char html_jpeg[] =
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
    static const char html_rgb565[] =
        "<!doctype html><html><body>"
        "<h1>ESP32-CAM (RGB565 fallback)</h1>"
        "<p>MJPEG stream disabled. Use <code>/jpg</code> for snapshots and <code>/edges</code> for ambilight data.</p>"
        "<img id='cam' style='max-width:100%;image-rendering:pixelated;'>"
        "<script>"
        "const img = document.getElementById('cam');"
        "function refresh(){"
        "  img.onload = () => setTimeout(refresh, 700);"
        "  img.onerror = () => setTimeout(refresh, 1200);"
        "  img.src = '/jpg?t=' + Date.now();"
        "}"
        "refresh();"
        "</script>"
        "</body></html>";

    httpd_resp_set_type(req, "text/html");
    if (camera_app_is_jpeg_mode()) {
        httpd_resp_send(req, html_jpeg, HTTPD_RESP_USE_STRLEN);
    } else {
        httpd_resp_send(req, html_rgb565, HTTPD_RESP_USE_STRLEN);
    }
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

    httpd_uri_t edge_uri = {
        .uri = "/edges",
        .method = HTTP_GET,
        .handler = edge_handler,
        .user_ctx = NULL
    };

    httpd_register_uri_handler(server, &root_uri);
    httpd_register_uri_handler(server, &jpg_uri);
    httpd_register_uri_handler(server, &stream_uri);
    httpd_register_uri_handler(server, &edge_uri);

    ESP_LOGI(TAG, "HTTP server started");
    return ESP_OK;
}

#include "stream_server.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include "esp_log.h"
#include "esp_http_server.h"
#include "camera_app.h"

static const char *TAG = "stream_server";
static const int SW_JPEG_QUALITY = 12;
static const int DEFAULT_SEGMENTS_PER_EDGE = 16;

static inline void rgb565_to_rgb888(uint16_t pixel, uint8_t *r, uint8_t *g, uint8_t *b)
{
    *r = ((pixel >> 11) & 0x1F) << 3;
    *g = ((pixel >> 5) & 0x3F) << 2;
    *b = (pixel & 0x1F) << 3;
}

static esp_err_t send_frame_as_jpeg(httpd_req_t *req, camera_fb_t *fb)
{
    uint8_t *jpg_buf = NULL;
    size_t jpg_len = 0;

    if (fb->format == PIXFORMAT_JPEG) {
        return httpd_resp_send(req, (const char *)fb->buf, fb->len);
    }

    bool converted = frame2jpg(fb, SW_JPEG_QUALITY, &jpg_buf, &jpg_len);
    if (!converted || !jpg_buf) {
        ESP_LOGE(TAG, "Software JPEG conversion failed");
        return ESP_FAIL;
    }

    esp_err_t res = httpd_resp_send(req, (const char *)jpg_buf, jpg_len);
    free(jpg_buf);
    return res;
}

static int clamp_int(int value, int min_value, int max_value)
{
    if (value < min_value) {
        return min_value;
    }
    if (value > max_value) {
        return max_value;
    }
    return value;
}

static int read_query_int(httpd_req_t *req, const char *key, int fallback, int min_value, int max_value)
{
    size_t query_len = httpd_req_get_url_query_len(req);
    if (query_len == 0) {
        return fallback;
    }

    char *query = calloc(1, query_len + 1);
    if (!query) {
        return fallback;
    }

    int value = fallback;
    if (httpd_req_get_url_query_str(req, query, query_len + 1) == ESP_OK) {
        char param[16] = {0};
        if (httpd_query_key_value(query, key, param, sizeof(param)) == ESP_OK) {
            int parsed = atoi(param);
            value = clamp_int(parsed, min_value, max_value);
        }
    }

    free(query);
    return value;
}

static bool json_append(char *dst, size_t cap, size_t *len, const char *fmt, ...)
{
    if (!dst || !len || *len >= cap) {
        return false;
    }

    va_list args;
    va_start(args, fmt);
    int written = vsnprintf(dst + *len, cap - *len, fmt, args);
    va_end(args);

    if (written < 0) {
        return false;
    }

    size_t w = (size_t)written;
    if (w >= (cap - *len)) {
        return false;
    }

    *len += w;
    return true;
}

static esp_err_t jpg_handler(httpd_req_t *req)
{
    camera_fb_t *fb = camera_app_get_frame();
    if (!fb) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Camera capture failed");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "image/jpeg");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store, no-cache, must-revalidate");

    esp_err_t res = send_frame_as_jpeg(req, fb);
    camera_app_return_frame(fb);
    return res;
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
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Expected RGB565 frame format");
        return ESP_FAIL;
    }

    const int width = fb->width;
    const int height = fb->height;
    const int segments = read_query_int(req, "segments", DEFAULT_SEGMENTS_PER_EDGE, 4, 64);
    const int depth = read_query_int(req, "depth", 12, 2, 48);
    uint16_t *pixels = (uint16_t *)fb->buf;

    size_t json_cap = 512 + (size_t)segments * 4 * 40;
    char *json = malloc(json_cap);
    if (!json) {
        camera_app_return_frame(fb);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
        return ESP_FAIL;
    }

    size_t n = 0;
    if (!json_append(json, json_cap, &n,
                     "{\"mode\":\"rgb565\",\"width\":%d,\"height\":%d,\"segments_per_edge\":%d,\"sample_depth\":%d,\"edges\":{",
                     width, height, segments, depth)) {
        free(json);
        camera_app_return_frame(fb);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to build edge payload");
        return ESP_FAIL;
    }

    const char *edge_names[4] = {"top", "right", "bottom", "left"};
    for (int edge = 0; edge < 4; edge++) {
        if (!json_append(json, json_cap, &n, "\"%s\":[", edge_names[edge])) {
            free(json);
            camera_app_return_frame(fb);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to build edge payload");
            return ESP_FAIL;
        }

        for (int s = 0; s < segments; s++) {
            long long sum_r = 0;
            long long sum_g = 0;
            long long sum_b = 0;
            int count = 0;

            if (edge == 0 || edge == 2) {
                int x0 = (s * width) / segments;
                int x1 = ((s + 1) * width) / segments;
                int y0 = (edge == 0) ? 0 : (height - depth);
                int y1 = (edge == 0) ? depth : height;
                y0 = clamp_int(y0, 0, height);
                y1 = clamp_int(y1, 0, height);

                for (int y = y0; y < y1; y++) {
                    for (int x = x0; x < x1; x++) {
                        uint8_t r, g, b;
                        rgb565_to_rgb888(pixels[y * width + x], &r, &g, &b);
                        sum_r += r;
                        sum_g += g;
                        sum_b += b;
                        count++;
                    }
                }
            } else {
                int y0 = (s * height) / segments;
                int y1 = ((s + 1) * height) / segments;
                int x0 = (edge == 1) ? (width - depth) : 0;
                int x1 = (edge == 1) ? width : depth;
                x0 = clamp_int(x0, 0, width);
                x1 = clamp_int(x1, 0, width);

                for (int y = y0; y < y1; y++) {
                    for (int x = x0; x < x1; x++) {
                        uint8_t r, g, b;
                        rgb565_to_rgb888(pixels[y * width + x], &r, &g, &b);
                        sum_r += r;
                        sum_g += g;
                        sum_b += b;
                        count++;
                    }
                }
            }

            int avg_r = (count > 0) ? (int)(sum_r / count) : 0;
            int avg_g = (count > 0) ? (int)(sum_g / count) : 0;
            int avg_b = (count > 0) ? (int)(sum_b / count) : 0;

            if (!json_append(json, json_cap, &n, "%s{\"r\":%d,\"g\":%d,\"b\":%d}",
                             (s == 0) ? "" : ",", avg_r, avg_g, avg_b)) {
                free(json);
                camera_app_return_frame(fb);
                httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to build edge payload");
                return ESP_FAIL;
            }
        }

        if (!json_append(json, json_cap, &n, "]%s", (edge == 3) ? "" : ",")) {
            free(json);
            camera_app_return_frame(fb);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to build edge payload");
            return ESP_FAIL;
        }
    }

    if (!json_append(json, json_cap, &n, "}}\n")) {
        free(json);
        camera_app_return_frame(fb);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to build edge payload");
        return ESP_FAIL;
    }
    camera_app_return_frame(fb);

    httpd_resp_set_type(req, "application/json");
    esp_err_t res = httpd_resp_send(req, json, n);
    free(json);
    return res;
}

static esp_err_t root_handler(httpd_req_t *req)
{
    static const char html[] =
        "<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<style>body{font-family:sans-serif;max-width:960px;margin:20px auto;padding:0 12px;}"
        "img{max-width:100%;height:auto;border:1px solid #888;background:#222;}"
        "button{padding:10px 14px;font-size:16px;margin-right:8px;}code{background:#eee;padding:2px 4px;}</style>"
        "</head><body><h1>ESP32-CAM Snapshot</h1>"
        "<p>Streaming is disabled. Press capture to load one high-quality image.</p>"
        "<button id='captureBtn'>Capture image</button><span id='status'>Idle</span>"
        "<p><small>Edge data: <code>/edges?segments=16&depth=12</code></small></p>"
        "<img id='snapshot' alt='Camera snapshot'/>"
        "<script>"
        "const btn=document.getElementById('captureBtn');"
        "const img=document.getElementById('snapshot');"
        "const status=document.getElementById('status');"
        "btn.onclick=()=>{"
        "  btn.disabled=true;"
        "  status.textContent='Capturing...';"
        "  const url='/jpg?t='+Date.now();"
        "  img.onload=()=>{status.textContent='Loaded';btn.disabled=false;};"
        "  img.onerror=()=>{status.textContent='Failed to load image';btn.disabled=false;};"
        "  img.src=url;"
        "};"
        "</script></body></html>";

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

    httpd_uri_t edge_uri = {
        .uri = "/edges",
        .method = HTTP_GET,
        .handler = edge_handler,
        .user_ctx = NULL
    };

    httpd_register_uri_handler(server, &root_uri);
    httpd_register_uri_handler(server, &jpg_uri);
    httpd_register_uri_handler(server, &edge_uri);

    ESP_LOGI(TAG, "HTTP server started");
    return ESP_OK;
}

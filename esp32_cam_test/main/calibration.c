#include "calibration.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"

static const char *TAG           = "calibration";
static const char *NVS_NAMESPACE = "ambilight";
static const char *NVS_KEY       = "corners";

/* On-disk layout — 8 int16_t values, 16 bytes total. */
typedef struct {
    int16_t tl_x, tl_y;
    int16_t tr_x, tr_y;
    int16_t br_x, br_y;
    int16_t bl_x, bl_y;
} corners_blob_t;

static calibration_t s_cal = { .valid = false };

esp_err_t calibration_init(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &h);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "No calibration stored");
        return ESP_OK;
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open failed: %s", esp_err_to_name(err));
        return err;
    }

    corners_blob_t blob;
    size_t len = sizeof(blob);
    err = nvs_get_blob(h, NVS_KEY, &blob, &len);
    nvs_close(h);

    if (err == ESP_OK && len == sizeof(blob)) {
        s_cal.tl.x = blob.tl_x;  s_cal.tl.y = blob.tl_y;
        s_cal.tr.x = blob.tr_x;  s_cal.tr.y = blob.tr_y;
        s_cal.br.x = blob.br_x;  s_cal.br.y = blob.br_y;
        s_cal.bl.x = blob.bl_x;  s_cal.bl.y = blob.bl_y;
        s_cal.valid = true;
        ESP_LOGI(TAG, "Loaded: TL(%d,%d) TR(%d,%d) BR(%d,%d) BL(%d,%d)",
                 s_cal.tl.x, s_cal.tl.y,
                 s_cal.tr.x, s_cal.tr.y,
                 s_cal.br.x, s_cal.br.y,
                 s_cal.bl.x, s_cal.bl.y);
    } else {
        ESP_LOGI(TAG, "No calibration stored");
    }
    return ESP_OK;
}

calibration_t calibration_get(void)
{
    return s_cal;
}

esp_err_t calibration_save(const calibration_t *c)
{
    corners_blob_t blob = {
        .tl_x = c->tl.x, .tl_y = c->tl.y,
        .tr_x = c->tr.x, .tr_y = c->tr.y,
        .br_x = c->br.x, .br_y = c->br.y,
        .bl_x = c->bl.x, .bl_y = c->bl.y,
    };

    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open failed: %s", esp_err_to_name(err));
        return err;
    }

    err = nvs_set_blob(h, NVS_KEY, &blob, sizeof(blob));
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);

    if (err == ESP_OK) {
        s_cal    = *c;
        s_cal.valid = true;
        ESP_LOGI(TAG, "Saved: TL(%d,%d) TR(%d,%d) BR(%d,%d) BL(%d,%d)",
                 s_cal.tl.x, s_cal.tl.y,
                 s_cal.tr.x, s_cal.tr.y,
                 s_cal.br.x, s_cal.br.y,
                 s_cal.bl.x, s_cal.bl.y);
    } else {
        ESP_LOGE(TAG, "Save failed: %s", esp_err_to_name(err));
    }
    return err;
}

esp_err_t calibration_clear(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }
    nvs_erase_key(h, NVS_KEY);
    err = nvs_commit(h);
    nvs_close(h);
    s_cal.valid = false;
    ESP_LOGI(TAG, "Calibration cleared");
    return err;
}

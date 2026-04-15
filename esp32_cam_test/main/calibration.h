#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

typedef struct {
    int16_t x, y;
} cal_point_t;

typedef struct {
    cal_point_t tl;   /* top-left     */
    cal_point_t tr;   /* top-right    */
    cal_point_t br;   /* bottom-right */
    cal_point_t bl;   /* bottom-left  */
    bool valid;
} calibration_t;

/* Load saved corners from NVS. Call after nvs_flash_init(). */
esp_err_t calibration_init(void);

/* Get current calibration (may have .valid == false if never saved). */
calibration_t calibration_get(void);

/* Persist corners to NVS and update in-memory state. */
esp_err_t calibration_save(const calibration_t *c);

/* Erase persisted corners from NVS and clear in-memory state. */
esp_err_t calibration_clear(void);

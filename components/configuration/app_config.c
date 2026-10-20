#include "app_config.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "config";
static SemaphoreHandle_t s_lock;

esp_err_t cfg_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition unusable (%s), erasing", esp_err_to_name(err));
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    if (err == ESP_OK && !s_lock) {
        s_lock = xSemaphoreCreateMutex();
        if (!s_lock) {
            err = ESP_ERR_NO_MEM;
        }
    }
    return err;
}

esp_err_t cfg_get_str(const char *ns, const char *key, char *buf, size_t size)
{
    nvs_handle_t h;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t err = nvs_open(ns, NVS_READONLY, &h);
    if (err == ESP_OK) {
        size_t len = size;
        err = nvs_get_str(h, key, buf, &len);
        nvs_close(h);
    }
    xSemaphoreGive(s_lock);
    if (err != ESP_OK && size > 0) {
        buf[0] = '\0';
    }
    return err;
}

esp_err_t cfg_set_str(const char *ns, const char *key, const char *value)
{
    nvs_handle_t h;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t err = nvs_open(ns, NVS_READWRITE, &h);
    if (err == ESP_OK) {
        err = nvs_set_str(h, key, value);
        if (err == ESP_OK) {
            err = nvs_commit(h);
        }
        nvs_close(h);
    }
    xSemaphoreGive(s_lock);
    /* value intentionally not logged: may be a secret */
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "write %s/%s failed: %s", ns, key, esp_err_to_name(err));
    }
    return err;
}

esp_err_t cfg_erase_key(const char *ns, const char *key)
{
    nvs_handle_t h;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t err = nvs_open(ns, NVS_READWRITE, &h);
    if (err == ESP_OK) {
        err = nvs_erase_key(h, key);
        if (err == ESP_OK || err == ESP_ERR_NVS_NOT_FOUND) {
            err = nvs_commit(h);
        }
        nvs_close(h);
    }
    xSemaphoreGive(s_lock);
    return err;
}

esp_err_t cfg_erase_namespace(const char *ns)
{
    nvs_handle_t h;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t err = nvs_open(ns, NVS_READWRITE, &h);
    if (err == ESP_OK) {
        err = nvs_erase_all(h);
        if (err == ESP_OK) {
            err = nvs_commit(h);
        }
        nvs_close(h);
    }
    xSemaphoreGive(s_lock);
    return err;
}

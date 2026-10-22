#include "ota_cmd.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include "burst_serial.h"
#include "esp_ota_ops.h"
#include "esp_rom_crc.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static void reply(const char *s) { (void)burst_serial_send(s, strlen(s)); }

void ota_boot_confirm(void) {
    /* Harmless without rollback support in the bootloader. */
    (void)esp_ota_mark_app_valid_cancel_rollback();
}

static bool receive_image(uint32_t size, uint32_t crc_want) {
    const esp_partition_t *next = esp_ota_get_next_update_partition(NULL);
    if (!next) { reply("ERR ota_no_partition\n"); return false; }
    if (size < 4096 || size > next->size) { reply("ERR ota_size\n"); return false; }
    esp_ota_handle_t h;
    if (esp_ota_begin(next, size, &h) != ESP_OK) { reply("ERR ota_begin\n"); return false; }
    reply("OK OTA\n");
    static uint8_t chunk[2048];
    uint32_t crc = 0, done = 0;
    int64_t last = esp_timer_get_time();
    while (done < size) {
        size_t want = size - done > sizeof(chunk) ? sizeof(chunk) : size - done;
        int n = burst_serial_read_raw(chunk, want);
        if (n <= 0) {
            if (esp_timer_get_time() - last > 10000000) { esp_ota_abort(h); reply("ERR ota_timeout\n"); return false; }
            vTaskDelay(1);
            continue;
        }
        last = esp_timer_get_time();
        crc = esp_rom_crc32_le(crc, chunk, n);
        if (esp_ota_write(h, chunk, n) != ESP_OK) { esp_ota_abort(h); reply("ERR ota_write\n"); return false; }
        done += n;
    }
    if (crc != crc_want) { esp_ota_abort(h); reply("ERR ota_crc\n"); return false; }
    if (esp_ota_end(h) != ESP_OK) { reply("ERR ota_image\n"); return false; }
    if (esp_ota_set_boot_partition(next) != ESP_OK) { reply("ERR ota_boot\n"); return false; }
    char line[64];
    snprintf(line, sizeof(line), "OK OTA DONE %s\n", next->label);
    reply(line);
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
    return true;
}

bool ota_command(char *line) {
    uint32_t size, crc; char extra;
    if (!strcmp(line, "OTA?")) {
        const esp_partition_t *run = esp_ota_get_running_partition();
        const esp_partition_t *next = esp_ota_get_next_update_partition(NULL);
        esp_ota_img_states_t st = ESP_OTA_IMG_UNDEFINED;
        esp_ota_get_state_partition(run, &st);
        char h[96];
        snprintf(h, sizeof(h), "OTA %s %s %d\n", run ? run->label : "?", next ? next->label : "-", (int)st);
        reply(h);
        return true;
    }
    if (sscanf(line, "OTA %" SCNu32 " %" SCNx32 " %c", &size, &crc, &extra) == 2) {
        receive_image(size, crc);
        return true;
    }
    return false;
}

#pragma once
/* In-band firmware update over the burst transport.
 *   OTA?                  -> "OTA <running> <next> <state>"
 *   OTA <size> <crc32hex> -> "OK OTA", then <size> raw bytes (CRC32 of the
 *                            image, esp_rom_crc32_le), "OK OTA DONE <next>",
 *                            boot partition switched, restart. */
#include <stdbool.h>
void ota_boot_confirm(void);      /* call once after boot */
bool ota_command(char *line);

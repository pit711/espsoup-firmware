/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 ESPsoup contributors / Peter Holzhauser
 *
 * ESP32-C6 frame-side speedups for CAP/CAP16/CAP20/RXRUN (receive only).
 *
 * On the C6 the acquisition itself is already register-level (chip.h
 * stock_capture, ~209 us for 16380 samples at 80 MS/s). The remaining
 * per-frame CPU time was spent around it: sentinel fill and check over all
 * words, byte-wise IQ8 packing, and the ROM crc32_le (byte-table loop,
 * ~9.5 instructions per byte over 32 KiB). This header replaces those three
 * with:
 *   - sentinels on a SENTINEL_STRIDE grid plus the last SENTINEL_TAIL words
 *     (the dump writer is sequential and completion is polled, so an
 *     unfinished or misrouted dump still leaves sentinels on the grid);
 *   - IQ8 packing two samples per 32-bit store with the CRC folded into the
 *     same pass;
 *   - CRC32 slicing-by-4 from a 4 KiB RAM table.
 * The wire format is unchanged: "DATA n crc us\n", payload, and the CRC is
 * bit-identical to esp_rom_crc32_le(0, payload, bytes). Host-verified
 * against the byte-wise reference (tests/test_c6_frame.py).
 */
#pragma once
#include <stdint.h>
#include <stddef.h>

#define SENTINEL_WORD 0xa5a0055au
#define SENTINEL_STRIDE 64u
#define SENTINEL_TAIL 64u

static uint32_t c6_crc_tab[4][256];

static void c6_crc_init(void) {
    if (c6_crc_tab[0][1]) return;
    for (unsigned i = 0; i < 256; i++) {
        uint32_t c = i;
        for (unsigned k = 0; k < 8; k++) c = (c & 1u) ? 0xedb88320u ^ (c >> 1) : c >> 1;
        c6_crc_tab[0][i] = c;
    }
    for (unsigned i = 0; i < 256; i++) {
        uint32_t c = c6_crc_tab[0][i];
        for (unsigned t = 1; t < 4; t++) {
            c = c6_crc_tab[0][c & 0xffu] ^ (c >> 8);
            c6_crc_tab[t][i] = c;
        }
    }
}

/* One little-endian word into the running (inverted) CRC state. The four
 * table bases are loop invariants the compiler keeps in registers. */
static inline uint32_t c6_crc_word(uint32_t crc, uint32_t w) {
    const uint32_t *t0 = c6_crc_tab[0], *t1 = c6_crc_tab[1], *t2 = c6_crc_tab[2], *t3 = c6_crc_tab[3];
    uint32_t x = crc ^ w;
    return t3[x & 0xffu] ^ t2[(x >> 8) & 0xffu] ^ t1[(x >> 16) & 0xffu] ^ t0[x >> 24];
}
static inline uint32_t c6_crc_byte(uint32_t crc, uint8_t b) {
    return c6_crc_tab[0][(crc ^ b) & 0xffu] ^ (crc >> 8);
}

/* Same result as esp_rom_crc32_le(crc, buf, len); buf must be word aligned. */
static uint32_t c6_crc32(uint32_t crc, const uint32_t *w, size_t len) {
    crc = ~crc;
    size_t words = len / 4;
    for (size_t j = 0; j < words; j++) crc = c6_crc_word(crc, w[j]);
    const uint8_t *p = (const uint8_t *)(w + words);
    for (size_t j = 0; j < (len & 3u); j++) crc = c6_crc_byte(crc, p[j]);
    return ~crc;
}

/* In-place IQ8 packing (as pack_iq8: I = bits 2-9, Q = bits 12-19 of each
 * word) of n samples in b, two samples per word store. Returns the CRC32 of
 * the 2*n packed bytes. Output word k only overwrites input word k <= 2k,
 * which was consumed in this or an earlier iteration. */
static uint32_t c6_pack_iq8_crc(uint32_t *b, unsigned n) {
    uint32_t crc = 0xffffffffu;
    unsigned j = 0;
    for (; j + 1 < n; j += 2) {
        uint32_t a = b[j], c = b[j + 1];
        uint32_t w = ((a >> 2) & 0xffu) | (((a >> 12) & 0xffu) << 8) |
                     (((c >> 2) & 0xffu) << 16) | (((c >> 12) & 0xffu) << 24);
        b[j >> 1] = w;
        crc = c6_crc_word(crc, w);
    }
    if (j < n) {
        uint32_t a = b[j];
        uint8_t *p = (uint8_t *)b + 2u * j;
        p[0] = (uint8_t)(a >> 2);
        p[1] = (uint8_t)(a >> 12);
        crc = c6_crc_byte(crc, p[0]);
        crc = c6_crc_byte(crc, p[1]);
    }
    return ~crc;
}

/* Sentinels for n samples: every SENTINEL_STRIDE-th word up to the tail, the
 * whole tail, and the four overrun guard words behind the buffer. */
static void c6_sentinel_fill(uint32_t *b, unsigned n) {
    unsigned tail = n > SENTINEL_TAIL ? n - SENTINEL_TAIL : 0;
    for (unsigned j = 0; j < tail; j += SENTINEL_STRIDE) b[j] = SENTINEL_WORD;
    for (unsigned j = tail; j < n; j++) b[j] = SENTINEL_WORD;
    for (unsigned j = 0; j < 4; j++) b[n + j] = 0x5a5aa5a5u ^ j;
}

/* 0 = all sentinels overwritten and guards intact, 1 = sentinel left
 * (unfinished dump), 2 = guard word changed (overrun). */
static int c6_sentinel_check(const uint32_t *b, unsigned n) {
    unsigned tail = n > SENTINEL_TAIL ? n - SENTINEL_TAIL : 0;
    for (unsigned j = 0; j < tail; j += SENTINEL_STRIDE) if (b[j] == SENTINEL_WORD) return 1;
    for (unsigned j = tail; j < n; j++) if (b[j] == SENTINEL_WORD) return 1;
    for (unsigned j = 0; j < 4; j++) if (b[n + j] != (0x5a5aa5a5u ^ j)) return 2;
    return 0;
}

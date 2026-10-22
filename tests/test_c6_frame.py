"""Host check of main/families/c5_c6_c61/c6_frame.h: the slicing-by-4 CRC and the
fused IQ8 pack+CRC must match the byte-wise ROM crc32_le over the stock
pack_iq8 output, and the grid sentinels must detect unfinished and overrun dumps."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

CHECK = r'''
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "c6_frame.h"
static uint32_t ref_tab[256];
static void ref_init(void) {
    for (unsigned i = 0; i < 256; i++) {
        uint32_t c = i;
        for (unsigned k = 0; k < 8; k++) c = (c & 1u) ? 0xedb88320u ^ (c >> 1) : c >> 1;
        ref_tab[i] = c;
    }
}
/* esp_rom_crc32_le reference (components/esp_rom/patches/esp_rom_crc.c) */
static uint32_t ref_crc(uint32_t crc, const uint8_t *buf, uint32_t len) {
    crc = ~crc;
    for (uint32_t i = 0; i < len; i++) crc = ref_tab[(crc ^ buf[i]) & 0xff] ^ (crc >> 8);
    return ~crc;
}
static void ref_pack_iq8(uint32_t *b, unsigned n) {   /* stock pack_iq8 */
    uint8_t *p = (uint8_t *)b;
    for (unsigned j = 0; j < n; j++) { uint32_t w = b[j]; p[2*j] = (w >> 2) & 255; p[2*j+1] = (w >> 12) & 255; }
}
static uint32_t rnd(void) { static uint32_t s = 0x12345678u; s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
#define WORDS (16384u + 4u)
static uint32_t A[WORDS], B[WORDS];
int main(void) {
    ref_init(); c6_crc_init();
    unsigned fails = 0, cases = 0;
    { uint32_t w[3]; memcpy(w, "123456789\0\0\0", 12); cases++; if (c6_crc32(0, w, 9) != 0xcbf43926u) { printf("FAIL known vector\n"); fails++; } }
    for (unsigned t = 0; t < 300; t++) {
        unsigned n = t < 10 ? 256 + t : (rnd() % (16380 - 256 + 1)) + 256;
        if (t == 20) n = 16380;
        if (t == 21) n = 16379;
        for (unsigned j = 0; j < WORDS; j++) A[j] = rnd();
        if (t & 1) for (unsigned j = 0; j < n; j++) A[j] &= 0xfffffu;
        memcpy(B, A, sizeof A);
        for (unsigned len = n * 4 - 3; len <= n * 4; len++) {
            uint32_t r = ref_crc(0, (const uint8_t *)A, len), c = c6_crc32(0, A, len);
            cases++; if (r != c) { printf("FAIL crc len=%u ref=%08x got=%08x\n", len, r, c); fails++; }
        }
        { uint32_t seed = rnd(), r = ref_crc(seed, (const uint8_t *)A, n * 4), c = c6_crc32(seed, A, n * 4);
          cases++; if (r != c) { printf("FAIL chained\n"); fails++; } }
        ref_pack_iq8(A, n);
        uint32_t r = ref_crc(0, (const uint8_t *)A, n * 2), c = c6_pack_iq8_crc(B, n);
        cases++; if (r != c || memcmp(A, B, n * 2)) { printf("FAIL pack n=%u\n", n); fails++; }
        c6_sentinel_fill(B, n);
        cases++; if (c6_sentinel_check(B, n) != 1) { printf("FAIL sentinel untouched n=%u\n", n); fails++; }
        for (unsigned j = 0; j < n; j++) B[j] = rnd() & 0xfffffu;
        cases++; if (c6_sentinel_check(B, n) != 0) { printf("FAIL sentinel clean n=%u\n", n); fails++; }
        unsigned cut = n - 1 - (rnd() % 64);
        c6_sentinel_fill(B, n); for (unsigned j = 0; j < cut; j++) B[j] = rnd() & 0xfffffu;
        cases++; if (c6_sentinel_check(B, n) != 1) { printf("FAIL sentinel short n=%u\n", n); fails++; }
        c6_sentinel_fill(B, n); for (unsigned j = 0; j < n; j++) B[j] = rnd() & 0xfffffu; B[n + (rnd() & 3)] = 0x12345u;
        cases++; if (c6_sentinel_check(B, n) != 2) { printf("FAIL overrun n=%u\n", n); fails++; }
    }
    printf("%u cases, %u failures\n", cases, fails);
    return fails ? 1 : 0;
}
'''

class C6Frame(unittest.TestCase):
    @unittest.skipUnless(shutil.which('cc'), 'Host C compiler unavailable')
    def test_c6_frame(self):
        family = Path(__file__).resolve().parents[1] / 'main/families/c5_c6_c61'
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'frame.c'
            path.write_text(CHECK)
            binary = Path(tmp) / 'frame'
            subprocess.run(['cc', '-std=c11', '-O2', '-I' + str(family), str(path), '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)

if __name__ == '__main__':
    unittest.main()

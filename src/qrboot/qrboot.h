#ifndef QRBOOT_QRBOOT_H
#define QRBOOT_QRBOOT_H
/* What a QR boot loader lends the app it loaded, as imports from module "qrboot". The screen is 240 x
 * 240 RGB565 and the camera 320 x 240 greyscale; the loader already reads QR codes, so it lends that too */
#include <stdint.h>

#define QB_IMPORT(name) __attribute__((import_module("qrboot"), import_name(#name)))

QB_IMPORT(draw_line) void qb_draw_line(int y, const uint16_t *rgb565);
/* UI_KEY_* from ui.h, or -1 */
QB_IMPORT(key) int qb_key(void);
QB_IMPORT(time_us) uint64_t qb_time_us(void);
QB_IMPORT(sleep_ms) void qb_sleep_ms(uint32_t ms);
/* 1 if a frame was written */
QB_IMPORT(capture) int qb_capture(uint8_t *grey, int w, int h);
/* kind 0 any, 1 SeedQR, 2 animated UR. The length written to out, or 0 none, -1 unreadable, -3 wrong kind */
QB_IMPORT(qr_decode) int qb_qr_decode(const uint8_t *grey, int w, int h, int kind, uint8_t *out, int cap);
/* Only for BIP340's aux data, where a bad source costs nothing */
QB_IMPORT(random) void qb_random(uint8_t *buf, int len);
QB_IMPORT(log) void qb_log(const char *text, int len);

#endif

/* The board drivers main.c calls, backed by the loader's imports (qrboot.h), as web/hal.c backs them by
 * the browser */
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/rand.h"
#include "qrboot.h"
#include "st7789.h"
#include "buttons.h"
#include "camera.h"

static int line_y;

void stdio_init_all(void) {
}

void sleep_ms(uint32_t ms) {
    qb_sleep_ms(ms);
}

uint64_t time_us_64(void) {
    return qb_time_us();
}

uint32_t get_rand_32(void) {
    uint32_t v;
    qb_random((uint8_t *)&v, sizeof(v));
    return v;
}

/* Built with -fno-builtin-printf/puts/putchar, so every printf, puts and putchar in main.c lands here instead of in
 * WASI */
int printf(const char *fmt, ...) {
    char buf[512];
    va_list ap;
    int n;
    va_start(ap, fmt);
    n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n > 0) qb_log(buf, n < (int)sizeof(buf) ? n : (int)sizeof(buf) - 1);
    return n;
}

void st7789_init(void) {
}

void st7789_begin_frame(void) {
    line_y = 0;
}

void st7789_write_line(const uint16_t *line) {
    qb_draw_line(line_y++, line);
}

void buttons_init(void) {
}

int buttons_poll(void) {
    return qb_key();
}

const camera_model_t camera_ov7670 = {"loader", 0, false, 0};

void camera_bus_init(const camera_model_t *model) {
    (void)model;
}

bool camera_init(const camera_model_t *model) {
    (void)model;
    return true;
}

bool camera_capture(uint8_t *buf, uint32_t timeout_ms) {
    (void)timeout_ms;
    return qb_capture(buf, CAMERA_W, CAMERA_H) == 1;
}

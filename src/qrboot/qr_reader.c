/* qr_reader.h on the loader's reader: the app keeps the frame, the loader decodes it */
#include "qr_reader.h"
#include <stdlib.h>
#include "camera.h"
#include "qrboot.h"
#include "wipe.h"

#define FRAME_SIZE ((size_t)CAMERA_W * CAMERA_H)
#define OUTPUT_CAP 3072 /* a version 40 code in byte mode is 2953 bytes */

static uint8_t *memory;
void (*qr_reader_on_panic)(void);

bool qr_reader_open(void) {
    return (memory = malloc(FRAME_SIZE + OUTPUT_CAP)) != NULL;
}

uint8_t *qr_reader_frame(void) {
    return memory;
}

int32_t qr_reader_read(qr_reader_kind_t kind) {
    return qb_qr_decode(memory, CAMERA_W, CAMERA_H, (int)kind, memory + FRAME_SIZE, OUTPUT_CAP);
}

const uint8_t *qr_reader_output(void) {
    return memory + FRAME_SIZE;
}

void qr_reader_close(void) {
    if (!memory) return;
    wipe(memory, FRAME_SIZE + OUTPUT_CAP);
    free(memory);
    memory = NULL;
}

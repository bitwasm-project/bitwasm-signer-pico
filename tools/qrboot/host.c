/* A QR boot loader on a Mac, without a window: runs an app on WAMR's classic interpreter, lending it
 * the imports in src/qrboot/qrboot.h. Keys and camera frames come from a script, the screen goes to a PNG.
 *
 *   host script.txt shot.png app.wasm
 *   host script.txt shot.png --qr <sha256> qr-*.png    boot from the printed codes, as the device would
 *
 * Script lines: "key SELECT|UP|DOWN|LEFT|RIGHT|A|B|C [count]", "idle polls" and "frames a.png b.png ...". A key waits
 * until the app has stopped capturing, so a scan is never cancelled by the next step's key */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>
#include <CommonCrypto/CommonDigest.h>
#include "wasm_export.h"

#define SCREEN 240
#define CAM_W 320
#define CAM_H 240
#define MAX_FRAMES 512
#define B36 "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ"
#define IDLE_POLLS 2000 /* key polls with nothing left in the script before the run ends */

size_t qr_reader_state_size(void);
size_t qr_reader_output_cap(void);
void qr_reader_state_init(void *state);
int qr_reader_decode(void *state, const uint8_t *px, size_t w, size_t h, int seedqr, int animated, uint8_t *out,
                     uint32_t budget);

static uint16_t screen[SCREEN * SCREEN];
static uint8_t *frames[MAX_FRAMES];
static int n_frames, next_frame, polls_since_capture;
static char **script;
static int script_len, script_at, key_left, idle;
static int32_t cur_key;
static int idle_left;
static uint64_t clock_us;
static void *qr_state;
static uint8_t *qr_out;

static int fail(const char *msg, const char *arg) {
    fprintf(stderr, "host: %s %s\n", msg, arg ? arg : "");
    exit(1);
}

/* PNG to greyscale: 8-bit grey, grey+alpha, RGB or RGBA, any filter, no interlace */
static uint8_t *read_png(const char *path, int *w, int *h) {
    FILE *f = fopen(path, "rb");
    uint8_t *buf, *idat = NULL, *raw, *grey;
    long size;
    size_t at = 8, idat_len = 0;
    int type = 0, bpp;
    if (!f) fail("cannot open", path);
    fseek(f, 0, SEEK_END), size = ftell(f), fseek(f, 0, SEEK_SET);
    buf = malloc((size_t)size);
    if (fread(buf, 1, (size_t)size, f) != (size_t)size) fail("cannot read", path);
    fclose(f);
    while (at + 12 <= (size_t)size) {
        uint32_t len = (uint32_t)buf[at] << 24 | buf[at + 1] << 16 | buf[at + 2] << 8 | buf[at + 3];
        const uint8_t *d = buf + at + 8;
        if (!memcmp(buf + at + 4, "IHDR", 4)) {
            *w = d[0] << 24 | d[1] << 16 | d[2] << 8 | d[3], *h = d[4] << 24 | d[5] << 16 | d[6] << 8 | d[7];
            type = d[9];
            if (d[8] != 8 || d[12]) fail("only 8-bit non-interlaced PNG:", path);
        } else if (!memcmp(buf + at + 4, "IDAT", 4)) {
            idat = realloc(idat, idat_len + len);
            memcpy(idat + idat_len, d, len), idat_len += len;
        }
        at += 12 + len;
    }
    bpp = type == 0 ? 1 : type == 4 ? 2 : type == 2 ? 3 : 4;
    uLongf raw_len = (uLongf)(*w * bpp + 1) * (uLongf)*h;
    raw = malloc(raw_len);
    if (uncompress(raw, &raw_len, idat, idat_len) != Z_OK) fail("bad PNG data:", path);
    size_t stride = (size_t)*w * bpp;
    uint8_t *px = calloc(stride * (size_t)*h, 1);
    for (int y = 0; y < *h; y++) {
        const uint8_t *src = raw + (size_t)y * (stride + 1) + 1;
        uint8_t *row = px + (size_t)y * stride, *up = y ? row - stride : NULL;
        for (size_t x = 0; x < stride; x++) {
            int a = x >= (size_t)bpp ? row[x - bpp] : 0, b = up ? up[x] : 0,
                c = x >= (size_t)bpp && up ? up[x - bpp] : 0;
            int p = a + b - c, pa = abs(p - a), pb = abs(p - b), pc = abs(p - c), pred;
            switch (src[-1]) {
            case 1:
                pred = a;
                break;
            case 2:
                pred = b;
                break;
            case 3:
                pred = (a + b) / 2;
                break;
            case 4:
                pred = pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
                break;
            default:
                pred = 0;
            }
            row[x] = (uint8_t)(src[x] + pred);
        }
    }
    grey = malloc((size_t)*w * (size_t)*h);
    for (size_t i = 0; i < (size_t)*w * (size_t)*h; i++)
        grey[i] =
            bpp < 3 ? px[i * bpp] : (uint8_t)((px[i * bpp] * 77 + px[i * bpp + 1] * 150 + px[i * bpp + 2] * 29) >> 8);
    free(buf), free(idat), free(raw), free(px);
    return grey;
}

/* A camera's view of the image: scaled to fit 90% of the frame and centred on white */
static uint8_t *to_frame(const uint8_t *img, int w, int h) {
    uint8_t *f = malloc(CAM_W * CAM_H);
    double s = (w * CAM_H > h * CAM_W ? (double)CAM_W / w : (double)CAM_H / h) * 0.9;
    int fw = (int)(w * s), fh = (int)(h * s), ox = (CAM_W - fw) / 2, oy = (CAM_H - fh) / 2;
    memset(f, 255, CAM_W * CAM_H);
    for (int y = 0; y < fh; y++)
        for (int x = 0; x < fw; x++) {
            int x0 = (int)(x / s), x1 = (int)((x + 1) / s), y0 = (int)(y / s), y1 = (int)((y + 1) / s), sum = 0, n = 0;
            if (x1 <= x0) x1 = x0 + 1; /* enlarging: nearest pixel */
            if (y1 <= y0) y1 = y0 + 1;
            for (int yy = y0; yy < y1 && yy < h; yy++)
                for (int xx = x0; xx < x1 && xx < w; xx++) sum += img[yy * w + xx], n++;
            f[(oy + y) * CAM_W + ox + x] = (uint8_t)(n ? sum / n : 255);
        }
    return f;
}

static void write_png(const char *path) {
    uLongf len = compressBound(SCREEN * (SCREEN * 3 + 1));
    uint8_t *raw = malloc(SCREEN * (SCREEN * 3 + 1)), *z = malloc(len), hdr[13];
    FILE *f = fopen(path, "wb");
    for (int y = 0; y < SCREEN; y++) {
        uint8_t *r = raw + y * (SCREEN * 3 + 1);
        r[0] = 0;
        for (int x = 0; x < SCREEN; x++) {
            uint16_t c = screen[y * SCREEN + x];
            r[1 + x * 3] = (uint8_t)((c >> 11) * 255 / 31);
            r[2 + x * 3] = (uint8_t)((c >> 5 & 63) * 255 / 63);
            r[3 + x * 3] = (uint8_t)((c & 31) * 255 / 31);
        }
    }
    compress2(z, &len, raw, SCREEN * (SCREEN * 3 + 1), 9);
    uint8_t ihdr[] = {0, 0, 0, SCREEN, 0, 0, 0, SCREEN, 8, 2, 0, 0, 0};
    memcpy(hdr, ihdr, 13);
    const struct {
        const char *tag;
        const uint8_t *d;
        uint32_t n;
    } chunks[] = {{"IHDR", hdr, 13}, {"IDAT", z, (uint32_t)len}, {"IEND", NULL, 0}};
    fwrite("\x89PNG\r\n\x1a\n", 1, 8, f);
    for (int i = 0; i < 3; i++) {
        uint8_t be[4] = {chunks[i].n >> 24, chunks[i].n >> 16 & 255, chunks[i].n >> 8 & 255, chunks[i].n & 255};
        uLong crc = crc32(crc32(0, (const Bytef *)chunks[i].tag, 4), chunks[i].d, chunks[i].n);
        uint8_t cb[4] = {crc >> 24, crc >> 16 & 255, crc >> 8 & 255, crc & 255};
        fwrite(be, 1, 4, f), fwrite(chunks[i].tag, 1, 4, f), fwrite(chunks[i].d, 1, chunks[i].n, f),
            fwrite(cb, 1, 4, f);
    }
    fclose(f), free(raw), free(z);
}

/* Every address the app passes is checked against its memory before the host touches it */
static void *app_ptr(wasm_exec_env_t env, uint32_t off, uint32_t len) {
    wasm_module_inst_t inst = wasm_runtime_get_module_inst(env);
    if (!wasm_runtime_validate_app_addr(inst, off, len)) {
        wasm_runtime_set_exception(inst, "qrboot: address out of bounds");
        return NULL;
    }
    return wasm_runtime_addr_app_to_native(inst, off);
}

static void qb_draw_line(wasm_exec_env_t env, int32_t y, uint32_t p) {
    const uint16_t *line = app_ptr(env, p, SCREEN * 2);
    if (line && y >= 0 && y < SCREEN) memcpy(screen + y * SCREEN, line, SCREEN * 2);
}

static int32_t key_code(const char *name) {
    static const char *const names[] = {"UP", "DOWN", "LEFT", "RIGHT", "SELECT", "A", "B", "C"};
    for (int i = 0; i < 8; i++)
        if (!strcmp(name, names[i])) return i;
    return fail("unknown key", name), -1;
}

static int32_t qb_key(wasm_exec_env_t env) {
    char cmd[16], name[16];
    int count;
    clock_us += 1000;
    if (++polls_since_capture < 3) return -1; /* still scanning */
    if (idle_left > 0 && idle_left--) return -1;
    while (!key_left && script_at < script_len) {
        const char *line = script[script_at++];
        if (sscanf(line, "%15s", cmd) != 1 || cmd[0] == '#') continue;
        if (!strcmp(cmd, "frames")) {
            char path[1024];
            const char *p = line + 6;
            int n, w, h;
            while (sscanf(p, " %1023s%n", path, &n) == 1) {
                uint8_t *img = read_png(path, &w, &h);
                if (n_frames == MAX_FRAMES) fail("too many frames", NULL);
                frames[n_frames++] = to_frame(img, w, h), free(img);
                p += n;
            }
            polls_since_capture = 0;
            return -1;
        }
        if (!strcmp(cmd, "idle") && sscanf(line, "%15s %d", cmd, &count) == 2) {
            idle_left = count;
            break;
        }
        count = 1;
        if (sscanf(line, "%15s %15s %d", cmd, name, &count) < 2 || strcmp(cmd, "key")) fail("bad script line:", line);
        key_left = count, cur_key = key_code(name);
    }
    if (idle_left > 0) return -1;
    if (key_left) {
        key_left--;
        next_frame = n_frames; /* a key ends whatever scan was running */
        return cur_key;
    }
    if (++idle > IDLE_POLLS) wasm_runtime_set_exception(wasm_runtime_get_module_inst(env), "qrboot: script done");
    return -1;
}

static uint64_t qb_time_us(wasm_exec_env_t env) {
    (void)env;
    return clock_us;
}

static void qb_sleep_ms(wasm_exec_env_t env, uint32_t ms) {
    (void)env;
    clock_us += (uint64_t)ms * 1000;
}

static int32_t qb_capture(wasm_exec_env_t env, uint32_t p, int32_t w, int32_t h) {
    uint8_t *dst;
    if (w != CAM_W || h != CAM_H || !(dst = app_ptr(env, p, CAM_W * CAM_H))) return 0;
    if (next_frame >= n_frames) return 0; /* out of frames: let the script's next key through */
    polls_since_capture = 0;
    memcpy(dst, frames[next_frame++], CAM_W * CAM_H);
    return 1;
}

static int32_t qb_qr_decode(wasm_exec_env_t env, uint32_t px, int32_t w, int32_t h, int32_t kind, uint32_t out,
                            int32_t cap) {
    const uint8_t *frame = app_ptr(env, px, (uint32_t)(w * h));
    uint8_t *dst = app_ptr(env, out, (uint32_t)cap);
    int32_t n;
    if (!frame || !dst || w <= 0 || h <= 0) return -1;
    n = qr_reader_decode(qr_state, frame, (size_t)w, (size_t)h, kind == 1, kind == 2, qr_out, 1000);
    if (n > cap) return -1;
    if (n > 0) memcpy(dst, qr_out, (size_t)n);
    return n;
}

static void qb_random(wasm_exec_env_t env, uint32_t p, int32_t len) {
    uint8_t *dst = app_ptr(env, p, (uint32_t)len);
    if (dst) arc4random_buf(dst, (size_t)len);
}

static void qb_log(wasm_exec_env_t env, uint32_t p, int32_t len) {
    const char *s = app_ptr(env, p, (uint32_t)len);
    if (s) fwrite(s, 1, (size_t)len, stdout);
}

/* The app from its BBQr codes (tools/qrboot/codes.py pack), in any order. It is refused unless its SHA-256
 * is the one printed with the codes; on the device that is a screen and a key instead of an argument */
static uint8_t *boot_from_codes(const char *expected, char **paths, int n, size_t *len) {
    static const char b32[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
    char **body = NULL, hex[65];
    int total = 0, have = 0;
    for (int i = 0; i < n; i++) {
        int w, h, got, idx, t;
        uint8_t *img = read_png(paths[i], &w, &h);
        if ((got = qr_reader_decode(qr_state, img, (size_t)w, (size_t)h, 0, 0, qr_out, 1000)) <= 8)
            fail("no code in", paths[i]);
        free(img);
        if (memcmp(qr_out, "B$ZB", 4)) fail("not a qrboot code:", paths[i]);
        t = (int)(strchr(B36, qr_out[4]) - B36) * 36 + (int)(strchr(B36, qr_out[5]) - B36);
        idx = (int)(strchr(B36, qr_out[6]) - B36) * 36 + (int)(strchr(B36, qr_out[7]) - B36);
        if (!body) total = t, body = calloc((size_t)t, sizeof(char *));
        if (t != total || idx >= total) fail("a code from another set:", paths[i]);
        if (!body[idx]) body[idx] = strndup((const char *)qr_out + 8, (size_t)got - 8), have++;
    }
    if (!body || have < total) fail("codes missing", NULL);
    size_t chars = 0, o = 0;
    for (int i = 0; i < total; i++) chars += strlen(body[i]);
    uint8_t *packed = malloc(chars * 5 / 8 + 1);
    uint32_t acc = 0;
    int bits = 0;
    for (int i = 0; i < total; i++)
        for (const char *c = body[i]; *c; c++) {
            const char *v = strchr(b32, *c);
            if (!v) fail("bad Base32 in code", NULL);
            acc = acc << 5 | (uint32_t)(v - b32);
            if ((bits += 5) >= 8) packed[o++] = (uint8_t)(acc >> (bits -= 8));
        }
    /* BBQr's zlib: raw deflate, 1 KiB window */
    size_t cap = 1 << 20;
    uint8_t *wasm = malloc(cap);
    z_stream z = {.next_in = packed, .avail_in = (uInt)o, .next_out = wasm, .avail_out = (uInt)cap};
    if (inflateInit2(&z, -10) != Z_OK || inflate(&z, Z_FINISH) != Z_STREAM_END) fail("bad compressed data", NULL);
    *len = z.total_out;
    inflateEnd(&z);
    uint8_t d[32];
    CC_SHA256(wasm, (CC_LONG)*len, d);
    for (int i = 0; i < 32; i++) snprintf(hex + i * 2, 3, "%02x", d[i]);
    printf("loader: %d codes, %zu bytes, sha256", total, *len);
    for (int i = 0; i < 64; i += 4) printf(" %.4s", hex + i);
    printf("\n");
    char want[65] = {0};
    for (int i = 0, j = 0; expected[i] && j < 64; i++)
        if (expected[i] != ' ') want[j++] = (char)(expected[i] | 0x20);
    if (strcmp(want, hex)) fail("not the SHA-256 printed with the codes; refusing to run it", NULL);
    return wasm;
}

/* wasi-libc's vsnprintf links its stdio, which names these; nothing in the app calls them */
static int32_t wasi_stub(wasm_exec_env_t env, int32_t a, int64_t b, int32_t c, int32_t d) {
    (void)env, (void)a, (void)b, (void)c, (void)d;
    return 52; /* ENOSYS */
}

int main(int argc, char **argv) {
    static NativeSymbol qrboot[] = {
        {"draw_line", qb_draw_line, "(ii)", NULL}, {"key", qb_key, "()i", NULL},
        {"time_us", qb_time_us, "()I", NULL},      {"sleep_ms", qb_sleep_ms, "(i)", NULL},
        {"capture", qb_capture, "(iii)i", NULL},   {"qr_decode", qb_qr_decode, "(iiiiii)i", NULL},
        {"random", qb_random, "(ii)", NULL},       {"log", qb_log, "(ii)", NULL},
    };
    static NativeSymbol wasi[] = {{"fd_write", wasi_stub, "(iiii)i", NULL}, {"fd_seek", wasi_stub, "(iIii)i", NULL}};
    char err[256], line[65536];
    RuntimeInitArgs init;
    FILE *f;
    long size;
    uint8_t *wasm;

    setvbuf(stdout, NULL, _IOLBF, 0);
    if (argc < 4 || (!strcmp(argv[3], "--qr") && argc < 6))
        fail("usage: host script.txt shot.png (app.wasm | --qr sha256 qr-*.png)", NULL);
    if (!(f = fopen(argv[1], "r"))) fail("cannot open", argv[1]);
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\n")] = 0;
        script = realloc(script, sizeof(char *) * (size_t)(script_len + 1));
        script[script_len++] = strdup(line);
    }
    fclose(f);
    qr_out = malloc(qr_reader_output_cap());
    qr_state = aligned_alloc(16, (qr_reader_state_size() + 15) & ~(size_t)15);
    qr_reader_state_init(qr_state);
    if (!strcmp(argv[3], "--qr")) {
        size_t n;
        wasm = boot_from_codes(argv[4], argv + 5, argc - 5, &n);
        size = (long)n;
    } else {
        if (!(f = fopen(argv[3], "rb"))) fail("cannot open", argv[3]);
        fseek(f, 0, SEEK_END), size = ftell(f), fseek(f, 0, SEEK_SET);
        wasm = malloc((size_t)size);
        if (fread(wasm, 1, (size_t)size, f) != (size_t)size) fail("cannot read", argv[3]);
        fclose(f);
    }

    memset(&init, 0, sizeof(init));
    init.mem_alloc_type = Alloc_With_System_Allocator;
    if (!wasm_runtime_full_init(&init) ||
        !wasm_runtime_register_natives("qrboot", qrboot, sizeof(qrboot) / sizeof(qrboot[0])) ||
        !wasm_runtime_register_natives("wasi_snapshot_preview1", wasi, 2))
        fail("WAMR init failed", NULL);
    wasm_module_t mod = wasm_runtime_load(wasm, (uint32_t)size, err, sizeof(err));
    if (!mod) fail("load:", err);
    wasm_module_inst_t inst = wasm_runtime_instantiate(mod, 65536, 0, err, sizeof(err));
    if (!inst) fail("instantiate:", err);
    wasm_exec_env_t env = wasm_runtime_create_exec_env(inst, 65536);
    wasm_function_inst_t fn = wasm_runtime_lookup_function(inst, "main");
    uint32_t args[2] = {0, 0};
    if (!fn) fail("no main export", NULL);
    int ok = wasm_runtime_call_wasm(env, fn, 0, args);
    const char *ex = wasm_runtime_get_exception(inst);
    write_png(argv[2]);
    if (!ok && ex && strstr(ex, "qrboot: script done")) return 0;
    fprintf(stderr, "host: app stopped: %s\n", ex ? ex : "returned");
    return ok ? 0 : 1;
}

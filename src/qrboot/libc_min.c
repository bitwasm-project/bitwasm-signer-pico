/* The two pieces of libc the app uses, small enough for printed codes. wasi-libc's vfprintf (8 KB, and the
 * WASI fd_write/fd_seek imports it names) and dlmalloc (7 KB) are left out because these are defined here */
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The conversions the app formats: d i u x X s c %, flags - and 0, a width, a precision (.N or .*), l and ll */
int vsnprintf(char *buf, size_t cap, const char *f, va_list ap) {
    size_t n = 0;
#define PUT(c) (n + 1 < cap ? (void)(buf[n] = (char)(c)) : (void)0, n++)
    for (; *f; f++) {
        if (*f != '%') {
            PUT(*f);
            continue;
        }
        int left = 0, zero = 0, lng = 0, neg = 0;
        size_t width = 0, prec = (size_t)-1, len;
        for (f++; *f == '-' || *f == '0'; f++) *f == '-' ? (left = 1) : (zero = 1);
        if (*f == '*') width = (size_t)va_arg(ap, int), f++;
        for (; *f >= '0' && *f <= '9'; f++) width = width * 10 + (size_t)(*f - '0');
        if (*f == '.') {
            prec = 0;
            if (*++f == '*') prec = (size_t)va_arg(ap, int), f++;
            for (; *f >= '0' && *f <= '9'; f++) prec = prec * 10 + (size_t)(*f - '0');
        }
        for (; *f == 'l'; f++) lng++;
        char tmp[24], *s = tmp + sizeof(tmp);
        if (*f == 's') {
            s = va_arg(ap, char *);
            s = s ? s : "(null)";
            len = strnlen(s, prec);
        } else if (*f == 'c' || *f == '%') {
            *--s = *f == 'c' ? (char)va_arg(ap, int) : '%';
            len = 1;
        } else if (*f && strchr("diuxX", *f)) {
            unsigned long long v;
            if (*f == 'd' || *f == 'i') {
                long long x = lng > 1 ? va_arg(ap, long long) : lng ? va_arg(ap, long) : va_arg(ap, int);
                neg = x < 0;
                v = neg ? 0 - (unsigned long long)x : (unsigned long long)x;
            } else {
                v = lng > 1 ? va_arg(ap, unsigned long long) : lng ? va_arg(ap, unsigned long) : va_arg(ap, unsigned);
            }
            unsigned base = *f == 'x' || *f == 'X' ? 16 : 10;
            const char *digits = *f == 'X' ? "0123456789ABCDEF" : "0123456789abcdef";
            do *--s = digits[v % base];
            while (v /= base);
            if (neg && !zero) *--s = '-', neg = 0;
            len = (size_t)(tmp + sizeof(tmp) - s);
        } else {
            PUT('%');
            if (!*f) break;
            PUT(*f);
            continue;
        }
        if (neg) PUT('-'), width = width ? width - 1 : 0;
        for (size_t i = len; !left && i < width; i++) PUT(zero ? '0' : ' ');
        for (size_t i = 0; i < len; i++) PUT(s[i]);
        for (size_t i = len; left && i < width; i++) PUT(' ');
    }
    if (cap) buf[n < cap ? n : cap - 1] = 0;
    return (int)n;
#undef PUT
}

int snprintf(char *buf, size_t cap, const char *f, ...) {
    va_list ap;
    va_start(ap, f);
    int n = vsnprintf(buf, cap, f, ap);
    va_end(ap);
    return n;
}

/* The app takes and returns a few fixed-size buffers, so a freed block is kept for the next request it fits,
 * with nothing split or merged; new ones come off the top of the heap, which grows the memory */
typedef struct block {
    size_t size;
    struct block *next;
    uint8_t pad[8];
} block;
extern unsigned char __heap_base;
static uintptr_t heap_top = (uintptr_t)&__heap_base;
static block *freed;

void *malloc(size_t n) {
    n = (n + 15) & ~(size_t)15;
    for (block **p = &freed; *p; p = &(*p)->next)
        if ((*p)->size >= n) {
            block *b = *p;
            *p = b->next;
            return b + 1;
        }
    uintptr_t at = (heap_top + 15) & ~(uintptr_t)15, end = at + sizeof(block) + n;
    size_t have = __builtin_wasm_memory_size(0) * 65536;
    if (end < at || (end > have && __builtin_wasm_memory_grow(0, (end - have + 65535) / 65536) == (size_t)-1))
        return NULL;
    heap_top = end;
    block *b = (block *)at;
    b->size = n;
    return b + 1;
}

void free(void *p) {
    if (!p) return;
    block *b = (block *)p - 1;
    b->next = freed;
    freed = b;
}

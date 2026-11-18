/* parser_host.h with the parser linked into the app: the app is one module, so there is no second
 * instance to cross into. Lengths are still checked as runtime/parser_host.c checks them */
#include "parser_host.h"
#include <string.h>
#include "psbt_parser.h"

int parser_host_init(const uint8_t *wasm, uint32_t wasm_len, void *pool, uint32_t pool_size) {
    (void)wasm, (void)wasm_len, (void)pool, (void)pool_size;
    return 1;
}

int parser_host_parse(const uint8_t *psbt, uint32_t len, uint32_t fingerprint, uint32_t *rc, plan_t *plan,
                      core_prevtx_t prev[PLAN_MAX_INPUTS], uint8_t *arena, size_t arena_cap) {
    size_t used = 0;
    memset(prev, 0, sizeof(core_prevtx_t) * PLAN_MAX_INPUTS);
    if (len > PARSER_PSBT_MAX) return 0;
    memmove(parser_input(), psbt, len); /* psbt may already be parser_input() */
    if ((*rc = (uint32_t)parser_parse(len, fingerprint)) != 0) return 1;
    memcpy(plan, parser_plan(), sizeof(*plan));
    for (unsigned int i = 0; i < plan->n_inputs && i < PLAN_MAX_INPUTS; i++) {
        unsigned int off = parser_prevtx_off(i), n = parser_prevtx_len(i);
        if (!n) continue;
        if (off > PARSER_PSBT_MAX || n > PARSER_PSBT_MAX - off) return 0;
        /* Without an arena the previous transactions are read where they lie, until the next input */
        if (!arena) {
            prev[i].raw = parser_input() + off, prev[i].len = n;
            continue;
        }
        if (n > arena_cap - used) return 0;
        memcpy(arena + used, parser_input() + off, n);
        prev[i].raw = arena + used, prev[i].len = n;
        used += n;
    }
    return 1;
}

int parser_host_nonces(uint8_t nonces[PLAN_MAX_INPUTS][33]) {
    memcpy(nonces, parser_nonces(), PLAN_MAX_INPUTS * 33);
    return 1;
}

int parser_host_set_points(const uint8_t points[PLAN_MAX_INPUTS][33]) {
    memcpy(parser_nonce_points(), points, PLAN_MAX_INPUTS * 33);
    return 1;
}

int parser_host_finalize(const core_sig_t *sigs, unsigned int n, uint8_t *out, size_t cap, uint32_t *out_len) {
    int len;
    if (n > PLAN_MAX_INPUTS) return 0;
    memcpy(parser_sigs(), sigs, sizeof(core_sig_t) * n);
    if ((len = parser_finalize(n)) < 0 || (size_t)len > cap) return 0;
    memmove(out, parser_output(), (size_t)len);
    *out_len = (uint32_t)len;
    return 1;
}

int parser_host_ur_reset(void) {
    parser_ur_reset();
    return 1;
}

int parser_host_ur_receive(const char *part, uint32_t len, int32_t *rc, uint8_t *psbt, size_t cap) {
    if (len > PARSER_PSBT_MAX) return 0;
    memcpy(parser_input(), part, len);
    if ((*rc = parser_ur_receive(len)) <= 0) return 1;
    if ((size_t)*rc > cap) return 0;
    memmove(psbt, parser_input(), (size_t)*rc);
    return 1;
}

int32_t parser_host_ur_encode_start(uint32_t len, uint32_t max_fragment_len) {
    return parser_ur_encode_start(len, max_fragment_len);
}

int32_t parser_host_ur_encode_bytes(const uint8_t *data, uint32_t len, uint32_t max_fragment_len) {
    if (len > PSBT_OUT_MAX) return -1;
    memcpy(parser_output(), data, len);
    return parser_ur_encode_start(len, max_fragment_len);
}

int parser_host_ur_encode_next(char *text, size_t cap) {
    int n = parser_ur_encode_next();
    if (n <= 0 || (size_t)n >= cap) return 0;
    memcpy(text, parser_input(), (size_t)n);
    text[n] = 0;
    return 1;
}

uint32_t parser_host_pool_highmark(void) {
    return 0;
}

#include <stdio.h>
#include <string.h>
#include "ui.h"

static int failures, checks;
#define CHECK(cond, msg)                                                                                               \
    do {                                                                                                               \
        checks++;                                                                                                      \
        if (!(cond)) {                                                                                                 \
            failures++;                                                                                                \
            printf("FAIL %s:%d %s\n", __FILE__, __LINE__, msg);                                                        \
        }                                                                                                              \
    } while (0)

static void display3(core_display_t *d) {
    memset(d, 0, sizeof(*d));
    d->n_outputs = 3, d->fee = 1000, d->spend = 100000;
    strcpy(d->outputs[0].text, "bc1q5knnnmmfe7xqdr55g43jyec388dtsah27rxamf");
    d->outputs[1].owner = CORE_OUT_CHANGE;
    strcpy(d->outputs[1].text, "bc1q8c6fshw2dlwun7ekn9qwf37cu2rn755upcp6el");
    d->outputs[2].text_kind = CORE_TEXT_SCRIPT;
    memset(d->outputs[2].text, 'a', 2 * PLAN_MAX_SPK); /* the longest script in hex, 166 characters */
}

int main(void) {
    static ui_review_t r;
    core_display_t d;

    display3(&d);
    ui_review_init(&r, &d);
    CHECK(r.n == 5, "summary + 3 outputs + final review");
    CHECK(ui_review_key(&r, UI_KEY_SELECT) == UI_PENDING && r.cur == 1, "next page is selected initially");
    for (int i = 1; i < 4; i++) ui_review_key(&r, UI_KEY_SELECT);
    CHECK(r.cur == 4, "reaches final review after all outputs");
    r.screens[4].selection = 2;
    CHECK(ui_review_key(&r, UI_KEY_SELECT) == UI_APPROVED, "sign after seeing all");

    /* Cancel is the default choice on the final review screen. */
    ui_review_init(&r, &d);
    for (int i = 0; i < 4; i++) ui_review_key(&r, UI_KEY_SELECT);
    CHECK(r.cur == 4 && ui_review_key(&r, UI_KEY_SELECT) == UI_REJECTED, "cancel returns to menu");

    /* Review Again returns to the summary and requires reviewing every output again. */
    ui_review_init(&r, &d);
    for (int i = 0; i < 4; i++) ui_review_key(&r, UI_KEY_SELECT);
    ui_review_key(&r, UI_KEY_UP);
    CHECK(ui_review_key(&r, UI_KEY_SELECT) == UI_PENDING && r.cur == 0 && r.seen == 1,
          "review again returns to summary");

    /* Skipping a screen cannot reach the confirmation anyway, but a gap in seen is checked directly too */
    ui_review_init(&r, &d);
    r.cur = 4;
    r.seen |= 1u << 4;
    r.screens[4].selection = 2;
    CHECK(ui_review_key(&r, UI_KEY_SELECT) == UI_PENDING, "unseen outputs block approval");
    CHECK(ui_review_key(&r, UI_KEY_B) == UI_REJECTED, "cancel anywhere");

    /* Even the longest script is shown in full, not truncated: 30 characters over 6 rows */
    ui_review_init(&r, &d);
    {
        size_t shown = 0;
        /* Row 1 is the application name and row 2 the heading, so the hex starts at row 7 */
        for (int row = 6; row < UI_ROWS - 1; row++) shown += strlen(r.screens[3].text[row]);
        CHECK(shown == 2 * PLAN_MAX_SPK, "longest script fully shown");
    }
    {
        size_t shown = 0;
        d.outputs[2].text_kind = CORE_TEXT_OP_RETURN, d.outputs[2].amount = 1;
        memset(d.outputs[2].text, 0, sizeof(d.outputs[2].text));
        memset(d.outputs[2].text, 'b', 2 * (PLAN_MAX_SPK - 1));
        ui_review_init(&r, &d);
        for (int row = 6; row < UI_ROWS - 1; row++) shown += strlen(r.screens[3].text[row]);
        CHECK(!strcmp(r.screens[3].text[4], "Burned: never spendable"), "OP_RETURN with an amount says it is burned");
        CHECK(shown == 2 * (PLAN_MAX_SPK - 1), "longest OP_RETURN data fully shown");
        CHECK(!r.screens[1].text[4][0], "an address output says nothing of burning");
        d.outputs[2].amount = 0;
        ui_review_init(&r, &d);
        CHECK(!r.screens[3].text[4][0], "OP_RETURN without an amount says nothing of burning");
    }
    /* The passphrase keyboard: 95 characters, then DEL and OK, 13 to a row */
    {
        ui_pass_t p;
        int i;
        ui_pass_init(&p);
        CHECK(ui_pass_key(&p, UI_KEY_SELECT) == UI_PENDING && !strcmp(p.text, "a"), "types the key under the cursor");
        ui_pass_key(&p, UI_KEY_DOWN), ui_pass_key(&p, UI_KEY_DOWN), ui_pass_key(&p, UI_KEY_SELECT);
        CHECK(!strcmp(p.text, "aA"), "two rows down is the capitals");
        ui_pass_key(&p, UI_KEY_UP), ui_pass_key(&p, UI_KEY_UP), ui_pass_key(&p, UI_KEY_UP);
        CHECK(p.cur == 12 + 7 * 13 - 13, "up from the top goes to the bottom of the column before");
        ui_pass_init(&p);
        ui_pass_key(&p, UI_KEY_LEFT);
        CHECK(p.cur == 12, "left wraps within the row");
        for (i = 0; i < 7; i++) ui_pass_key(&p, UI_KEY_UP);
        CHECK(p.cur == 11, "seven ups climb the column before back to its top");
        p.cur = 7 * 13 + 3, ui_pass_key(&p, UI_KEY_SELECT);
        CHECK(!strcmp(p.text, " ") && !strcmp(p.screen.text[3], "1/64 [space]"), "space is the last character");
        ui_pass_key(&p, UI_KEY_RIGHT), ui_pass_key(&p, UI_KEY_SELECT);
        CHECK(p.len == 0 && !p.text[0], "DEL removes the last character");
        ui_pass_key(&p, UI_KEY_RIGHT), ui_pass_key(&p, UI_KEY_RIGHT);
        CHECK(p.cur == 7 * 13, "right wraps within the short last row");
        ui_pass_key(&p, UI_KEY_LEFT);
        CHECK(p.cur == 7 * 13 + 5 && ui_pass_key(&p, UI_KEY_SELECT) == UI_APPROVED, "OK ends the entry");
        CHECK(ui_pass_key(&p, UI_KEY_B) == UI_REJECTED, "B cancels");
        ui_pass_init(&p);
        for (i = 0; i < 97 * 2; i++) ui_pass_key(&p, UI_KEY_DOWN);
        CHECK(p.cur == 0, "down alone visits all 97 keys and comes back");
        for (i = 0; i < UI_PASS_MAX + 5; i++) ui_pass_key(&p, UI_KEY_SELECT);
        CHECK(p.len == UI_PASS_MAX && strlen(p.text) == UI_PASS_MAX, "stops at the maximum length");
        CHECK(strlen(p.screen.text[2]) == 19 && !strncmp(p.screen.text[3], "64/64", 5),
              "the end of a long one is shown");
    }
    printf("%d/%d checks passed\n", checks - failures, checks);
    return failures != 0;
}

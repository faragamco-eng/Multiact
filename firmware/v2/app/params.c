#include "params.h"
#include "crc32.h"
#include <stddef.h>
#include <string.h>

#define PMAGIC 0x50524D32u           /* "PRM2" */

typedef struct {
    uint32_t magic, seq;
    params_t p;
    uint32_t crc;                    /* over magic..p */
} rec_t;

#define REC_SIZE ((uint32_t)((sizeof(rec_t) + 7u) & ~7u))
uint32_t params_record_size(void) { return REC_SIZE; }

static bool rec_ok(const rec_t *r)
{
    return r->magic == PMAGIC && r->crc == crc32_update(0, r, (uint32_t)offsetof(rec_t, crc));
}

static bool blank(const uint8_t *p, uint32_t n)
{
    while (n--) if (*p++ != 0xFFu) return false;
    return true;
}

bool params_load(params_store_t *s, const flash_if_t *fl, uint32_t page0, uint32_t page1, params_t *p)
{
    memset(s, 0, sizeof *s);
    s->fl = fl;
    s->page[0] = page0;
    s->page[1] = page1;
    s->cur = -1;
    const rec_t *best = 0;
    uint32_t per_page = fl->page_size / REC_SIZE;
    for (int pg = 0; pg < 2; pg++) {
        for (uint32_t k = 0; k < per_page; k++) {
            uint32_t a = s->page[pg] + k * REC_SIZE;
            const rec_t *r = (const rec_t *)(const void *)fl->ptr(a);
            if (blank((const uint8_t *)r, REC_SIZE)) break;
            if (rec_ok(r) && (!best || r->seq > best->seq)) {
                best = r;
                s->cur = pg;
                s->seq = r->seq;
            }
        }
    }
    if (!best) return false;
    memcpy(p, &best->p, sizeof *p);
    /* next free slot in the current page (after the last written one) */
    s->next = 0;
    for (uint32_t k = 0; k < per_page; k++) {
        uint32_t a = s->page[s->cur] + k * REC_SIZE;
        if (blank(fl->ptr(a), REC_SIZE)) { s->next = a; break; }
    }
    return true;
}

bool params_save(params_store_t *s, const params_t *p)
{
    const flash_if_t *fl = s->fl;
    static rec_t r;                  /* not on the stack (small MCU stacks) */
    memset(&r, 0xFF, sizeof r);
    r.magic = PMAGIC;
    r.seq = s->seq + 1u;
    memcpy(&r.p, p, sizeof *p);
    r.crc = crc32_update(0, &r, (uint32_t)offsetof(rec_t, crc));

    if (s->cur < 0 || s->next == 0 ||
        s->next + REC_SIZE > s->page[s->cur] + fl->page_size) {
        int np = (s->cur < 0) ? 0 : 1 - s->cur;     /* switch page */
        if (!fl->erase(s->page[np])) return false;
        s->cur = np;
        s->next = s->page[np];
    }
    static uint8_t buf[REC_SIZE];
    memset(buf, 0xFF, sizeof buf);
    memcpy(buf, &r, sizeof r);
    if (!fl->program(s->next, buf, REC_SIZE)) return false;
    if (!rec_ok((const rec_t *)(const void *)fl->ptr(s->next))) return false;
    s->next += REC_SIZE;
    s->seq = r.seq;
    return true;
}

/* SHA-1 for the native state reader (T-trace).  Standard FIPS 180-1
 * algorithm, written for this project; no dependency taken.  The trace
 * schema truncates digests, but the full 20 bytes are produced here and
 * the caller takes what it needs.
 */
#include <stdint.h>
#include <string.h>

typedef struct {
    uint32_t h[5];
    uint64_t len;
    unsigned char buf[64];
    unsigned fill;
} sl_sha1_ctx;

static uint32_t rol(uint32_t v, int n) { return (v << n) | (v >> (32 - n)); }

void sl_sha1_init(sl_sha1_ctx *c)
{
    c->h[0] = 0x67452301u; c->h[1] = 0xEFCDAB89u; c->h[2] = 0x98BADCFEu;
    c->h[3] = 0x10325476u; c->h[4] = 0xC3D2E1F0u;
    c->len = 0;
    c->fill = 0;
}

static void sl_sha1_block(sl_sha1_ctx *c, const unsigned char *p)
{
    uint32_t w[80], a, b, d, e, f, k, t, cc;
    int i;

    for (i = 0; i < 16; i++)
        w[i] = ((uint32_t) p[i * 4] << 24) | ((uint32_t) p[i * 4 + 1] << 16)
             | ((uint32_t) p[i * 4 + 2] << 8) | p[i * 4 + 3];
    for (; i < 80; i++)
        w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);

    a = c->h[0]; b = c->h[1]; cc = c->h[2]; d = c->h[3]; e = c->h[4];
    for (i = 0; i < 80; i++) {
        if (i < 20)      { f = (b & cc) | (~b & d);          k = 0x5A827999u; }
        else if (i < 40) { f = b ^ cc ^ d;                   k = 0x6ED9EBA1u; }
        else if (i < 60) { f = (b & cc) | (b & d) | (cc & d); k = 0x8F1BBCDCu; }
        else             { f = b ^ cc ^ d;                   k = 0xCA62C1D6u; }
        t = rol(a, 5) + f + e + k + w[i];
        e = d; d = cc; cc = rol(b, 30); b = a; a = t;
    }
    c->h[0] += a; c->h[1] += b; c->h[2] += cc; c->h[3] += d; c->h[4] += e;
}

void sl_sha1_update(sl_sha1_ctx *c, const void *data, unsigned n)
{
    const unsigned char *p = data;

    c->len += n;
    while (n > 0) {
        unsigned take = 64 - c->fill;
        if (take > n) take = n;
        memcpy(c->buf + c->fill, p, take);
        c->fill += take; p += take; n -= take;
        if (c->fill == 64) {
            sl_sha1_block(c, c->buf);
            c->fill = 0;
        }
    }
}

void sl_sha1_final(sl_sha1_ctx *c, unsigned char out[20])
{
    unsigned char pad = 0x80;
    unsigned char zero = 0;
    unsigned char lenb[8];
    uint64_t bits = c->len * 8;
    int i;

    sl_sha1_update(c, &pad, 1);
    while (c->fill != 56)
        sl_sha1_update(c, &zero, 1);
    for (i = 0; i < 8; i++)
        lenb[i] = (unsigned char) (bits >> (56 - i * 8));
    sl_sha1_update(c, lenb, 8);
    for (i = 0; i < 5; i++) {
        out[i * 4]     = (unsigned char) (c->h[i] >> 24);
        out[i * 4 + 1] = (unsigned char) (c->h[i] >> 16);
        out[i * 4 + 2] = (unsigned char) (c->h[i] >> 8);
        out[i * 4 + 3] = (unsigned char) (c->h[i]);
    }
}

void sl_sha1(const void *data, unsigned n, unsigned char out[20])
{
    sl_sha1_ctx c;
    sl_sha1_init(&c);
    sl_sha1_update(&c, data, n);
    sl_sha1_final(&c, out);
}

/* one streaming context for the props/composite accumulators - the reader
 * uses it strictly begin/update/end, never nested */
static sl_sha1_ctx sl_stream;

void sl_sha1_stream_begin(void) { sl_sha1_init(&sl_stream); }
void sl_sha1_stream_update(const void *data, unsigned n) { sl_sha1_update(&sl_stream, data, n); }
void sl_sha1_stream_end(unsigned char out20[20]) { sl_sha1_final(&sl_stream, out20); }

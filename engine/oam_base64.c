#include "oam_base64.h"

static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

int oam_base64_encode(oam_buf *out, const void *data, size_t len)
{
    const unsigned char *p = data;
    char q[4];
    size_t i;
    for (i = 0; i + 2 < len; i += 3) {
        q[0] = alphabet[p[i] >> 2];
        q[1] = alphabet[(p[i] & 3) << 4 | p[i + 1] >> 4];
        q[2] = alphabet[(p[i + 1] & 15) << 2 | p[i + 2] >> 6];
        q[3] = alphabet[p[i + 2] & 63];
        if (!oam_buf_add(out, q, 4)) return 0;
    }
    if (len - i == 1) {
        q[0] = alphabet[p[i] >> 2];
        q[1] = alphabet[(p[i] & 3) << 4];
        q[2] = q[3] = '=';
        if (!oam_buf_add(out, q, 4)) return 0;
    } else if (len - i == 2) {
        q[0] = alphabet[p[i] >> 2];
        q[1] = alphabet[(p[i] & 3) << 4 | p[i + 1] >> 4];
        q[2] = alphabet[(p[i + 1] & 15) << 2];
        q[3] = '=';
        if (!oam_buf_add(out, q, 4)) return 0;
    }
    return 1;
}

static int value(char c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

int oam_base64_decode(oam_buf *out, const char *text, size_t len)
{
    unsigned long acc = 0;
    int bits = 0;
    size_t i;
    for (i = 0; i < len; i++) {
        char c = text[i];
        int v;
        if (c == '=') break;
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        v = value(c);
        if (v < 0) return 0;
        acc = (acc << 6 | (unsigned long)v) & 0xffffff;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            if (!oam_buf_addc(out, (char)(acc >> bits & 0xff))) return 0;
        }
    }
    return 1;
}

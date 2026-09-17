/**
 * Native-only big-endian accessors (T4).
 *
 * File formats and display lists stay in wire (big-endian) byte order in
 * memory: the decomp parses DLs byte-positionally (bytes[0] == opcode), so
 * wholesale swapping would break every byte-oriented parser.  Where the CPU
 * reads or writes a whole BE word in place, use these instead.
 *
 * Never included by the IDO/matching build.
 */
#ifndef SL_ENDIAN_H
#define SL_ENDIAN_H

#ifdef __sgi
#error "sl_endian.h is native-only; guard the include with #ifndef __sgi"
#endif

static inline unsigned int sl_be32r(const void *p)
{
    const unsigned char *b = p;
    return ((unsigned int) b[0] << 24) | ((unsigned int) b[1] << 16)
         | ((unsigned int) b[2] << 8)  |  (unsigned int) b[3];
}

static inline void sl_be32w(void *p, unsigned int v)
{
    unsigned char *b = p;
    b[0] = (unsigned char) (v >> 24);
    b[1] = (unsigned char) (v >> 16);
    b[2] = (unsigned char) (v >> 8);
    b[3] = (unsigned char) v;
}

static inline unsigned short sl_be16r(const void *p)
{
    const unsigned char *b = p;
    return (unsigned short) ((b[0] << 8) | b[1]);
}

static inline void sl_be16w(void *p, unsigned short v)
{
    unsigned char *b = p;
    b[0] = (unsigned char) (v >> 8);
    b[1] = (unsigned char) v;
}

/* Opcode byte of a display-list command whose words are in native order
 * (value-preserving swapped from the wire).  On N64 this is byte 0; here it
 * is the top byte of w0. */
#define SL_DLOP(p) ((unsigned char) (*(const unsigned int *) (p) >> 24))

#endif /* SL_ENDIAN_H */

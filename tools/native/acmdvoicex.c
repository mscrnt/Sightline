/* Dual-evaluator cross-check for the derived ADPCM semantics.
 *
 * This is an INDEPENDENT CROSS-CHECK OF THE DERIVED MICROCODE SEMANTICS.
 * It is NOT cartridge PCM validation: no decoded sample here is compared
 * against anything the console produced. It can only catch a derivation that
 * is internally inconsistent, never one that is consistently wrong.
 *
 * Two evaluators, sharing only measured INPUTS (command words, the grounded
 * 9-byte frames, the grounded 32-byte codebook entry, the grounded incoming
 * state, the resolved loop address):
 *
 *   A  literal transcription of the vector sequence - 8 lanes, products
 *      accumulated as (a*b)<<16 into a 48-bit accumulator, VSAR HIGH/MID
 *      extracted as separate 16-bit slices, recombined by x0x20 and >>16.
 *      Residual scaling goes through the VMUDM emulation:
 *      ((nib<<12) * (0x8000 >> (11-scale))) >> 16.
 *
 *   B  scalar, derived separately. History is a FLAT sample-indexed array,
 *      not per-half vector elements. Residual scaling is written directly as
 *      signed4 << scale, never through VMUDM. The shift is a plain >>11 on a
 *      32-bit sum, never through VSAR.
 *
 * So B disagrees with A if the VMUDM reasoning, the VSAR recombine, or the
 * per-half history mapping is wrong. They share no residual, no accumulator,
 * no decoded sample, no history and no final state, and each carries its own
 * output from frame N into frame N+1 and its own writeback into the next
 * command that continues the chain.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "../../src/platform/sl_acmd.h"

enum { CTL_NONE=0, CTL_Q10, CTL_Q12, CTL_COEF, CTL_LOOPSRC, CTL_LOOPDST, CTL_HIST, CTL_PVAC };
static int CTL = 0;
static const char *CTLNAME[] = { "none (baseline)", "Q11->Q10", "Q11->Q12",
    "coefficient b2[0] += 1", "A_LOOP reads normal s1", "A_LOOP writes loop addr",
    "history reset each frame",
    "VACUITY: corrupt the state handed to P only" };

static uint32_t be32(const uint8_t*p){return (uint32_t)p[0]<<24|p[1]<<16|p[2]<<8|p[3];}
static int16_t  gs16(const uint8_t*p){return (int16_t)((p[0]<<8)|p[1]);}
static void     ps16(uint8_t*p,int v){ if(v>32767)v=32767; if(v<-32768)v=-32768;
                                       p[0]=(uint8_t)((v>>8)&0xFF); p[1]=(uint8_t)(v&0xFF); }
static int16_t  clamp16(int32_t v){ if(v>32767)return 32767; if(v<-32768)return -32768; return (int16_t)v; }

/* ---------- evaluator A: literal vector transcription ---------------------- */
static void A_frame(const uint8_t *fr, const uint8_t *book,
                    int16_t hist[8], int16_t out[16])
{
    int scale = fr[0] >> 4, i, j, h;
    int32_t r[16];
    int16_t b1[8], b2[8];

    for (j = 0; j < 8; j++) { b1[j] = gs16(book + 2*j); b2[j] = gs16(book + 16 + 2*j); }
    if (CTL == CTL_COEF) b2[0] = (int16_t)(b2[0] + 1);

    for (i = 0; i < 16; i++) {
        int b = fr[1 + (i >> 1)];
        int nib = (i & 1) ? (b & 15) : (b >> 4);
        int32_t v = (int16_t)(uint16_t)(nib << 12);          /* VMUDN path */
        if (12 - scale > 0)
            v = (v * (int32_t)(0x8000 >> (11 - scale))) >> 16;  /* VMUDM path */
        r[i] = v;
    }
    if (CTL == CTL_HIST) memset(hist, 0, 8 * sizeof *hist);

    for (h = 0; h < 2; h++) {
        int16_t res[8];
        int32_t p2 = hist[6], p1 = hist[7];
        for (j = 0; j < 8; j++) {
            int64_t acc = 0; int32_t mid_u, hi_s, w; int64_t acc2;
            acc += ((int64_t)b1[j] * p2) << 16;              /* VMUDH */
            acc += ((int64_t)b2[j] * p1) << 16;              /* VMADH */
            for (i = 0; i < j; i++)
                acc += ((int64_t)b2[j-1-i] * r[8*h+i]) << 16;
            acc += ((int64_t)r[8*h+j] * 0x800) << 16;
            mid_u = (int32_t)((acc >> 16) & 0xFFFF);         /* VSAR MID  */
            hi_s  = (int16_t)((acc >> 32) & 0xFFFF);         /* VSAR HIGH */
            acc2  = (int64_t)((uint32_t)mid_u * 32u);        /* VMUDN x0x20 */
            acc2 += ((int64_t)hi_s * 32) << 16;              /* VMADH x0x20 */
            w = (int32_t)(acc2 >> 16);
            if (CTL == CTL_Q10) w = (int32_t)(acc2 >> 15);
            if (CTL == CTL_Q12) w = (int32_t)(acc2 >> 17);
            res[j] = clamp16(w);
        }
        for (j = 0; j < 8; j++) { out[8*h + j] = res[j]; hist[j] = res[j]; }
    }
}

/* ---------- evaluator B: scalar, flat history, no VMUDM, no VSAR ---------- */
static void B_frame(const uint8_t *fr, const uint8_t *book,
                    int16_t st[2], int16_t out[16])
{
    int scale = fr[0] >> 4, n, i;
    int32_t r[16];
    int16_t b1[8], b2[8], S[18];

    for (i = 0; i < 8; i++) { b1[i] = gs16(book + 2*i); b2[i] = gs16(book + 16 + 2*i); }

    for (i = 0; i < 16; i++) {
        int b = fr[1 + (i >> 1)];
        int s4 = (i & 1) ? (b & 15) : (b >> 4);
        if (s4 > 7) s4 -= 16;                      /* plain sign extension */
        r[i] = (scale >= 12) ? (int32_t)s4 * 4096 : ((int32_t)s4 << scale);
    }

    S[0] = st[0]; S[1] = st[1];                    /* the two samples before */
    for (n = 0; n < 16; n++) {
        int h = n >> 3, j = n & 7;
        int32_t sum;
        int16_t p2 = (h == 0) ? S[0] : S[2 + 6];
        int16_t p1 = (h == 0) ? S[1] : S[2 + 7];
        sum  = (int32_t)b1[j] * p2;
        sum += (int32_t)b2[j] * p1;
        for (i = 0; i < j; i++) sum += (int32_t)b2[j-1-i] * r[8*h + i];
        sum += r[8*h + j] << 11;
        out[n] = clamp16(sum >> 11);
        S[2 + n] = out[n];
    }
    st[0] = out[14]; st[1] = out[15];
}

/* ---------- evaluator P: the SHIPPED interpreter --------------------------
 * A and B are both harness code. Agreeing with each other says nothing about
 * what src/platform/sl_acmd.c actually does, so the production decoder is run
 * as a third evaluator on the same grounded inputs. Requires -DSL_ACMD_ADPCM=1
 * while the production path is still gated off. */
static sl_acmd_state PS;
static int P_command(const uint8_t *dmemsrc, uint32_t w0, uint32_t w1,
                     int si, int so, int sc, uint32_t loopa,
                     const uint8_t *incoming, uint8_t *dram, uint32_t lo,
                     uint32_t dsz, int16_t *outbuf, int F)
{
    uint32_t st = w1 & 0xFFFFFF, cmd[2]; int q, rc;
    uint32_t src = ((w0 >> 16) & 2) ? loopa : st;
    sl_acmd_init(&PS, dram, lo, dsz);
    memcpy(PS.dmem, dmemsrc, 0x1000);
    sl_acmd_sset(&PS, 0x00, (uint32_t)si);
    sl_acmd_sset(&PS, 0x02, (uint32_t)so);
    sl_acmd_sset(&PS, 0x04, (uint32_t)sc);
    sl_acmd_sset32(&PS, 0x10, loopa);
    if (!((w0 >> 16) & 1) && src >= lo && src + 32 <= lo + dsz)
        memcpy(dram + (src - lo), incoming, 32);
    cmd[0] = w0; cmd[1] = w1;
    rc = sl_acmd_exec(&PS, cmd, 1);
    if (rc != SL_ACMD_OK) return -1;
    for (q = 0; q < 16 * F; q++)
        outbuf[q] = (int16_t)((PS.dmem[so + 32 + 2*q] << 8) | PS.dmem[so + 32 + 2*q + 1]);
    return 0;
}

/* ---------- capture walk with the grounded model -------------------------- */
#define BASE 0x5c0
#define CB   0x4C0
static uint8_t dmv[0x1000], dmg[0x1000];
static void taint(int a,int n){int i;if(a<0)return;for(i=0;i<n;i++)if(a+i<0x1000)dmg[a+i]=0;}

int main(int argc, char **argv)
{
    const char *path = argc > 1 ? argv[1] : "/tmp/w/all.bin";
    FILE *f; uint8_t h[20]; uint32_t nf, lo, dsz, fi; uint32_t *w;
    uint8_t *bef, *drv, *drg, *drd, *stA, *stB, *hasA, *hasB;
    long long cN=0, cFull=0, fExec=0, fFull=0;
    long long samp=0, smis=0, statemis=0, cmdmis=0, small_ok=0;
    long long pmis=0, pfail=0; static int16_t pbuf[16*4096], bbuf[16*4096];
    CTL = argc > 2 ? atoi(argv[2]) : 0;

    f = fopen(path, "rb"); if (!f) { perror(path); return 2; }
    fread(h,1,20,f); nf=be32(h+8); lo=be32(h+12); dsz=be32(h+16);
    w=malloc(4096*8); bef=malloc(dsz); drv=malloc(dsz); drg=malloc(dsz); drd=malloc(dsz);
    stA=malloc(dsz); stB=malloc(dsz); hasA=malloc(dsz); hasB=malloc(dsz);

    for (fi = 0; fi < nf; fi++) {
        uint8_t fh[12]; uint32_t n,k; int si=-1,so=-1,sc=-1; uint32_t loopa=0; int haveloop=0;
        if (fread(fh,1,12,f)!=12) break; n=be32(fh+8); if (n>4096) break;
        for (k=0;k<n;k++){uint8_t c[8];fread(c,1,8,f);w[2*k]=be32(c);w[2*k+1]=be32(c+4);}
        fread(bef,1,dsz,f); fseek(f,dsz,SEEK_CUR);
        memcpy(drv,bef,dsz); memset(drg,1,dsz); memset(drd,0,dsz);
        memset(hasA,0,dsz); memset(hasB,0,dsz);
        memset(dmv,0,sizeof dmv); memset(dmg,0,sizeof dmg);

        for (k = 0; k < n; k++) {
            uint32_t w0=w[2*k], w1=w[2*k+1]; int op=(int)((w0>>24)&0xFF); uint32_t j;
            if (op==8){ if(!((w0>>16)&8)){ si=(int)(w0&0xFFFF)+BASE;
                        so=(int)((w1>>16)&0xFFFF)+BASE; sc=(int)(w1&0xFFFF); } }
            else if (op==2){ int d=(int)(w0&0xFFFF)+BASE,c2=(int)(w1&0xFFFF);
                 if(c2){int nn=((c2+15)/16)*16; if(d>=0&&d+nn<=0x1000){memset(dmv+d,0,nn);memset(dmg+d,1,nn);} } }
            else if (op==4){ if(si>=0&&w1>=lo&&w1+sc<=lo+dsz&&si+sc<=0x1000){
                 memcpy(dmv+si,drv+(w1-lo),sc); memcpy(dmg+si,drg+(w1-lo),sc);} }
            else if (op==6){ if(so>=0&&w1>=lo&&w1+sc<=lo+dsz&&so+sc<=0x1000){
                 memcpy(drv+(w1-lo),dmv+so,sc); memcpy(drg+(w1-lo),dmg+so,sc);} }
            else if (op==11){ int c2=(int)(w0&0xFFFF);
                 if(w1>=lo&&(uint32_t)(w1+c2)<=lo+dsz&&CB+c2<=0x1000){
                 memcpy(dmv+CB,drv+(w1-lo),c2); memcpy(dmg+CB,drg+(w1-lo),c2);} }
            else if (op==10){ int i=(int)(w0&0xFFFF)+BASE,o=(int)((w1>>16)&0xFFFF)+BASE,c2=(int)(w1&0xFFFF);
                 if(i>=0&&o>=0&&i+c2<=0x1000&&o+c2<=0x1000){memmove(dmv+o,dmv+i,c2);memmove(dmg+o,dmg+i,c2);} }
            else if (op==13){ int L=(int)((w1>>16)&0xFFFF)+BASE,R=(int)(w1&0xFFFF)+BASE;
                 if(so>=0&&L>=0&&R>=0&&L+sc<=0x1000&&R+sc<=0x1000&&so+2*sc<=0x1000)
                   for(j=0;(int)j<(uint32_t)sc;j+=2){
                     dmv[so+2*j]=dmv[L+j];dmv[so+2*j+1]=dmv[L+j+1];dmg[so+2*j]=dmg[L+j];dmg[so+2*j+1]=dmg[L+j+1];
                     dmv[so+2*j+2]=dmv[R+j];dmv[so+2*j+3]=dmv[R+j+1];dmg[so+2*j+2]=dmg[R+j];dmg[so+2*j+3]=dmg[R+j+1];} }
            else if (op==12){ taint((int)(w1&0xFFFF)+BASE, sc>0?sc:0); }
            else if (op==14||op==5){ taint(so,sc>0?sc:0); }
            else if (op==15){ loopa=w1&0xFFFFFF; haveloop=1; }
            else if (op==3){ memset(dmg,0,sizeof dmg);
                 { uint32_t a=w1&0xFFFFFF; if(a>=lo&&a+80<=lo+dsz){
                     memset(drg+(a-lo),0,80); memset(drd+(a-lo),0,80);} } }
            else if (op==1 && si>=0) {
                uint32_t flags=(w0>>16)&0xFF, st=w1&0xFFFFFF, ssrc=0;
                int F=(sc<=0)?0:(sc+31)/32, ff, q, ok=1, sg=0;
                uint8_t inA[32], inB[32], curA[32], curB[32];
                int16_t histA[8], stB2[2];

                cN++; fExec += F;
                /* every frame's nine bytes and its codebook entry */
                for (ff=0; ff<F; ff++) {
                    int ia=si+9*ff, pred;
                    if (ia<0||ia+9>0x1000) { ok=0; break; }
                    for (q=0;q<9;q++) if(!dmg[ia+q]) ok=0;
                    if(!ok) break;
                    pred=dmv[ia]&15;
                    for (q=0;q<32;q++) if(CB+pred*32+q>=0x1000||!dmg[CB+pred*32+q]) ok=0;
                    if(!ok) break;
                }
                /* incoming state, chronological, per evaluator */
                if (flags & 1) { sg=1; memset(inA,0,32); memset(inB,0,32); }
                else {
                    uint32_t sA, sB;
                    ssrc = (flags & 2) ? loopa : st;
                    if ((flags & 2) && !haveloop) ssrc = 0xFFFFFFFFu;
                    sA = ssrc; sB = ssrc;
                    if (CTL == CTL_LOOPSRC && (flags & 2)) sA = st;   /* A only */
                    if (ssrc>=lo && ssrc+32<=lo+dsz) {
                        sg=1; for(q=0;q<32;q++) if(!drg[ssrc-lo+q]&&!drd[ssrc-lo+q]) sg=0;
                    }
                    if (sg) {
                        if (sA>=lo&&sA+32<=lo+dsz)
                            memcpy(inA, hasA[sA-lo]?stA+(sA-lo):drv+(sA-lo), 32);
                        else memset(inA,0,32);
                        memcpy(inB, hasB[sB-lo]?stB+(sB-lo):drv+(sB-lo), 32);
                    }
                }
                if (ok && sg) {
                    uint32_t dA, dB;
                    cFull++; fFull += F;
                    if (sc>0 && (sc%32)) small_ok++;
                    memcpy(curA,inA,32); memcpy(curB,inB,32);
                    for (q=0;q<8;q++) histA[q]=gs16(curA+16+2*q);
                    stB2[0]=gs16(curB+28); stB2[1]=gs16(curB+30);
                    for (ff=0; ff<F; ff++) {
                        const uint8_t *fr=dmv+si+9*ff;
                        int pred=fr[0]&15; const uint8_t *bk=dmv+CB+pred*32;
                        int16_t oA[16], oB[16]; int mm=0;
                        A_frame(fr,bk,histA,oA);
                        B_frame(fr,bk,stB2,oB);
                        for (q=0;q<16;q++){ samp++; if(oA[q]!=oB[q]){smis++;mm=1;}
                            if (ff<4096) bbuf[16*ff+q]=oB[q]; }
                        if (mm) cmdmis++;
                        for (q=0;q<16;q++){ ps16(curA+2*q,oA[q]); ps16(curB+2*q,oB[q]); }
                    }
                    if (F && memcmp(curA,curB,32)) statemis++;
                    /* third evaluator: the shipped interpreter, same inputs */
                    if (F && F <= 4096) {
                        uint8_t sav[32];
                        uint32_t srcp = (flags&2)?loopa:st;
                        if (srcp>=lo && srcp+32<=lo+dsz) memcpy(sav, drv+(srcp-lo), 32);
                        { uint8_t pin[32]; memcpy(pin,inB,32);
                          if (CTL==CTL_PVAC) pin[30]^=0x40;
                        if (P_command(dmv, w0, w1, si, so, sc, loopa, pin,
                                      drv, lo, dsz, pbuf, F) != 0) pfail++;
                        else { int t; for (t=0;t<16*F;t++)
                                 if (pbuf[t] != bbuf[t]) pmis++; } }
                        if (srcp>=lo && srcp+32<=lo+dsz) memcpy(drv+(srcp-lo), sav, 32);
                    }
                    /* each evaluator's own writeback */
                    dA = st; dB = st;
                    if (CTL == CTL_LOOPDST && (flags & 2) && haveloop) dA = loopa;
                    if (dA>=lo&&dA+32<=lo+dsz){ memcpy(stA+(dA-lo),curA,32); memset(hasA+(dA-lo),1,32); }
                    if (dB>=lo&&dB+32<=lo+dsz){ memcpy(stB+(dB-lo),curB,32); memset(hasB+(dB-lo),1,32); }
                    if (st>=lo&&st+32<=lo+dsz){ memset(drg+(st-lo),0,32); memset(drd+(st-lo),1,32); }
                } else if (st>=lo&&st+32<=lo+dsz) {
                    memset(drg+(st-lo),0,32); memset(drd+(st-lo),0,32);
                }
                taint(so, 32 + 32*F);
            }
        }
    }
    printf("control: %s\n\n", CTLNAME[CTL]);
    printf("ADPCM commands total            : %lld\n", cN);
    printf("commands fully grounded         : %lld\n", cFull);
    printf("frames executed                 : %lld\n", fExec);
    printf("frames fully evaluable          : %lld\n", fFull);
    printf("  of those, commands with count below 32 : %lld\n", small_ok);
    printf("samples compared                : %lld\n", samp);
    printf("SAMPLE MISMATCHES               : %lld\n", smis);
    printf("frames with any mismatch        : %lld\n", cmdmis);
    printf("FINAL-STATE MISMATCHES          : %lld\n", statemis);
    printf("\nSHIPPED INTERPRETER (src/platform/sl_acmd.c) vs evaluator B\n");
    printf("  P sample mismatches           : %lld\n", pmis);
    printf("  P command exec failures       : %lld\n", pfail);
    printf("\n%s\n", (smis==0&&statemis==0&&pmis==0&&pfail==0)
        ? "A, B and the SHIPPED interpreter agree on every sample and final state."
        : "A and B DISAGREE - control fired (or the derivation is inconsistent).");
    return 0;
}

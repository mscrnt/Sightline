/* RESAMPLE full-state oracle: score all 32 predicted bytes against the cartridge.
 *
 * The handler DMAs 32 bytes in (skipped on A_INIT) and 32 bytes out. Its stores
 * cover +0x00..0B and +0x10..1F; +0x0C..0F is never written by either path, so
 * on a continuation it is preserved from DRAM and on an init it is whatever the
 * DMEM scratch already held. The oracle therefore MODELS the 32-byte scratch
 * rather than synthesising fields, and preservation falls out.
 *
 * Populations are partitioned by A_INIT versus CONTINUE and reported separately;
 * mismatches are localised per offset range. Five controls, each required to
 * behave as stated.
 */
#include "../../src/platform/sl_acmd.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SCRATCH 0xF90u
enum { C_NONE=0, C_OLD10, C_BADALIGN, C_ZERO_CD, C_BADP2, C_CORRUPT_TASKSTART };
static int CTL = 0;
static unsigned rs_prev10 = 0;
static const char *CN[] = { "baseline", "old 10-byte writeback", "wrong alignment",
    "zero +0x0C..0F instead of preserving", "wrong source alignment for +0x10",
    "corrupt the scratch's task-start contents" };

static uint32_t be32(const uint8_t*p){return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];}
static int16_t dm16(const sl_acmd_state*s,uint32_t o){return (int16_t)((s->dmem[o]<<8)|s->dmem[o+1]);}
static void put16(uint8_t*m,uint32_t o,int v){ m[o]=(uint8_t)((v>>8)&0xFF); m[o+1]=(uint8_t)(v&0xFF); }

/* ranges scored separately so a failure localises immediately */
static const struct { int lo,hi; const char*nm; } RG[5] = {
    {0x00,0x08,"+0x00..07 history"}, {0x08,0x0A,"+0x08..09 phase"},
    {0x0A,0x0C,"+0x0A..0B align"},   {0x0C,0x10,"+0x0C..0F preserved"},
    {0x10,0x20,"+0x10..1F quad"} };

int main(int argc,char**argv)
{
    const char*path=argc>1?argv[1]:"/tmp/w/all.bin";
    static sl_acmd_state S; FILE*f; uint8_t h[20];
    uint32_t nf,lo,dsz,fi; uint8_t*bef,*aft,*dram;
    long long obs[2]={0,0}, bytes[2]={0,0}, mism[2]={0,0};
    long long rgm[2][5]; long long exercised[2]={0,0}; int ndump=0;
    CTL = argc>2?atoi(argv[2]):0;
    memset(rgm,0,sizeof rgm);
    f=fopen(path,"rb"); if(!f){perror(path);return 2;}
    fread(h,1,20,f); nf=be32(h+8); lo=be32(h+12); dsz=be32(h+16);
    bef=malloc(dsz); aft=malloc(dsz); dram=malloc(dsz);
    for(fi=0;fi<nf;fi++){
        uint8_t fh[12]; uint32_t n,k,*w; int ok=1;
        uint32_t lastw[128], lasta[128]; int nl=0;
        if(fread(fh,1,12,f)!=12)break; n=be32(fh+8); if(n>4096)break;
        w=malloc(8*n);
        for(k=0;k<n;k++){uint8_t c[8];fread(c,1,8,f);w[2*k]=be32(c);w[2*k+1]=be32(c+4);}
        fread(bef,1,dsz,f); fread(aft,1,dsz,f);
        /* last DRAM writer, by byte range, over every DRAM-writing opcode */
        for(k=0;k<n;k++){ uint32_t a=w[2*k+1]&0xFFFFFF; int o2=(int)((w[2*k]>>24)&0xFF);
            if(o2==5){ int q,fnd=-1; for(q=0;q<nl;q++) if(lasta[q]==a){fnd=q;break;}
                if(fnd<0&&nl<128){lasta[nl]=a;lastw[nl]=k;nl++;} else if(fnd>=0) lastw[fnd]=k; } }
        memcpy(dram,bef,dsz); sl_acmd_init(&S,dram,lo,dsz);
        for(k=0;k<n && ok;k++){
            uint32_t w0=w[2*k],w1=w[2*k+1],cmd[2];
            if(((w0>>24)&0xFF)==5){
                uint32_t st=w1&0xFFFFFF; int init=(int)((w0>>16)&1);
                uint32_t in,c2,P0,P1,P2; int32_t fa,acc,a0; int q,islast=0; uint16_t phase0=0;
                uint8_t pred[32];
                exercised[init]++;
                /* per-command corruption isolates a continuation's OWN scratch
                 * dependence: the 32-byte read must wipe it. Per-TASK
                 * corruption conflated that with legitimate propagation through
                 * DRAM from an earlier init, which is why it moved CONTINUE. */
                /* Corrupt ONLY before a continuation. Corrupting before any
                 * command still reaches continuations indirectly, because an
                 * init does not reload the scratch and writes it to memory,
                 * where a later continuation legitimately reads it back. Only
                 * this form isolates a continuation's OWN scratch dependence,
                 * which must be none. */
                if(CTL==C_CORRUPT_TASKSTART && !init) memset(S.dmem+SCRATCH,0x5A,32);
                for(q=0;q<nl;q++) if(lasta[q]==st&&lastw[q]==k) islast=1;
                /* model the scratch: read 32 from DRAM unless A_INIT */
                if(!init && st>=lo && st+32<=lo+dsz){
                    memcpy(S.dmem+SCRATCH, dram+(st-lo), 32);
                    phase0=(uint16_t)((dram[st-lo+8]<<8)|dram[st-lo+9]); }
                cmd[0]=w0;cmd[1]=w1;
                in=sl_acmd_sget(&S,0x00); c2=sl_acmd_sget(&S,0x04);
                if(sl_acmd_exec(&S,cmd,1)!=SL_ACMD_OK){ok=0;break;}
                /* the production writeback already produced +0x00..09 in DRAM */
                if(st>=lo&&st+32<=lo+dsz) memcpy(S.dmem+SCRATCH, dram+(st-lo), 10);
                if(init){ memset(S.dmem+SCRATCH,0,10); }
                /* The accumulator after the output loop: the incoming phase
                 * plus two pitch steps per output sample. Production stores
                 * only its low half at +0x08, so the integer advance has to be
                 * recomputed here rather than read back. */
                { int32_t pitch=(int32_t)(w0&0xFFFF);
                  int32_t nout=(int32_t)(c2/2);
                  int32_t phase_in = init ? 0 : (int32_t)phase0;
                  int64_t a = (int64_t)phase_in + (int64_t)2*nout*pitch;
                  acc = (int32_t)a;
                  fa  = (int32_t)(a >> 16); }
                P0 = in - 8 + 2*(uint32_t)fa;
                P1 = P0 + 8;
                a0 = (int32_t)((P1 - in) & 15);
                if(CTL==C_BADALIGN) put16(S.dmem,SCRATCH+10,a0);
                else                put16(S.dmem,SCRATCH+10,(a0==0)?0:16-a0);
                P2 = (CTL==C_BADP2) ? P1 : (P1 - (uint32_t)a0);
                /* what the scratch already held at +0x10..11, before this
                 * command's quad copy - the candidate for a store that
                 * never reaches the first element */
                { unsigned prev10 = ((unsigned)S.dmem[SCRATCH+16]<<8)|S.dmem[SCRATCH+17];
                  rs_prev10 = prev10; }
                if(P2+16<=SL_DMEM_SIZE) memcpy(S.dmem+SCRATCH+16,S.dmem+P2,16);
                if(CTL==C_ZERO_CD) memset(S.dmem+SCRATCH+12,0,4);
                memcpy(pred,S.dmem+SCRATCH,32);
                if(CTL==C_OLD10 && st>=lo&&st+32<=lo+dsz) memcpy(pred+10,bef+(st-lo)+10,22);
                if(st>=lo&&st+32<=lo+dsz) memcpy(dram+(st-lo),pred,32);
                /* WHERE DO THE CARTRIDGE'S +0x10..11 BYTES COME FROM?
                 * The corpus is silent on the RSP's vector-load semantics -
                 * gedocs search 'LDV SQV vector load element wrap' and 'RSP
                 * microcode vector' both return nothing, and the routing file
                 * already carries an 'audio microcode / ACMD command set'
                 * not_covered entry - so this is derived by measurement, not
                 * by reasoning about LDV. RS_DUMP=1 prints the DMEM
                 * neighbourhood of P2 next to the cartridge's actual quad, so
                 * the source offset can be READ OFF rather than guessed. */
                if(getenv("RS_DUMP") && islast && st>=lo && st+32<=lo+dsz){
                    int bad=0,q2; for(q2=0x10;q2<0x20;q2++)
                        if(pred[q2]!=aft[st-lo+q2]) bad=1;
                    if(bad){
                        int d;
                        printf("  DUMP task %u cmd %u P2 %#05x (P2&15=%u) in %#05x fa %d a0 %d\n",
                               fi,k,P2,P2&15,in,fa,a0);
                        printf("    cart quad :");
                        for(d=0;d<16;d++) printf(" %02x",aft[st-lo+0x10+d]);
                        printf("\n    pred quad :");
                        for(d=0;d<16;d++) printf(" %02x",pred[0x10+d]);
                        printf("\n    dmem P2-16..P2+31:\n");
                        for(d=-16;d<32;d+=16){
                            int e; printf("      %+4d:",d);
                            for(e=0;e<16;e++){ int ad=(int)P2+d+e;
                                if(ad>=0&&ad<SL_DMEM_SIZE) printf(" %02x",S.dmem[ad]);
                                else printf(" --"); }
                            printf("\n"); }
                        /* locate the cartridge halfword anywhere nearby */
                        { unsigned want=((unsigned)aft[st-lo+0x10]<<8)|aft[st-lo+0x11];
                          unsigned nout=c2/2;
                          printf("      candidates: prev_scratch+0x10=%04x  "
                                 "out_last=%04x out_first=%04x  dmem[P2-2]=%04x  "
                                 "dmem[P1]=%04x\n",
                             rs_prev10,
                             nout? (((unsigned)S.dmem[(P0+2*(nout-1))&0xFFF]<<8)
                                    |S.dmem[(P0+2*(nout-1)+1)&0xFFF]) : 0,
                             (((unsigned)S.dmem[P0&0xFFF]<<8)|S.dmem[(P0+1)&0xFFF]),
                             (((unsigned)S.dmem[(P2-2)&0xFFF]<<8)|S.dmem[(P2-1)&0xFFF]),
                             (((unsigned)S.dmem[P1&0xFFF]<<8)|S.dmem[(P1+1)&0xFFF]));
                          printf("      bef+0x10..11=%04x  bef+0x0C..0F=%02x%02x%02x%02x\n",
                             (((unsigned)bef[st-lo+0x10]<<8)|bef[st-lo+0x11]),
                             bef[st-lo+0x0C],bef[st-lo+0x0D],bef[st-lo+0x0E],bef[st-lo+0x0F]);
                          if((((unsigned)bef[st-lo+0x10]<<8)|bef[st-lo+0x11])==want)
                             printf("      *** PRESERVED FROM INCOMING DRAM STATE ***\n");
                          printf("      MATCHES: %s%s%s\n",
                             rs_prev10==want?"prev_scratch ":"",
                             ((((unsigned)S.dmem[(P2-2)&0xFFF]<<8)|S.dmem[(P2-1)&0xFFF])==want)?"dmemP2-2 ":"",
                             ((((unsigned)S.dmem[P1&0xFFF]<<8)|S.dmem[(P1+1)&0xFFF])==want)?"dmemP1 ":""); }
                        { int ad; unsigned want=((unsigned)aft[st-lo+0x10]<<8)|aft[st-lo+0x11];
                          printf("      cart +0x10..11 = %04x found at:",want);
                          for(ad=(int)P2-64; ad<(int)P2+64; ad++)
                            if(ad>=0&&ad+1<SL_DMEM_SIZE &&
                               ((unsigned)S.dmem[ad]<<8|S.dmem[ad+1])==want)
                                printf(" P2%+d",ad-(int)P2);
                          printf("\n"); }
                    }
                }
                if(islast && st>=lo && st+32<=lo+dsz){
                    obs[init]++;
                    for(q=0;q<32;q++){ int r; bytes[init]++;
                        if(pred[q]!=aft[st-lo+q]){ mism[init]++;
                            if(q>=0x10 && ndump<6){ ndump++;
                                printf("  QUAD MISS task %3u cmd %3u st %#08x off %#04x "
                                       "pred %#04x aft %#04x | in %#05x fa %d a0 %d P2 %#05x cnt %u\n",
                                       fi,k,st,q,pred[q],aft[st-lo+q],in,fa,a0,P2,c2); }
                            for(r=0;r<5;r++) if(q>=RG[r].lo&&q<RG[r].hi){rgm[init][r]++;break;} } }
                }
                continue;
            }
            cmd[0]=w0;cmd[1]=w1;
            if(sl_acmd_exec(&S,cmd,1)!=SL_ACMD_OK) ok=0;
        }
        free(w);
    }
    printf("RESAMPLE full-state oracle   control: %s\n\n",CN[CTL]);
    printf("  RESAMPLE commands exercised : A_INIT %lld   CONTINUE %lld\n",
           exercised[1],exercised[0]);
    printf("  last-writer OBSERVABLE      : A_INIT %lld   CONTINUE %lld\n",obs[1],obs[0]);
    printf("  bytes compared              : A_INIT %lld   CONTINUE %lld\n",bytes[1],bytes[0]);
    printf("  MISMATCHES                  : A_INIT %lld   CONTINUE %lld\n",mism[1],mism[0]);
    printf("\n  by offset range:            A_INIT    CONTINUE\n");
    { int r; for(r=0;r<5;r++)
        printf("    %-24s %7lld    %7lld\n",RG[r].nm,rgm[1][r],rgm[0][r]); }
    return 0;
}

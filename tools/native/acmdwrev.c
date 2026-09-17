/* Write-EVENT provenance: who physically wrote a byte, independent of whether
 * the value changed. The three concepts are kept separately inspectable:
 *   EXPECTED writer  - semantics say the opcode owns the range
 *   ACTUAL   writer  - the evaluator physically issued the write
 *   VALUE CHANGED    - the byte's contents differ afterwards
 * Never infer the first from the third.
 */
#include "../../src/platform/sl_acmd.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WMAX 200000
static struct { uint32_t a,l; int op,ep; } WEV[WMAX];
static int NW=0, EPOCH=0;
void sl_acmd_wrlog(uint32_t a,uint32_t l,int op)
{ if(NW<WMAX){ WEV[NW].a=a; WEV[NW].l=l; WEV[NW].op=op; WEV[NW].ep=EPOCH; NW++; } }

static uint32_t be32(const uint8_t*p){return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];}

/* ---- tracker acceptance gate: a store that writes the same value back ---- */
static int samevalue_control(void)
{
    static uint8_t dram[0x2000]; static sl_acmd_state S;
    uint32_t cmd[8]; int i, ev_with, ev_without; uint8_t before[16], after[16];
    const uint32_t LO=0x1000, SRC=0x1000, DST=0x1800;

    /* DST already holds exactly what the save will write, so the after-image
     * cannot reveal the write. */
    memset(dram,0,sizeof dram);
    for(i=0;i<16;i++){ dram[SRC-LO+i]=(uint8_t)(0x40+i); dram[DST-LO+i]=(uint8_t)(0x40+i); }
    memcpy(before,dram+(DST-LO),16);
    sl_acmd_init(&S,dram,LO,sizeof dram);
    NW=0; EPOCH=1;
    /* one buffer offset for both, so the save writes back exactly the bytes
     * the load brought in - and DST already holds those bytes */
    cmd[0]=(8u<<24)|0x100u; cmd[1]=(0x100u<<16)|16u;  sl_acmd_exec(&S,cmd,1); /* SETBUFF */
    cmd[0]=(4u<<24);        cmd[1]=SRC;               sl_acmd_exec(&S,cmd,1); /* LOADBUFF */
    EPOCH=2;
    cmd[0]=(6u<<24);        cmd[1]=DST;               sl_acmd_exec(&S,cmd,1); /* SAVEBUFF */
    memcpy(after,dram+(DST-LO),16);
    ev_with=NW;

    /* now omit the store entirely; the final bytes still read the same */
    memset(dram,0,sizeof dram);
    for(i=0;i<16;i++){ dram[SRC-LO+i]=(uint8_t)(0x40+i); dram[DST-LO+i]=(uint8_t)(0x40+i); }
    sl_acmd_init(&S,dram,LO,sizeof dram);
    NW=0; EPOCH=1;
    cmd[0]=(8u<<24)|0x100u; cmd[1]=(0x100u<<16)|16u;  sl_acmd_exec(&S,cmd,1);
    cmd[0]=(4u<<24);        cmd[1]=SRC;               sl_acmd_exec(&S,cmd,1);
    ev_without=NW;

    printf("TRACKER ACCEPTANCE GATE - same-value write control\n");
    printf("  with the store   : write events %d, value changed %s\n",
           ev_with, memcmp(before,after,16)?"YES":"no");
    printf("  without it       : write events %d, final bytes identical %s\n",
           ev_without, memcmp(before,dram+(DST-LO),16)?"no":"YES");
    { int ok = (ev_with>ev_without) && !memcmp(before,after,16);
      printf("  %s\n\n", ok
        ? "GATE PASSES: the write event is recorded although the value never\n"
          "    changed, and disappears when the store is removed while the\n"
          "    final bytes still read the same. Events are not value diffs."
        : "GATE FAILS: the tracker cannot see a same-value write; do not use it.");
      return ok; }
}

int main(int argc,char**argv)
{
    const char*path=argc>1?argv[1]:"/tmp/w/all.bin";
    static sl_acmd_state S; FILE*f; uint8_t h[20];
    uint32_t nf,lo,dsz,fi; uint8_t*bef,*aft,*dram;
    long long edges=0, clean=0, overlap=0; long long byop[16];
    memset(byop,0,sizeof byop);
    if(!samevalue_control()) return 3;
    f=fopen(path,"rb"); if(!f){perror(path);return 2;}
    fread(h,1,20,f); nf=be32(h+8); lo=be32(h+12); dsz=be32(h+16);
    bef=malloc(dsz); aft=malloc(dsz); dram=malloc(dsz);
    for(fi=0;fi<nf;fi++){
        uint8_t fh[12]; uint32_t n,k,*w; int ok=1;
        if(fread(fh,1,12,f)!=12)break; n=be32(fh+8); if(n>4096)break;
        w=malloc(8*n);
        for(k=0;k<n;k++){uint8_t c[8];fread(c,1,8,f);w[2*k]=be32(c);w[2*k+1]=be32(c+4);}
        fread(bef,1,dsz,f); fread(aft,1,dsz,f);
        memcpy(dram,bef,dsz); sl_acmd_init(&S,dram,lo,dsz);
        NW=0;
        for(k=0;k<n&&ok;k++){
            uint32_t w0=w[2*k],w1=w[2*k+1],cmd[2];
            EPOCH=(int)k;
            if(((w0>>24)&0xFF)==3) continue;      /* mixer unimplemented */
            cmd[0]=w0;cmd[1]=w1;
            if(sl_acmd_exec(&S,cmd,1)!=SL_ACMD_OK) ok=0;
        }
        /* for each mixer command, did any LATER write EVENT touch its range? */
        for(k=0;k<n;k++){
            uint32_t w0=w[2*k],w1=w[2*k+1],st; int j,hit=-1;
            if(((w0>>24)&0xFF)!=3) continue;
            st=w1&0xFFFFFF; if(st<lo||st+80>lo+dsz) continue;
            edges++;
            for(j=0;j<NW;j++){
                if(WEV[j].ep<=(int)k) continue;
                if(WEV[j].a+WEV[j].l<=st || WEV[j].a>=st+80) continue;
                hit=WEV[j].op; break;
            }
            if(hit<0) clean++;
            else { overlap++; if(hit>=0&&hit<16) byop[hit]++; }
        }
        free(w);
    }
    printf("WRITE-EVENT audit over mixer state ranges (events, not value diffs)\n");
    printf("  mixer commands examined                  : %lld\n",edges);
    printf("  ranges with NO later write EVENT         : %lld\n",clean);
    printf("  ranges with a later write EVENT          : %lld\n",overlap);
    { const char*N[16]={"SPNOOP","ADPCM","CLEARBUFF","ENVMIXER","LOADBUFF","RESAMPLE","SAVEBUFF",
        "SEGMENT","SETBUFF","SETVOL","DMEMMOVE","LOADADPCM","MIXER","INTERLEAVE","POLEF","SETLOOP"};
      int q; for(q=0;q<16;q++) if(byop[q]) printf("    later writer %-11s : %lld\n",N[q],byop[q]); }
    return 0;
}

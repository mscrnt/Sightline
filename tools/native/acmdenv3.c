/* Three-world chronology comparison for ENVMIXER persistent state.
 *
 * Each world owns its DMEM and DRAM outright and walks the whole capture
 * independently; command N+1 in a world consumes that world's own command-N
 * write. Nothing is reseeded from the cartridge or from another world once a
 * chain begins - the worlds only share the captured task-start image.
 *
 *   L  literal: follows the instruction order, carrying the paired accumulators
 *      and applying the clamps as the compare and clip forms.
 *   S  scalar: an independently expressed recurrence - closed-form lane ramp,
 *      per-group advance, signed max and the wrapped-difference rule.
 *   T  the traversal evaluator's own formulation.
 *
 * They share no helper for the ramp, the advance or the clamps.
 */
#include "../../src/platform/sl_acmd.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int16_t cl16(int64_t v){ if(v>32767)return 32767; if(v<-32768)return -32768; return (int16_t)v; }
static int64_t a48(int64_t v){ v&=0xFFFFFFFFFFFFLL; if(v&0x800000000000LL)v-=0x1000000000000LL; return v; }
static int fires(int64_t a){ int64_t t=a>>31; return !(t==0||t==-1); }
long long NEGU=0;
static int16_t lowrule(int64_t a){ if(!fires(a)) return (int16_t)(a&0xFFFF);
    if(a>0) return (int16_t)0xFFFF; NEGU++; return (int16_t)0x0000; }
static int16_t g16(const uint8_t*m,uint32_t o){return (int16_t)((m[o]<<8)|m[o+1]);}
static void p16(uint8_t*m,uint32_t o,int v){ m[o]=(uint8_t)((v>>8)&0xFF); m[o+1]=(uint8_t)(v&0xFF); }

typedef struct { int16_t hi[8], lo[8]; } Env;

/* ---- world L: literal, paired accumulators, clamp forms ------------------ */
static void L_build(const int16_t*ramp,int16_t cv,int16_t rm,uint16_t rl,Env*e)
{ int k; for(k=0;k<8;k++){ int64_t a=0;
    a=a48(((int64_t)(uint16_t)ramp[k]*(int64_t)rl)>>16);
    a=a48(a+(int64_t)(uint16_t)ramp[k]*(int64_t)rm);
    a=a48(a+(((int64_t)1*(int64_t)cv)<<16));
    e->hi[k]=cl16(a>>16); e->lo[k]=lowrule(a); } }
static void L_clamp(Env*e,int16_t tg,int16_t rm)
{ int k; for(k=0;k<8;k++){
    if(rm>0){ uint16_t d=(uint16_t)((uint16_t)e->hi[k]-(uint16_t)tg);
              e->hi[k]=((int16_t)d>=0)?tg:e->hi[k]; }
    else    { int sel=(e->hi[k]>tg)||(e->hi[k]==tg); e->hi[k]=sel?e->hi[k]:tg; } } }
static void L_adv(Env*e,int16_t rm,uint16_t rl)
{ int k; for(k=0;k<8;k++){ uint32_t s=(uint32_t)(uint16_t)e->lo[k]+(uint32_t)rl;
    e->lo[k]=(int16_t)(s&0xFFFF);
    e->hi[k]=cl16((int32_t)e->hi[k]+(int32_t)rm+(int)((s>>16)&1)); } }

/* ---- world S: independently expressed scalar recurrence ------------------ */
static void S_build(const int16_t*ramp,int16_t cv,int16_t rm,uint16_t rl,Env*e)
{ int k; int32_t rate=((int32_t)rm<<16)|(int32_t)rl;
  for(k=0;k<8;k++){ int64_t f=(int64_t)(uint16_t)ramp[k];
    int64_t env=((int64_t)cv<<16)+(((f*(int64_t)rl)>>16)+f*(int64_t)rm);
    (void)rate;
    e->hi[k]=cl16(env>>16);
    e->lo[k]= (env>(int64_t)2147483647LL)?(int16_t)0xFFFF
            : (env<(int64_t)(-2147483647LL-1))?(int16_t)0x0000
            : (int16_t)(env&0xFFFF); } }
static void S_clamp(Env*e,int16_t tg,int16_t rm)
{ int k; for(k=0;k<8;k++){
    if(rm>0){ uint16_t d=(uint16_t)((uint16_t)e->hi[k]-(uint16_t)tg);
              if((int16_t)d>=0) e->hi[k]=tg; }
    else if(e->hi[k]<tg) e->hi[k]=tg; } }
static void S_adv(Env*e,int16_t rm,uint16_t rl)
{ int k; for(k=0;k<8;k++){ int32_t t=(int32_t)(uint16_t)e->lo[k]+(int32_t)rl;
    int c=(t>=65536); e->lo[k]=(int16_t)(t&0xFFFF);
    e->hi[k]=cl16((int32_t)e->hi[k]+(int32_t)rm+c); } }

/* ---- one command, per world; W=0 literal, 1 scalar ---------------------- */
static void run_cmd(int W,sl_acmd_state*S,uint32_t w0,uint32_t w1,
                    const int16_t*ramp,uint32_t lo,uint32_t dsz,uint8_t*out80,
                    int corrupt)
{
    int init=(int)((w0>>16)&1), aux=(int)((w0>>16)&8);
    uint32_t st=w1&0xFFFFFF;
    uint32_t in=sl_acmd_sget(S,0x00), d3=sl_acmd_sget(S,0x02);
    uint32_t d2=sl_acmd_sget(S,0x0A), d1=sl_acmd_sget(S,0x0C), d0=sl_acmd_sget(S,0x0E);
    int cnt=(int)sl_acmd_sget(S,0x04);
    int16_t tL,mL,tR,mR,dry,wet; uint16_t lL,lR;
    Env L,R; int g,G,step=aux?16:0; uint8_t*p;
    tL=sl_acmd_sget(S,0x10); mL=sl_acmd_sget(S,0x12); lL=sl_acmd_sget(S,0x14);
    tR=sl_acmd_sget(S,0x16); mR=sl_acmd_sget(S,0x18); lR=sl_acmd_sget(S,0x1A);
    dry=sl_acmd_sget(S,0x1C); wet=sl_acmd_sget(S,0x1E);
    if(!aux){ d1=d0=0xF90+80; }
    G=(cnt>0)?((cnt+15)/16):1;
    p=(st>=lo&&st+80<=lo+dsz)?S->dram+(st-lo):0;
    if(!p){ memset(out80,0,80); return; }
    if(init){ if(W==0){ L_build(ramp,(int16_t)sl_acmd_sget(S,0x06),mL,lL,&L);
                        L_build(ramp,(int16_t)sl_acmd_sget(S,0x08),mR,lR,&R); }
              else    { S_build(ramp,(int16_t)sl_acmd_sget(S,0x06),mL,lL,&L);
                        S_build(ramp,(int16_t)sl_acmd_sget(S,0x08),mR,lR,&R); } }
    else {
        for(g=0;g<8;g++){ L.hi[g]=g16(p,2*g); L.lo[g]=g16(p,16+2*g);
                          R.hi[g]=g16(p,32+2*g); R.lo[g]=g16(p,48+2*g); }
        tL=g16(p,64+0); mL=g16(p,64+2); lL=(uint16_t)g16(p,64+4);
        tR=g16(p,64+6); mR=g16(p,64+8); lR=(uint16_t)g16(p,64+10);
        dry=g16(p,64+12); wet=g16(p,64+14);
    }
    if(init){ if(W==0){L_clamp(&L,tL,mL);L_clamp(&R,tR,mR);} else {S_clamp(&L,tL,mL);S_clamp(&R,tR,mR);}
              in+=16; d3+=16; d2+=16; d1+=step; d0+=step; cnt-=16; }
    if(W==0) L_adv(&L,mL,lL); else S_adv(&L,mL,lL);
    for(g=0;g<G-(init?1:0);g++){
        if(W==0){ L_clamp(&L,tL,mL); L_adv(&R,mR,lR); } else { S_clamp(&L,tL,mL); S_adv(&R,mR,lR); }
        for(int k2=0;k2<8;k2++){ p16(S->dmem,0xF90+2*k2,L.hi[k2]); p16(S->dmem,0xF90+16+2*k2,L.lo[k2]); }
        if(W==0){ L_clamp(&R,tR,mR); L_adv(&L,mL,lL); } else { S_clamp(&R,tR,mR); S_adv(&L,mL,lL); }
    }
    for(g=0;g<8;g++){ p16(S->dmem,0xF90+32+2*g,R.hi[g]); p16(S->dmem,0xF90+48+2*g,R.lo[g]); }
    p16(S->dmem,0xF90+64+0,tL); p16(S->dmem,0xF90+64+2,mL);
    p16(S->dmem,0xF90+64+4,(int16_t)lL);
    p16(S->dmem,0xF90+64+6,tR); p16(S->dmem,0xF90+64+8,mR);
    p16(S->dmem,0xF90+64+10,(int16_t)lR);
    p16(S->dmem,0xF90+64+12,dry); p16(S->dmem,0xF90+64+14,wet);
    memcpy(p,S->dmem+0xF90,80);
    if(corrupt) memset(p+64,0x5A,16);      /* chronology control */
    memcpy(out80,p,80);
}
static uint32_t be32(const uint8_t*q){return (uint32_t)q[0]<<24|(uint32_t)q[1]<<16|(uint32_t)q[2]<<8|q[3];}

#define MAXC 400000
static uint8_t ST_[3][MAXC][80]; static int NC=0;
static uint32_t CT[MAXC], CI[MAXC]; static int CINIT[MAXC];

int main(int argc,char**argv)
{
    const char*path=argc>1?argv[1]:"/tmp/w/all.bin";
    int CORRUPT=argc>2?atoi(argv[2]):0;   /* 1 = corrupt params after an init */
    int16_t ramp[8]; uint8_t db[0x2c0];
    FILE*f,*d; uint8_t h[20]; uint32_t nf,lo,dsz,fi; uint8_t*bef,*dram;
    static sl_acmd_state S; int W;
    long long ninit=0,ncont=0;
    d=fopen("bin/aspboot.data.bin","rb"); if(!d){fprintf(stderr,"FATAL: extraction missing\n");return 2;}
    fread(db,1,sizeof db,d); fclose(d);
    { int k; for(k=0;k<8;k++) ramp[k]=(int16_t)((db[0xB0+2*k]<<8)|db[0xB0+2*k+1]); }
    for(W=0;W<2;W++){
        int idx=0;
        f=fopen(path,"rb"); if(!f){perror(path);return 2;}
        fread(h,1,20,f); nf=be32(h+8); lo=be32(h+12); dsz=be32(h+16);
        bef=malloc(dsz); dram=malloc(dsz);
        for(fi=0;fi<nf;fi++){
            uint8_t fh[12]; uint32_t n,k,*w; int ok=1, seen_init=0;
            if(fread(fh,1,12,f)!=12)break; n=be32(fh+8); if(n>4096)break;
            w=malloc(8*n);
            for(k=0;k<n;k++){uint8_t c[8];fread(c,1,8,f);w[2*k]=be32(c);w[2*k+1]=be32(c+4);}
            fread(bef,1,dsz,f); fseek(f,dsz,SEEK_CUR);
            memcpy(dram,bef,dsz); sl_acmd_init(&S,dram,lo,dsz);
            for(k=0;k<n&&ok;k++){
                uint32_t w0=w[2*k],w1=w[2*k+1],cmd[2];
                if(((w0>>24)&0xFF)==3){
                    int isinit=(int)((w0>>16)&1);
                    int doc = (CORRUPT==1 && isinit && !seen_init && W==0);
                    if(CORRUPT==2 && isinit && !seen_init && W==0){
                        uint32_t st2=w1&0xFFFFFF;
                        if(st2>=lo && st2+80<=lo+dsz) memset(dram+(st2-lo),0x5A,80); }
                    if(isinit) seen_init=1;
                    if(idx<MAXC){ run_cmd(W,&S,w0,w1,ramp,lo,dsz,ST_[W][idx],doc);
                        if(W==0){ CT[idx]=fi; CI[idx]=k; CINIT[idx]=isinit;
                                  if(isinit) ninit++; else ncont++; }
                        idx++; }
                    continue; }
                cmd[0]=w0;cmd[1]=w1;
                if(sl_acmd_exec(&S,cmd,1)!=SL_ACMD_OK) ok=0;
            }
            free(w);
        }
        fclose(f); free(bef); free(dram);
        if(W==0) NC=idx;
    }
    { long long mis=0,cmds=0,firstT=-1,firstC=-1,firstOff=-1; int i,q;
      for(i=0;i<NC;i++){ int bad=0;
        for(q=0;q<80;q++) if(ST_[0][i][q]!=ST_[1][i][q]){ mis++; bad=1;
            if(firstT<0){firstT=CT[i];firstC=CI[i];firstOff=q;} }
        if(bad) cmds++; }
      printf("Three-world chronology: LITERAL vs SCALAR%s\n\n",
             CORRUPT==1?"   [CONTROL+: params corrupted AFTER first init in world L]":
             CORRUPT==2?"   [CONTROL-: state corrupted BEFORE an init, overwritten before use]":"");
      printf("  ENVMIXER commands walked : %d   (init %lld, continuation %lld)\n",NC,ninit,ncont);
      printf("  state bytes compared     : %d\n",NC*80);
      printf("  MISMATCHING bytes        : %lld\n",mis);
      printf("  commands with a mismatch : %lld\n",cmds);
      if(firstT>=0) printf("  first divergence         : task %lld cmd %lld offset %lld\n",
                           firstT,firstC,firstOff);
      printf("  ungrounded negative-branch takings: %lld\n",NEGU);
    }
    return 0;
}

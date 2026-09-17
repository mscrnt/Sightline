/* Stage 2: an independent scalar world with its OWN lifecycle plumbing,
 * compared against the traversal world across the whole measured chronology.
 *
 * The two worlds share command parsing and immutable inputs (the capture, the
 * ramp constants). They share NO implementation of the disputed semantics:
 * the continuation state source, the parameter source, the state load and
 * writeback, or reset. Each keeps its own DRAM, its own persistent-state map,
 * and its own boundary carry.
 */
#include "../../src/platform/sl_acmd.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int16_t cl16(int64_t v){ if(v>32767)return 32767; if(v<-32768)return -32768; return (int16_t)v; }
static int64_t a48(int64_t v){ v&=0xFFFFFFFFFFFFLL; if(v&0x800000000000LL)v-=0x1000000000000LL; return v; }
static int16_t g16(const uint8_t*m,uint32_t o){return (int16_t)((m[o]<<8)|m[o+1]);}
static void p16(uint8_t*m,uint32_t o,int v){ m[o]=(uint8_t)((v>>8)&0xFF); m[o+1]=(uint8_t)(v&0xFF); }
static uint32_t be32(const uint8_t*p){return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];}

/* ================= WORLD T : traversal formulation ======================= */
static int fires_T(int64_t a){ int64_t t=a>>31; return !(t==0||t==-1); }
long long NEG_T=0;
static int16_t low_T(int64_t a){ if(!fires_T(a)) return (int16_t)(a&0xFFFF);
    if(a>0) return (int16_t)0xFFFF; NEG_T++; return (int16_t)0x0000; }
typedef struct { int16_t hi[8], lo[8]; } EnvT;
static void T_build(const int16_t*r,int16_t cv,int16_t rm,uint16_t rl,EnvT*e)
{ int k; for(k=0;k<8;k++){ int64_t a;
    a=a48(((int64_t)(uint16_t)r[k]*(int64_t)rl)>>16);
    a=a48(a+(int64_t)(uint16_t)r[k]*(int64_t)rm);
    a=a48(a+(((int64_t)1*(int64_t)cv)<<16));
    e->hi[k]=cl16(a>>16); e->lo[k]=low_T(a); } }
static void T_clamp(EnvT*e,int16_t tg,int16_t rm)
{ int k; for(k=0;k<8;k++){
    if(rm>0){ uint16_t d=(uint16_t)((uint16_t)e->hi[k]-(uint16_t)tg);
              e->hi[k]=((int16_t)d>=0)?tg:e->hi[k]; }
    else e->hi[k]=(e->hi[k]>tg)?e->hi[k]:tg; } }
static void T_adv(EnvT*e,int16_t rm,uint16_t rl)
{ int k; for(k=0;k<8;k++){ uint32_t s=(uint32_t)(uint16_t)e->lo[k]+(uint32_t)rl;
    e->lo[k]=(int16_t)(s&0xFFFF);
    e->hi[k]=cl16((int32_t)e->hi[k]+(int32_t)rm+(int)((s>>16)&1)); } }

static void T_exec(sl_acmd_state*S,uint32_t w0,uint32_t w1,const int16_t*ramp,
                   uint32_t lo,uint32_t dsz,uint8_t*out80)
{
    int init=(int)((w0>>16)&1); uint32_t st=w1&0xFFFFFF;
    int cnt=(int)sl_acmd_sget(S,0x04); EnvT L,R; int g,G; uint8_t*p;
    int16_t tL,mL,tR,mR,dry,wet; uint16_t lL,lR;
    p=(st>=lo&&st+80<=lo+dsz)?S->dram+(st-lo):0; if(!p){memset(out80,0,80);return;}
    tL=sl_acmd_sget(S,0x10); mL=sl_acmd_sget(S,0x12); lL=sl_acmd_sget(S,0x14);
    tR=sl_acmd_sget(S,0x16); mR=sl_acmd_sget(S,0x18); lR=sl_acmd_sget(S,0x1A);
    dry=sl_acmd_sget(S,0x1C); wet=sl_acmd_sget(S,0x1E);
    G=(cnt>0)?((cnt+15)/16):1;
    if(init){ T_build(ramp,(int16_t)sl_acmd_sget(S,0x06),mL,lL,&L);
              T_build(ramp,(int16_t)sl_acmd_sget(S,0x08),mR,lR,&R);
              T_clamp(&L,tL,mL); T_clamp(&R,tR,mR); }
    else { for(g=0;g<8;g++){ L.hi[g]=g16(p,2*g); L.lo[g]=g16(p,16+2*g);
                             R.hi[g]=g16(p,32+2*g); R.lo[g]=g16(p,48+2*g); }
           tL=g16(p,64+0); mL=g16(p,64+2); lL=(uint16_t)g16(p,64+4);
           tR=g16(p,64+6); mR=g16(p,64+8); lR=(uint16_t)g16(p,64+10);
           dry=g16(p,64+12); wet=g16(p,64+14); }
    T_adv(&L,mL,lL);
    for(g=0; g<G-(init?1:0); g++){ T_clamp(&L,tL,mL); T_adv(&R,mR,lR);
                                   T_clamp(&R,tR,mR); T_adv(&L,mL,lL); }
    for(g=0;g<8;g++){ p16(p,2*g,L.hi[g]); p16(p,16+2*g,L.lo[g]);
                      p16(p,32+2*g,R.hi[g]); p16(p,48+2*g,R.lo[g]); }
    p16(p,64+0,tL); p16(p,64+2,mL); p16(p,64+4,(int16_t)lL);
    p16(p,64+6,tR); p16(p,64+8,mR); p16(p,64+10,(int16_t)lR);
    p16(p,64+12,dry); p16(p,64+14,wet);
    memcpy(out80,p,80);
}

/* ================= WORLD S : independent scalar + own lifecycle =========== */
/* S keeps its persistent state in its OWN map, not in DRAM, and reconstructs
 * its view from that map. Its parameter source, load, writeback and reset are
 * written separately from T's. */
#define SMAX 64
static uint32_t s_addr[SMAX]; static int16_t s_hiL[SMAX][8], s_loL[SMAX][8];
static int16_t s_hiR[SMAX][8], s_loR[SMAX][8], s_par[SMAX][8];
static int s_valid[SMAX], s_n=0;
static int s_slot(uint32_t a){ int i; for(i=0;i<s_n;i++) if(s_addr[i]==a) return i;
    if(s_n<SMAX){ s_addr[s_n]=a; s_valid[s_n]=0; return s_n++; } return -1; }
long long NEG_S=0;
/* Controls, applied to world S ONLY so any divergence is attributable.
 *  1 corrupt S's persisted state after a grounded writer -> must propagate
 *    across the boundary to the consumer in the next task
 *  2 same corruption, but an init intervenes -> the reset must suppress it
 *  3 bad READ source: take continuation parameters from the live task-state
 *  4 bad WRITE source: store live task-state parameters instead of executed
 */
static int CTL=0; static uint32_t CTL_ADDR=0; static int CTL_TASK=-1, CTL_CMD=-1;
long long CTL_FIRED=0;
static int16_t low_S(int64_t e)
{ if(e>(int64_t)2147483647LL) return (int16_t)0xFFFF;
  if(e<(int64_t)(-2147483647LL-1)){ NEG_S++; return (int16_t)0x0000; }
  return (int16_t)(e&0xFFFF); }

static void S_exec(sl_acmd_state*S,uint32_t w0,uint32_t w1,const int16_t*ramp,
                   uint8_t*out80)
{
    int init=(int)((w0>>16)&1); uint32_t st=w1&0xFFFFFF;
    int cnt=(int)sl_acmd_sget(S,0x04); int sl=s_slot(st); int k,g,G;
    int16_t hiL[8],loL[8],hiR[8],loR[8],par[8];
    if(sl<0){ memset(out80,0,80); return; }
    G=(cnt>0)?((cnt+15)/16):1;
    if(CTL==3 && !init && s_valid[sl])
        for(k=0;k<8;k++) s_par[sl][k]=sl_acmd_sget(S,0x10+2*k);   /* bad read src */
    if(init || !s_valid[sl]){
        for(k=0;k<8;k++) par[k]=sl_acmd_sget(S,0x10+2*k);
        { int16_t cv[2]; int ch; cv[0]=sl_acmd_sget(S,0x06); cv[1]=sl_acmd_sget(S,0x08);
          for(ch=0;ch<2;ch++){ int16_t rm=par[ch?4:1]; uint16_t rl=(uint16_t)par[ch?5:2];
            int16_t tg=par[ch?3:0];
            for(k=0;k<8;k++){ int64_t f=(int64_t)(uint16_t)ramp[k];
              int64_t env=((int64_t)cv[ch]<<16)+(((f*(int64_t)rl)>>16)+f*(int64_t)rm);
              int16_t h=cl16(env>>16), l=low_S(env);
              if(rm>0){ uint16_t dd=(uint16_t)((uint16_t)h-(uint16_t)tg);
                        if((int16_t)dd>=0) h=tg; }
              else if(h<tg) h=tg;
              if(ch){ hiR[k]=h; loR[k]=l; } else { hiL[k]=h; loL[k]=l; } } } }
    } else {
        memcpy(hiL,s_hiL[sl],sizeof hiL); memcpy(loL,s_loL[sl],sizeof loL);
        memcpy(hiR,s_hiR[sl],sizeof hiR); memcpy(loR,s_loR[sl],sizeof loR);
        memcpy(par,s_par[sl],sizeof par);
    }
    { int16_t mL=par[1],mR=par[4],tL=par[0],tR=par[3];
      uint16_t lL=(uint16_t)par[2], lR=(uint16_t)par[5];
      /* S's own advance, expressed with an explicit carry test */
      for(k=0;k<8;k++){ int32_t t=(int32_t)(uint16_t)loL[k]+(int32_t)lL;
          loL[k]=(int16_t)(t&0xFFFF); hiL[k]=cl16((int32_t)hiL[k]+mL+(t>=65536)); }
      for(g=0; g<G-(init?1:0); g++){
        for(k=0;k<8;k++){ if(mL>0){ uint16_t dd=(uint16_t)((uint16_t)hiL[k]-(uint16_t)tL);
                                    if((int16_t)dd>=0) hiL[k]=tL; }
                          else if(hiL[k]<tL) hiL[k]=tL; }
        for(k=0;k<8;k++){ int32_t t=(int32_t)(uint16_t)loR[k]+(int32_t)lR;
            loR[k]=(int16_t)(t&0xFFFF); hiR[k]=cl16((int32_t)hiR[k]+mR+(t>=65536)); }
        for(k=0;k<8;k++){ if(mR>0){ uint16_t dd=(uint16_t)((uint16_t)hiR[k]-(uint16_t)tR);
                                    if((int16_t)dd>=0) hiR[k]=tR; }
                          else if(hiR[k]<tR) hiR[k]=tR; }
        for(k=0;k<8;k++){ int32_t t=(int32_t)(uint16_t)loL[k]+(int32_t)lL;
            loL[k]=(int16_t)(t&0xFFFF); hiL[k]=cl16((int32_t)hiL[k]+mL+(t>=65536)); } } }
    memcpy(s_hiL[sl],hiL,sizeof hiL); memcpy(s_loL[sl],loL,sizeof loL);
    memcpy(s_hiR[sl],hiR,sizeof hiR); memcpy(s_loR[sl],loR,sizeof loR);
    if(CTL==4) for(k=0;k<8;k++) par[k]=sl_acmd_sget(S,0x10+2*k);  /* bad write src */
    memcpy(s_par[sl],par,sizeof par); s_valid[sl]=1;
    for(k=0;k<8;k++){ p16(out80,2*k,hiL[k]); p16(out80,16+2*k,loL[k]);
                      p16(out80,32+2*k,hiR[k]); p16(out80,48+2*k,loR[k]);
                      p16(out80,64+2*k,par[k]); }
}

int main(int argc,char**argv)
{
    const char*path=argc>1?argv[1]:"/tmp/w/all.bin";
    CTL=(argc>2)?atoi(argv[2]):0;
    CTL_ADDR=(argc>3)?(uint32_t)strtoul(argv[3],0,0):0x2da7a0;
    CTL_TASK=(argc>4)?atoi(argv[4]):109; CTL_CMD=(argc>5)?atoi(argv[5]):538;
    static sl_acmd_state ST; int16_t ramp[8]; uint8_t db[0x2c0];
    FILE*f,*d; uint8_t h[20]; uint32_t nf,lo,dsz,fi; uint8_t*bef,*dram;
    static uint32_t wsA[64]; static uint8_t wsD[64][80]; int wsN=0,i;
    long long cmds=0,bytes=0,mis=0,badcmd=0,ninit=0,ncont=0;
    long long firstT=-1,firstC=-1,firstOff=-1;
    d=fopen("bin/aspboot.data.bin","rb"); if(!d){fprintf(stderr,"FATAL\n");return 2;}
    fread(db,1,sizeof db,d); fclose(d);
    for(i=0;i<8;i++) ramp[i]=(int16_t)((db[0xB0+2*i]<<8)|db[0xB0+2*i+1]);
    f=fopen(path,"rb"); fread(h,1,20,f); nf=be32(h+8); lo=be32(h+12); dsz=be32(h+16);
    bef=malloc(dsz); dram=malloc(dsz);
    for(fi=0;fi<nf;fi++){
        uint8_t fh[12]; uint32_t n,k,*w; int ok=1;
        if(fread(fh,1,12,f)!=12)break; n=be32(fh+8); if(n>4096)break;
        w=malloc(8*n);
        for(k=0;k<n;k++){uint8_t c[8];fread(c,1,8,f);w[2*k]=be32(c);w[2*k+1]=be32(c+4);}
        fread(bef,1,dsz,f); fseek(f,dsz,SEEK_CUR);
        memcpy(dram,bef,dsz);
        for(i=0;i<wsN;i++) if(wsA[i]>=lo && wsA[i]+80<=lo+dsz)
            memcpy(dram+(wsA[i]-lo), wsD[i], 80);      /* T's own carry */
        sl_acmd_init(&ST,dram,lo,dsz);
        for(k=0;k<n&&ok;k++){
            uint32_t w0=w[2*k],w1=w[2*k+1],cmd[2]; uint8_t oT[80],oS[80];
            if(((w0>>24)&0xFF)==3){
                uint32_t sa=w1&0xFFFFFF; int q,fnd=-1;
                T_exec(&ST,w0,w1,ramp,lo,dsz,oT);
                S_exec(&ST,w0,w1,ramp,oS);
                if((w0>>16)&1) ninit++; else ncont++;
                cmds++;
                { int bad=0; for(q=0;q<80;q++){ bytes++;
                    if(oT[q]!=oS[q]){ mis++; bad=1;
                      if(firstT<0){firstT=fi;firstC=k;firstOff=q;} } }
                  if(bad) badcmd++; }
                /* controls 1 and 2: corrupt S's persisted vector after the
                 * grounded writer, then let the chronology carry it */
                if((CTL==1||CTL==2) && (int)fi==CTL_TASK && (int)k==CTL_CMD && sa==CTL_ADDR){
                    int q2, sl2=s_slot(sa);
                    if(sl2>=0){ for(q2=0;q2<8;q2++) s_par[sl2][q2]=(int16_t)0x5A5A; CTL_FIRED++; } }
                for(q=0;q<wsN;q++) if(wsA[q]==sa){fnd=q;break;}
                if(fnd<0&&wsN<64){fnd=wsN;wsA[wsN++]=sa;}
                if(fnd>=0) memcpy(wsD[fnd],oT,80);
                continue; }
            cmd[0]=w0;cmd[1]=w1;
            if(sl_acmd_exec(&ST,cmd,1)!=SL_ACMD_OK) ok=0;
        }
        free(w);
    }
    printf("Stage 2/4: TRAVERSAL vs INDEPENDENT SCALAR  [control %d]\n\n",CTL);
    printf("  mixer commands compared : %lld  (init %lld, continuation %lld)\n",cmds,ninit,ncont);
    printf("  state bytes compared    : %lld\n",bytes);
    printf("  MISMATCHING bytes       : %lld\n",mis);
    printf("  commands with mismatch  : %lld\n",badcmd);
    if(firstT>=0) printf("  first divergence        : task %lld cmd %lld offset %lld\n",firstT,firstC,firstOff);
    printf("  ungrounded negative-branch: T %lld, S %lld\n",NEG_T,NEG_S);
    if(CTL==1||CTL==2) printf("  corruption injections applied: %lld\n",CTL_FIRED);
    return mis!=0;
}

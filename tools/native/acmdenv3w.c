/* Stage 3: three worlds compared pairwise over the complete chronology.
 *
 *   L  the ACCEPTED machine-checked literal evaluator (envlit.h), its
 *      descriptors re-verified against the extracted text segment at startup.
 *      Construction and clamps are unmodified; chronological plumbing is added
 *      alongside - its own state store, load, advance and writeback.
 *   T  the traversal formulation, state in DRAM, carried across boundaries.
 *   S  the independent scalar, state in its own address-keyed map.
 *
 * No shared implementation of the continuation state source, the parameter
 * source, the state load and writeback, or reset.
 */
#include "../../src/platform/sl_acmd.h"
#include "envlit.h"
#include <stdlib.h>

static int16_t cl16(int64_t v){ if(v>32767)return 32767; if(v<-32768)return -32768; return (int16_t)v; }
static int64_t a48(int64_t v){ v&=0xFFFFFFFFFFFFLL; if(v&0x800000000000LL)v-=0x1000000000000LL; return v; }
static int16_t g16(const uint8_t*m,uint32_t o){return (int16_t)((m[o]<<8)|m[o+1]);}
static void p16(uint8_t*m,uint32_t o,int v){ m[o]=(uint8_t)((v>>8)&0xFF); m[o+1]=(uint8_t)(v&0xFF); }
static uint32_t be32(const uint8_t*p){return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];}
/* CHRONOLOGICAL STATE TRACE, same record order as the full evaluator's. */
static FILE *CHRON=0; static uint32_t CHRON_hdr[5];

/* A continuation must NEVER silently become an init because a world's state
 * map is invalid - that would let the harness repair a broken carry path
 * unnoticed. Counted, and the run fails loudly. */
long long MISSING_L=0, MISSING_S=0, MISSING_T=0;
/* The steady-state loop stores the LEFT pair INSIDE the loop body
 * ("SQV v20@s7+0, v21@s7+16", blob 0xc50..0xd18) while the right pair is
 * stored only after the loop exits. The saved left state is therefore the
 * value after that iteration's clamp - NOT the value after the trailing left
 * advance that closes the iteration. World L saved the post-advance value.
 * LSTORE_OLD is a NEGATIVE-CONTROL mask restoring the old post-advance
 * store per world: 1=L, 2=S, 4=T. Each world is repaired in its own
 * idiom - no shared implementation of the store point. */
static int LSTORE_OLD=0;
static int FORCE_MISSING=0;   /* constructed control: must fail loudly */

/* ---- world L: literal construction + literal advance, own state store ---- */
#define LMAX 64
static uint32_t l_addr[LMAX]; static int16_t l_st[LMAX][40]; static int l_valid[LMAX], l_n=0;
static int l_slot(uint32_t a){ int i; for(i=0;i<l_n;i++) if(l_addr[i]==a) return i;
    if(l_n<LMAX){ l_addr[l_n]=a; l_valid[l_n]=0; return l_n++; } return -1; }

/* the advance, expressed with the literal carry/clear discipline the accepted
 * evaluator uses for its flag-bearing forms */
static void L_adv(Actx*c,int16_t*hi,int16_t*lo,int16_t rm,uint16_t rl)
{
    int k; c->vco_lo=0;
    for(k=0;k<8;k++){ uint32_t s=(uint32_t)(uint16_t)lo[k]+(uint32_t)rl;
        if(s>>16) c->vco_lo|=(uint16_t)(1u<<k);
        lo[k]=(int16_t)(s&0xFFFF); }
    for(k=0;k<8;k++) hi[k]=cl16((int32_t)hi[k]+(int32_t)rm+(int)((c->vco_lo>>k)&1));
    c->vco_lo=0; c->vco_hi=0; c->vce=0;
}
static void L_exec(sl_acmd_state*S,uint32_t w0,uint32_t w1,const int16_t*ramp,uint8_t*out80)
{
    int init=(int)((w0>>16)&1); uint32_t st=w1&0xFFFFFF; int sl=l_slot(st);
    int cnt=(int)sl_acmd_sget(S,0x04); int k,g,G; Actx c;
    int16_t hiL[8],loL[8],hiR[8],loR[8],par[8];
    if(sl<0){ memset(out80,0,80); return; }
    G=(cnt>0)?((cnt+15)/16):1;
    memset(&c,0,sizeof c);
    for(k=0;k<8;k++){ c.r[31][k]=1; c.r[30][k]=ramp[k]; }
    if(!init && !l_valid[sl]){ MISSING_L++;
        fprintf(stderr,"FATAL: world L continuation at %#08x has no carried state\n",st);
        exit(6); }
    if(init){
        for(k=0;k<8;k++) par[k]=sl_acmd_sget(S,0x10+2*k);
        for(k=0;k<8;k++) c.r[24][k]=par[k];
        c.r[20][7]=sl_acmd_sget(S,0x06);
        A_run(&c, CHAIN_L, (int)(sizeof CHAIN_L/sizeof *CHAIN_L));
        if(par[1] > 0) A_clip(&c,20,20,par[0]); else A_compare(&c,20,20,par[0]);
        memcpy(hiL,c.r[20],sizeof hiL); memcpy(loL,c.r[21],sizeof loL);
        c.r[18][7]=sl_acmd_sget(S,0x08);
        A_run(&c, CHAIN_R, (int)(sizeof CHAIN_R/sizeof *CHAIN_R));
        if(par[4] > 0) A_clip(&c,18,18,par[3]); else A_compare(&c,18,18,par[3]);
        memcpy(hiR,c.r[18],sizeof hiR); memcpy(loR,c.r[19],sizeof loR);
    } else {
        for(k=0;k<8;k++){ hiL[k]=l_st[sl][k]; loL[k]=l_st[sl][8+k];
                          hiR[k]=l_st[sl][16+k]; loR[k]=l_st[sl][24+k];
                          par[k]=l_st[sl][32+k]; }
    }
    { int16_t tL=par[0],mL=par[1],tR=par[3],mR=par[4];
      int16_t sHiL[8],sLoL[8]; int stored=0;
      uint16_t lL=(uint16_t)par[2], lR=(uint16_t)par[5];
      L_adv(&c,hiL,loL,mL,lL);
      for(g=0; g<G-(init?1:0); g++){
        memcpy(c.r[20],hiL,sizeof hiL);
        if(mL>0) A_clip(&c,20,20,tL); else A_compare(&c,20,20,tL);
        memcpy(hiL,c.r[20],sizeof hiL);
        /* the in-loop SQV of v20/v21 */
        memcpy(sHiL,hiL,sizeof sHiL); memcpy(sLoL,loL,sizeof sLoL); stored=1;
        L_adv(&c,hiR,loR,mR,lR);
        memcpy(c.r[18],hiR,sizeof hiR);
        if(mR>0) A_clip(&c,18,18,tR); else A_compare(&c,18,18,tR);
        memcpy(hiR,c.r[18],sizeof hiR);
        L_adv(&c,hiL,loL,mL,lL); }
      if(!(LSTORE_OLD&1) && stored){ memcpy(hiL,sHiL,sizeof hiL);
                                 memcpy(loL,sLoL,sizeof loL); } }
    for(k=0;k<8;k++){ l_st[sl][k]=hiL[k]; l_st[sl][8+k]=loL[k];
                      l_st[sl][16+k]=hiR[k]; l_st[sl][24+k]=loR[k];
                      l_st[sl][32+k]=par[k]; }
    l_valid[sl]=1;
    for(k=0;k<8;k++){ p16(out80,2*k,hiL[k]); p16(out80,16+2*k,loL[k]);
                      p16(out80,32+2*k,hiR[k]); p16(out80,48+2*k,loR[k]);
                      p16(out80,64+2*k,par[k]); }
}
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
    { EnvT Lsv; int sv=0;
      for(g=0; g<G-(init?1:0); g++){ T_clamp(&L,tL,mL);
        Lsv=L; sv=1;                       /* the in-loop store of v20/v21 */
        T_adv(&R,mR,lR); T_clamp(&R,tR,mR); T_adv(&L,mL,lL); }
      if(!(LSTORE_OLD&4) && sv) L=Lsv; }
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
/* NAMED FIXTURES, one per control. A shared default let the reset control
 * silently borrow the PROPAGATION control's edge, so it never tested reset
 * suppression at all and its "firing" was a pass reported for a negative
 * control that must stay silent. Each fixture is compiled in, named, and
 * PRINTED at startup; there are no positional overrides to borrow from.
 * Both edges were read out of the command chronology, not assumed:
 *   propagation  task 109 cmd 538 CONT -> next access task 110 cmd 14  CONT
 *   reset        task 183 cmd 184 CONT -> next access task 184 cmd 276 INIT
 *   missing      the task 110 cmd 14 continuation consumer itself
 */
typedef struct { const char*name; uint32_t addr; int task, cmd; } Fixture;
static const Fixture FX_PROP =
    {"propagation - corrupt at the task 109 -> 110 CONTINUATION edge "
     "(inject after task 109 cmd 538; next access task 110 cmd 14, a continuation)",
     0x2da7a0, 109, 538};
static const Fixture FX_RESET =
    {"reset - corrupt at the task 183 -> 184 INIT edge "
     "(inject after task 183 cmd 184; next access task 184 cmd 276, an A_INIT)",
     0x2da7a0, 183, 184};
static const Fixture FX_MISS =
    {"missing-state - invalidate the carried state at the task 110 cmd 14 "
     "continuation consumer",
     0x2da7a0, 110, 14};
static int CTL=0; static uint32_t CTL_ADDR=0; static int CTL_TASK=-1, CTL_CMD=-1;
long long CTL_FIRED=0;
/* A continuation must NEVER silently become an init because a world's state
 * map is invalid - that would let the harness repair a broken carry path
 * without anyone noticing. Counted and failed loudly instead. */
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
    if(!init && !s_valid[sl]){ MISSING_S++;
        fprintf(stderr,"FATAL: world S continuation at %#08x has no carried state\n",st);
        exit(6); }
    if(init){
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
      int16_t svHiL[8],svLoL[8]; int svS=0;
      uint16_t lL=(uint16_t)par[2], lR=(uint16_t)par[5];
      /* S's own advance, expressed with an explicit carry test */
      for(k=0;k<8;k++){ int32_t t=(int32_t)(uint16_t)loL[k]+(int32_t)lL;
          loL[k]=(int16_t)(t&0xFFFF); hiL[k]=cl16((int32_t)hiL[k]+mL+(t>=65536)); }
      for(g=0; g<G-(init?1:0); g++){
        for(k=0;k<8;k++){ if(mL>0){ uint16_t dd=(uint16_t)((uint16_t)hiL[k]-(uint16_t)tL);
                                    if((int16_t)dd>=0) hiL[k]=tL; }
                          else if(hiL[k]<tL) hiL[k]=tL; }
        for(k=0;k<8;k++){ svHiL[k]=hiL[k]; svLoL[k]=loL[k]; } svS=1;
        for(k=0;k<8;k++){ int32_t t=(int32_t)(uint16_t)loR[k]+(int32_t)lR;
            loR[k]=(int16_t)(t&0xFFFF); hiR[k]=cl16((int32_t)hiR[k]+mR+(t>=65536)); }
        for(k=0;k<8;k++){ if(mR>0){ uint16_t dd=(uint16_t)((uint16_t)hiR[k]-(uint16_t)tR);
                                    if((int16_t)dd>=0) hiR[k]=tR; }
                          else if(hiR[k]<tR) hiR[k]=tR; }
        for(k=0;k<8;k++){ int32_t t=(int32_t)(uint16_t)loL[k]+(int32_t)lL;
            loL[k]=(int16_t)(t&0xFFFF); hiL[k]=cl16((int32_t)hiL[k]+mL+(t>=65536)); } }
      if(!(LSTORE_OLD&2) && svS){ for(k=0;k<8;k++){ hiL[k]=svHiL[k]; loL[k]=svLoL[k]; } } }
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
    static sl_acmd_state ST; int16_t ramp[8]; uint8_t db[0x2c0];
    FILE*f,*d; uint8_t h[20]; uint32_t nf,lo,dsz,fi; uint8_t*bef,*dram;
    static uint32_t wsA[64]; static uint8_t wsD[64][80]; int wsN=0,i;
    long long cmds=0,ninit=0,ncont=0;
    long long mLT=0,mLS=0,mTS=0, bytes=0;
    long long fLT=-1,fLS=-1,fTS=-1;
    long long desc_checked=0, desc_bad=0;
    long long xb=0, xmL=0, xmS=0, xmT=0, xedges=0;      /* cross-task incoming state */
    static uint32_t lastTask[64]; static uint32_t lastAddr[64]; int lastN=0;
    CTL=(argc>2)?atoi(argv[2]):0;
    FORCE_MISSING=(CTL>=9)?(CTL-8):0;   /* 9=L, 10=S, 11=T */
    { const Fixture *fx=0;
      if(CTL==1) fx=&FX_PROP;
      else if(CTL==2) fx=&FX_RESET;
      else if(FORCE_MISSING) fx=&FX_MISS;
      if(fx){ CTL_ADDR=fx->addr; CTL_TASK=fx->task; CTL_CMD=fx->cmd;
              printf("FIXTURE for control %d: %s\n   state %#08x, task %d, command %d\n\n",
                     CTL,fx->name,CTL_ADDR,CTL_TASK,CTL_CMD); }
      else if(CTL) printf("control %d uses no fixture (it applies at every "
                          "continuation)\n\n",CTL); }

    /* the literal evaluator's guarantee: its descriptors must still match the
     * extracted text segment, or it is not the accepted evaluator */
    { FILE*t=fopen("bin/aspboot.text.bin","rb"); static uint8_t tx[0x1000]; size_t txn;
      if(!t){ fprintf(stderr,"FATAL: text segment missing\n"); return 2; }
      txn=fread(tx,1,sizeof tx,t); fclose(t);
      if(!verify_chain(tx,txn,CHAIN_L,(int)(sizeof CHAIN_L/sizeof *CHAIN_L),"CHAIN_L",&desc_checked,&desc_bad) ||
         !verify_chain(tx,txn,CHAIN_R,(int)(sizeof CHAIN_R/sizeof *CHAIN_R),"CHAIN_R",&desc_checked,&desc_bad)){
          fprintf(stderr,"FATAL: literal descriptors do not match the source instructions\n"); return 2; }
      printf("literal descriptors verified against the text segment: %lld checked, %lld bad\n\n",
             desc_checked,desc_bad); }

    d=fopen("bin/aspboot.data.bin","rb"); if(!d){fprintf(stderr,"FATAL\n");return 2;}
    fread(db,1,sizeof db,d); fclose(d);
    for(i=0;i<8;i++) ramp[i]=(int16_t)((db[0xB0+2*i]<<8)|db[0xB0+2*i+1]);
    { const char*lo2=getenv("LSTORE_OLD"); if(lo2) LSTORE_OLD=atoi(lo2); }
    if(LSTORE_OLD) printf("NEGATIVE CONTROL mask %d: post-advance store restored in%s%s%s\n\n",
        LSTORE_OLD,(LSTORE_OLD&1)?" L":"",(LSTORE_OLD&2)?" S":"",(LSTORE_OLD&4)?" T":"");
    { const char*cp=getenv("CHRON"); if(cp){ CHRON=fopen(cp,"wb");
        if(!CHRON){fprintf(stderr,"FATAL: cannot open CHRON\n");return 2;} } }
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
            memcpy(dram+(wsA[i]-lo), wsD[i], 80);
        sl_acmd_init(&ST,dram,lo,dsz);
        for(k=0;k<n&&ok;k++){
            uint32_t w0=w[2*k],w1=w[2*k+1],cmd[2]; uint8_t oT[80],oS[80],oL[80];
            if(((w0>>24)&0xFF)==3){
                uint32_t sa=w1&0xFFFFFF; int q,fnd=-1;
                if(FORCE_MISSING && (int)fi==CTL_TASK && (int)k==CTL_CMD){
                    int q3;
                    if(FORCE_MISSING==1){ q3=l_slot(sa); if(q3>=0) l_valid[q3]=0; }
                    if(FORCE_MISSING==2){ q3=s_slot(sa); if(q3>=0) s_valid[q3]=0; }
                    if(FORCE_MISSING==3){ int q8; for(q8=0;q8<wsN;q8++) if(wsA[q8]==sa){
                        wsA[q8]=wsA[wsN-1]; wsN--; break; } } }
                /* DIRECT boundary oracle: at a cross-task consumer, compare
                 * each world's CARRIED 80 bytes against this task's cartridge
                 * before-image, BEFORE executing. This replaces the weaker
                 * inference from equal final outcomes. */
                if(!((w0>>16)&1) && sa>=lo && sa+80<=lo+dsz){
                    int q5,prevT=-1;
                    for(q5=0;q5<lastN;q5++) if(lastAddr[q5]==sa){prevT=(int)lastTask[q5];break;}
                    if(prevT>=0 && prevT!=(int)fi){
                        const uint8_t*cb=bef+(sa-lo);
                        int q6,slL=l_slot(sa),slS=s_slot(sa);
                        uint8_t cl[80],cs[80];
                        for(q6=0;q6<8;q6++){
                            p16(cl,2*q6,l_st[slL][q6]);       p16(cl,16+2*q6,l_st[slL][8+q6]);
                            p16(cl,32+2*q6,l_st[slL][16+q6]); p16(cl,48+2*q6,l_st[slL][24+q6]);
                            p16(cl,64+2*q6,l_st[slL][32+q6]);
                            p16(cs,2*q6,s_hiL[slS][q6]);      p16(cs,16+2*q6,s_loL[slS][q6]);
                            p16(cs,32+2*q6,s_hiR[slS][q6]);   p16(cs,48+2*q6,s_loR[slS][q6]);
                            p16(cs,64+2*q6,s_par[slS][q6]); }
                        for(q6=0;q6<80;q6++){ xb++;
                            if(cl[q6]!=cb[q6]) xmL++;
                            if(cs[q6]!=cb[q6]) xmS++;
                            if(dram[sa-lo+q6]!=cb[q6]) xmT++; }
                        if(xedges<4 && (xmL||xmT)){
                            int z,d1=0; for(z=0;z<80;z++) if(dram[sa-lo+z]!=cb[z]) d1++;
                            printf("  DIAG edge task %u cmd %u addr %#08x prevTask %d: "
                                   "T-vs-before %d of 80\n",fi,k,sa,prevT,d1); }
                        xedges++; } }
                if(CHRON){ CHRON_hdr[0]=fi; CHRON_hdr[1]=k;
                    CHRON_hdr[2]=(w0>>16)&1;
                    CHRON_hdr[3]=(uint32_t)(uint16_t)sl_acmd_sget(&ST,0x04);
                    CHRON_hdr[4]=sa; }
                L_exec(&ST,w0,w1,ramp,oL);
                if(!((w0>>16)&1)){ int q4,seen=0;
                    for(q4=0;q4<wsN;q4++) if(wsA[q4]==sa){seen=1;break;}
                    if(!seen){ MISSING_T++;
                        fprintf(stderr,"FATAL: world T continuation at %#08x has no carried state\n",sa);
                        exit(6); } }
                T_exec(&ST,w0,w1,ramp,lo,dsz,oT);
                S_exec(&ST,w0,w1,ramp,oS);
                if((w0>>16)&1) ninit++; else ncont++;
                cmds++;
                if(CHRON){ uint8_t rec[260]; memcpy(rec,CHRON_hdr,20);
                    memcpy(rec+20,oL,80); memcpy(rec+100,oS,80);
                    memcpy(rec+180,oT,80); fwrite(rec,1,260,CHRON); }
                for(q=0;q<80;q++){ bytes++;
                    if(oL[q]!=oT[q]){ mLT++; if(fLT<0) fLT=(long long)fi; }
                    if(oL[q]!=oS[q]){ mLS++; if(fLS<0) fLS=(long long)fi; }
                    if(oT[q]!=oS[q]){ mTS++; if(fTS<0) fTS=(long long)fi; } }
                if((CTL==1||CTL==2) && (int)fi==CTL_TASK && (int)k==CTL_CMD && sa==CTL_ADDR){
                    int q2, sl2=s_slot(sa);
                    if(sl2>=0){ for(q2=0;q2<8;q2++) s_par[sl2][q2]=(int16_t)0x5A5A; CTL_FIRED++; } }
                { int q7,f7=-1; for(q7=0;q7<lastN;q7++) if(lastAddr[q7]==sa){f7=q7;break;}
                  if(f7<0&&lastN<64){f7=lastN;lastAddr[lastN++]=sa;}
                  if(f7>=0) lastTask[f7]=fi; }
                for(q=0;q<wsN;q++) if(wsA[q]==sa){fnd=q;break;}
                if(fnd<0&&wsN<64){fnd=wsN;wsA[wsN++]=sa;}
                if(fnd>=0) memcpy(wsD[fnd],oT,80);
                continue; }
            cmd[0]=w0;cmd[1]=w1;
            if(sl_acmd_exec(&ST,cmd,1)!=SL_ACMD_OK) ok=0;
        }
        free(w);
    }
    if(CHRON){ fclose(CHRON); CHRON=0; }
    printf("Stage 3: THREE WORLDS pairwise  [control %d]\n\n",CTL);
    printf("  mixer commands : %lld  (init %lld, continuation %lld)\n",cmds,ninit,ncont);
    printf("  bytes compared per pair : %lld\n\n",bytes);
    printf("  LITERAL vs TRAVERSAL : %lld mismatching%s\n",mLT, mLT?"":"  (zero)");
    printf("  LITERAL vs SCALAR    : %lld mismatching%s\n",mLS, mLS?"":"  (zero)");
    printf("  TRAVERSAL vs SCALAR  : %lld mismatching%s\n",mTS, mTS?"":"  (zero)");
    if(mLT||mLS) printf("  first L-divergence task: LT %lld, LS %lld\n",fLT,fLS);
    if(CTL==1||CTL==2){ printf("  corruption injections applied: %lld\n",CTL_FIRED);
        if(CTL_FIRED!=1){ fprintf(stderr,
            "FATAL: control %d expected exactly one injection, applied %lld - "
            "the fixture does not name a real access\n",CTL,CTL_FIRED); return 7; } }
    printf("\n  MISSING-STATE continuations (must be 0): L %lld  S %lld  T %lld\n",
           MISSING_L,MISSING_S,MISSING_T);
    printf("  CROSS-TASK incoming state vs cartridge before-image:\n");
    printf("    cross-task consumers scored %lld, bytes per world %lld\n",xedges,xb);
    printf("    mismatches: L %lld  S %lld  T %lld\n",xmL,xmS,xmT);
    if(!xedges){ fprintf(stderr,"FATAL: boundary oracle scored nothing\n"); return 5; }
    if(MISSING_L||MISSING_S||MISSING_T){ fprintf(stderr,
        "FATAL: a continuation found no carried state - the carry path is broken\n"); return 4; }
    return (mLT||mLS||mTS)?1:0;
}

/* Candidate low-result rule discrimination against the captured task oracle.
 *
 * The 80-byte persistent ENVMIX state holds the envelope LOW words directly -
 * left at +0x10, right at +0x30 - so a candidate that changes a low word is
 * externally observable in the bracketed cartridge after-image without needing
 * any output arithmetic.
 *
 * Each candidate arm carries its OWN recurrence. No production state is used.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

enum { R_RAW=0, R_SAT32=1, R_S16=2 };
static const char *RN[3] = { "raw", "sat32", "s16" };

static int fires(int64_t a){ int64_t t=a>>31; return !(t==0||t==-1); }
static int16_t low_of(int64_t a,int rule){
    if(!fires(a)) return (int16_t)(a&0xFFFF);
    if(rule==R_SAT32) return (a>0)?(int16_t)0xFFFF:(int16_t)0x0000;
    if(rule==R_S16)   return (a>0)?(int16_t)0x7FFF:(int16_t)0x8000;
    return (int16_t)(a&0xFFFF);
}
static int16_t cl16(int64_t v){ if(v>32767)return 32767; if(v<-32768)return -32768; return (int16_t)v; }
static int64_t acc48(int64_t v){ v&=0xFFFFFFFFFFFFLL; if(v&0x800000000000LL) v-=0x1000000000000LL; return v; }

/* construct one channel's eight lanes under a chosen low rule */
static void construct(const int16_t *ramp,int16_t cvol,int16_t ratm,uint16_t ratl,
                      int rule,int16_t *hi,int16_t *lo)
{
    int k;
    for(k=0;k<8;k++){
        uint16_t f=(uint16_t)ramp[k];
        int64_t a=0;
        a=acc48(((int64_t)f*(int64_t)ratl)>>16);
        a=acc48(a+(int64_t)f*(int64_t)ratm);
        a=acc48(a+(((int64_t)1*(int64_t)cvol)<<16));
        hi[k]=cl16(a>>16);
        lo[k]=low_of(a,rule);
    }
}
/* one group advance: unsigned low add giving carry, signed saturating high add */
static void advance(int16_t *hi,int16_t *lo,int16_t ratm,uint16_t ratl)
{
    int k;
    for(k=0;k<8;k++){
        uint32_t s=(uint32_t)(uint16_t)lo[k]+(uint32_t)ratl;
        int carry=(s>>16)&1;
        lo[k]=(int16_t)(s&0xFFFF);
        hi[k]=cl16((int32_t)hi[k]+(int32_t)ratm+carry);
    }
}
static void clamp_ch(int16_t *hi,int16_t tgt,int16_t ratm)
{
    int k;
    for(k=0;k<8;k++){
        if(ratm>0){ uint16_t d=(uint16_t)((uint16_t)hi[k]-(uint16_t)tgt);
                    hi[k]=((int16_t)d>=0)?tgt:hi[k]; }
        else      { hi[k]=(hi[k]>tgt)?hi[k]:tgt; }
    }
}
/* every opcode capable of writing DRAM, with the range it writes */
static int dram_write_range(uint32_t w0,uint32_t w1,uint16_t setcount,
                            uint32_t *a,uint32_t *n)
{
    int op=(int)((w0>>24)&0xFF);
    switch(op){
    case 6:  *a=w1;              *n=setcount;                     return 1; /* SAVEBUFF */
    case 1:  *a=w1&0xFFFFFF;     *n=32;                           return 1; /* ADPCM state */
    case 5:  *a=w1&0xFFFFFF;     *n=16;                           return 1; /* RESAMPLE state */
    case 14: *a=w1&0xFFFFFF;     *n=16;                           return 1; /* POLEF state */
    case 3:  *a=w1&0xFFFFFF;     *n=80;                           return 1; /* ENVMIXER state */
    default: return 0; }
}
static uint32_t be32(const uint8_t*p){return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];}

int main(int argc,char**argv)
{
    const char*path=argc>1?argv[1]:"/tmp/w/all.bin";
    int16_t ramp[8]; uint8_t db[0x2c0];
    FILE*f,*d; uint8_t h[20]; uint32_t nf,lo,dsz,fi; uint8_t *bef,*aft;
    long long candL[3]={0,0,0}, candR[3]={0,0,0};
    long long scored=0, obsL=0, obsR=0, diffBC=0;
    long long posL=0,negL=0,posR=0,negR=0;          /* overflow sign census */
    long long dposL=0,dnegL=0,dposR=0,dnegR=0;      /* on discriminating cases */
    long long disq=0;                                /* bytes disqualified as oracle */
    int ncase=0;
    d=fopen("bin/aspboot.data.bin","rb");
    if(!d){fprintf(stderr,"FATAL: bin/aspboot.data.bin missing\n");return 2;}
    fread(db,1,sizeof db,d); fclose(d);
    { int k; for(k=0;k<8;k++) ramp[k]=(int16_t)((db[0xB0+2*k]<<8)|db[0xB0+2*k+1]); }
    f=fopen(path,"rb"); if(!f){perror(path);return 2;}
    fread(h,1,20,f); nf=be32(h+8); lo=be32(h+12); dsz=be32(h+16);
    bef=malloc(dsz); aft=malloc(dsz);
    for(fi=0;fi<nf;fi++){
        uint8_t fh[12]; uint32_t n,k,*w; uint16_t blk[0x20]; int have[0x20];
        uint32_t lastwr[64]; uint32_t lastaddr[64]; int nlast=0;
        if(fread(fh,1,12,f)!=12)break; n=be32(fh+8); if(n>4096)break;
        w=malloc(8*n);
        for(k=0;k<n;k++){uint8_t c[8];fread(c,1,8,f);w[2*k]=be32(c);w[2*k+1]=be32(c+4);}
        fread(bef,1,dsz,f); fread(aft,1,dsz,f);
        /* last ENVMIXER writer per state address in this task */
        for(k=0;k<n;k++){ uint32_t w0=w[2*k],w1=w[2*k+1];
            if(((w0>>24)&0xFF)==3){ uint32_t a=w1&0xFFFFFF; int q,fnd=-1;
                for(q=0;q<nlast;q++) if(lastaddr[q]==a){fnd=q;break;}
                if(fnd<0&&nlast<64){lastaddr[nlast]=a;lastwr[nlast]=k;nlast++;}
                else if(fnd>=0) lastwr[fnd]=k; } }
        memset(have,0,sizeof have);
        for(k=0;k<n;k++){
            uint32_t w0=w[2*k],w1=w[2*k+1];
            int op=(int)((w0>>24)&0xFF),fl=(int)((w0>>16)&0xFF);
            if(op==8&&!(fl&8)) blk[0x04]=(uint16_t)(w1&0xFFFF),have[0x04]=1;
            else if(op==9){
                uint16_t v=(uint16_t)(w0&0xFFFF),hi2=(uint16_t)((w1>>16)&0xFFFF),lw=(uint16_t)(w1&0xFFFF);
                if(fl&8){blk[0x1C]=v;have[0x1C]=1;blk[0x1E]=lw;have[0x1E]=1;}
                else if(fl&4){int o=(fl&2)?0x06:0x08;blk[o]=v;have[o]=1;}
                else{int o=(fl&2)?0x10:0x16;blk[o]=v;have[o]=1;blk[o+2]=hi2;have[o+2]=1;blk[o+4]=lw;have[o+4]=1;}
            } else if(op==3&&(fl&1)){
                uint32_t st=w1&0xFFFFFF; int q,islast=0,G,r,ch;
                if(!(have[0x04]&&have[0x06]&&have[0x08]&&have[0x10]&&have[0x12]
                     &&have[0x14]&&have[0x16]&&have[0x18]&&have[0x1A])) continue;
                for(q=0;q<nlast;q++) if(lastaddr[q]==st&&lastwr[q]==k) islast=1;
                if(!islast) continue;
                if(st<lo||st+80>lo+dsz) continue;
                G=(blk[0x04]>0)?((blk[0x04]+15)/16):1;
                scored++;
                for(ch=0;ch<2;ch++){
                    int16_t cvol=(int16_t)blk[ch?0x08:0x06];
                    int16_t ratm=(int16_t)blk[ch?0x18:0x12];
                    uint16_t ratl=blk[ch?0x1A:0x14];
                    int16_t tgt =(int16_t)blk[ch?0x16:0x10];
                    uint32_t hoff=ch?0x20:0x00, loff=ch?0x30:0x10;
                    int anyfire=0,kk,later=0; uint32_t q2;
                    int16_t H[8],L[8];
                    for(kk=0;kk<8;kk++){ uint16_t fq=(uint16_t)ramp[kk]; int64_t a;
                        a=acc48(((int64_t)fq*(int64_t)ratl)>>16);
                        a=acc48(a+(int64_t)fq*(int64_t)ratm);
                        a=acc48(a+(((int64_t)1*(int64_t)cvol)<<16));
                        if(fires(a)){ anyfire=1; if(a>0){ if(ch)posR++; else posL++; }
                                      else { if(ch)negR++; else negL++; } } }
                    if(!anyfire) continue;
                    /* BYTE-RANGE last-writer audit over the two oracle ranges */
                    for(q2=k+1;q2<n;q2++){ uint32_t a2,n2;
                        if(dram_write_range(w[2*q2],w[2*q2+1],blk[0x04],&a2,&n2)){
                            if(!(a2+n2<=st+hoff || a2>=st+hoff+16)) later=1;
                            if(!(a2+n2<=st+loff || a2>=st+loff+16)) later=1; } }
                    if(later){ disq++; continue; }
                    if(ch) obsR++; else obsL++;
                    for(r=0;r<3;r++){
                        int g,mism=0;
                        construct(ramp,cvol,ratm,ratl,r,H,L);
                        for(g=0;g<G;g++){ clamp_ch(H,tgt,ratm);
                            if(g<G-1) advance(H,L,ratm,ratl); }
                        for(g=0;g<8;g++){
                            int16_t ch2=(int16_t)((aft[st-lo+hoff+2*g]<<8)|aft[st-lo+hoff+2*g+1]);
                            int16_t cl2=(int16_t)((aft[st-lo+loff+2*g]<<8)|aft[st-lo+loff+2*g+1]);
                            if(ch2!=H[g]||cl2!=L[g]) mism++; }
                        if(!mism){ if(ch) candR[r]++; else candL[r]++; }
                    }
                    { int16_t Hb[8],Lb[8],Hc[8],Lc[8]; int g,gg;
                      /* run each candidate through the SAME group advances that
                       * produce the observed state, so the dump compares like
                       * with like rather than construction-time against final */
                      construct(ramp,cvol,ratm,ratl,R_SAT32,Hb,Lb);
                      for(gg=0;gg<G;gg++){ clamp_ch(Hb,tgt,ratm);
                          if(gg<G-1) advance(Hb,Lb,ratm,ratl); }
                      construct(ramp,cvol,ratm,ratl,R_S16,Hc,Lc);
                      for(gg=0;gg<G;gg++){ clamp_ch(Hc,tgt,ratm);
                          if(gg<G-1) advance(Hc,Lc,ratm,ratl); }
                      for(g=0;g<8;g++) if(Lb[g]!=Lc[g]){ diffBC++;
                        { int64_t a; uint16_t fq=(uint16_t)ramp[g];
                          a=acc48(((int64_t)fq*(int64_t)ratl)>>16);
                          a=acc48(a+(int64_t)fq*(int64_t)ratm);
                          a=acc48(a+(((int64_t)1*(int64_t)cvol)<<16));
                          if(a>0){ if(ch)dposR++; else dposL++; } else { if(ch)dnegR++; else dnegL++; }
                          if(ncase<12){ int16_t cl2=(int16_t)((aft[st-lo+loff+2*g]<<8)|aft[st-lo+loff+2*g+1]);
                            printf("  case %2d task %3u cmd %3u state %#08x %-5s lane %d %s"
                                   "  cart %6d  sat32 %6d  s16 %6d   (final)\n", ++ncase, fi, k, st,
                                   ch?"RIGHT":"LEFT", g, (a>0)?"pos":"neg", cl2, Lb[g], Lc[g]); } } } }
                }
            }
        }
        free(w);
    }
    printf("\nCandidate low-rule discrimination against the cartridge after-image\n\n");
    printf("  last-writer A_INIT commands                : %lld\n",scored);
    printf("  channel oracles disqualified by a later overlapping DRAM writer : %lld\n",disq);
    printf("\n  overflow-sign census over ALL boundary events in these commands:\n");
    printf("    LEFT  positive %lld  negative %lld\n",posL,negL);
    printf("    RIGHT positive %lld  negative %lld\n",posR,negR);
    printf("  on the DISCRIMINATING lanes only:\n");
    printf("    LEFT  positive %lld  negative %lld\n",dposL,dnegL);
    printf("    RIGHT positive %lld  negative %lld\n",dposR,dnegR);
    printf("\n  cartridge matches, per channel (high+low at their own offsets):\n");
    { int r; for(r=0;r<3;r++)
        printf("    %-6s : LEFT %lld/%lld   RIGHT %lld/%lld   COMBINED %lld/%lld\n",
               RN[r],candL[r],obsL,candR[r],obsR,candL[r]+candR[r],obsL+obsR); }
    printf("  discriminating lane low words (sat32 vs s16) : %lld\n",diffBC);
    printf("\n%s\n", (candL[1]+candR[1] > 0)
        ? "sat32 reproduces the observed state on the scored population."
        : "sat32 did NOT reproduce it - do not treat this run as a selection.");
    return 0;
}

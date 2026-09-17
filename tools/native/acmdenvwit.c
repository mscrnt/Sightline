/* ENVMIXER PCM witness: does the per-sample envelope ramp change SCORED
 * destination PCM, or only internal state?
 *
 * DMEM chronology is maintained by executing every accepted opcode through the
 * production interpreter; ENVMIXER itself is not implemented there, so the walk
 * stops at the first qualifying A_INIT command and analyses that one.
 *
 * A witness requires at least TWO nonzero input lanes carrying DIFFERENT
 * envelope gains. Flattening the eight lanes to one group-wide gain must change
 * destination PCM. A difference confined to internal state does not count.
 */
#include "../../src/platform/sl_acmd.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int16_t cl16(int64_t v){ if(v>32767)return 32767; if(v<-32768)return -32768; return (int16_t)v; }
static int64_t acc48(int64_t v){ v&=0xFFFFFFFFFFFFLL; if(v&0x800000000000LL)v-=0x1000000000000LL; return v; }
static int fires(int64_t a){ int64_t t=a>>31; return !(t==0||t==-1); }
static int16_t low_sat32(int64_t a){ if(!fires(a)) return (int16_t)(a&0xFFFF);
    return (a>0)?(int16_t)0xFFFF:(int16_t)0x0000; }
/* the accepted output arithmetic: destination passes through a near-unity
 * multiply and the gained input is accumulated (MIXER's derived form) */
static int16_t out_sample(int16_t dst,int16_t in,int16_t genv)
{
    int64_t acc = (((int64_t)dst*(int64_t)0x7FFF)<<1) + 0x8000;
    acc += ((int64_t)in*(int64_t)genv)<<1;
    return cl16(acc>>16);
}
static int16_t vmulf(int16_t a,int16_t b)
{ int64_t acc=(((int64_t)a*(int64_t)b)<<1)+0x8000; return cl16(acc>>16); }
static uint32_t be32(const uint8_t*p){return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];}
static int16_t dm16(const sl_acmd_state*s,uint32_t o){return (int16_t)((s->dmem[o]<<8)|s->dmem[o+1]);}

int main(int argc,char**argv)
{
    const char*path=argc>1?argv[1]:"/tmp/w/all.bin";
    static sl_acmd_state S; int16_t ramp[8]; uint8_t db[0x2c0];
    FILE*f,*d; uint8_t h[20]; uint32_t nf,lo,dsz,fi; uint8_t*bef,*aft,*dram;
    long long examined=0, rejected_lanes=0;
    d=fopen("bin/aspboot.data.bin","rb");
    if(!d){fprintf(stderr,"FATAL: bin/aspboot.data.bin missing\n");return 2;}
    fread(db,1,sizeof db,d); fclose(d);
    { int k; for(k=0;k<8;k++) ramp[k]=(int16_t)((db[0xB0+2*k]<<8)|db[0xB0+2*k+1]); }
    f=fopen(path,"rb"); if(!f){perror(path);return 2;}
    fread(h,1,20,f); nf=be32(h+8); lo=be32(h+12); dsz=be32(h+16);
    bef=malloc(dsz); aft=malloc(dsz); dram=malloc(dsz);
    for(fi=0;fi<nf;fi++){
        uint8_t fh[12]; uint32_t n,k,*w; uint16_t blk[0x20]; int have[0x20];
        if(fread(fh,1,12,f)!=12)break; n=be32(fh+8); if(n>4096)break;
        w=malloc(8*n);
        for(k=0;k<n;k++){uint8_t c[8];fread(c,1,8,f);w[2*k]=be32(c);w[2*k+1]=be32(c+4);}
        fread(bef,1,dsz,f); fread(aft,1,dsz,f);
        memcpy(dram,bef,dsz);
        sl_acmd_init(&S,dram,lo,dsz);
        memset(have,0,sizeof have);
        for(k=0;k<n;k++){
            uint32_t w0=w[2*k],w1=w[2*k+1],cmd[2];
            int op=(int)((w0>>24)&0xFF),fl=(int)((w0>>16)&0xFF);
            if(op==9){
                uint16_t v=(uint16_t)(w0&0xFFFF),hi=(uint16_t)((w1>>16)&0xFFFF),lw=(uint16_t)(w1&0xFFFF);
                if(fl&8){blk[0x1C]=v;have[0x1C]=1;blk[0x1E]=lw;have[0x1E]=1;}
                else if(fl&4){int o=(fl&2)?0x06:0x08;blk[o]=v;have[o]=1;}
                else{int o=(fl&2)?0x10:0x16;blk[o]=v;have[o]=1;blk[o+2]=hi;have[o+2]=1;blk[o+4]=lw;have[o+4]=1;}
                cmd[0]=w0;cmd[1]=w1; sl_acmd_exec(&S,cmd,1); continue;
            }
            if(op==3){
                if(!(fl&1)) break;                 /* chronology ends at CONTINUE */
                if(!(have[0x06]&&have[0x10]&&have[0x12]&&have[0x14]&&have[0x1C])) break;
                {
                    uint32_t in = sl_acmd_sget(&S,0x00), outb = sl_acmd_sget(&S,0x02);
                    int16_t cvol=(int16_t)blk[0x06], ratm=(int16_t)blk[0x12];
                    uint16_t ratl=blk[0x14]; int16_t tgt=(int16_t)blk[0x10];
                    int16_t dry=(int16_t)blk[0x1C];
                    int16_t H[8],pcm[8],dst[8]; int i,nz=0,distinct=0;
                    examined++;
                    for(i=0;i<8;i++){
                        uint16_t fq=(uint16_t)ramp[i]; int64_t a;
                        a=acc48(((int64_t)fq*(int64_t)ratl)>>16);
                        a=acc48(a+(int64_t)fq*(int64_t)ratm);
                        a=acc48(a+(((int64_t)1*(int64_t)cvol)<<16));
                        H[i]=cl16(a>>16); (void)low_sat32(a);
                        if(ratm>0){uint16_t dd=(uint16_t)((uint16_t)H[i]-(uint16_t)tgt);
                                   H[i]=((int16_t)dd>=0)?tgt:H[i];}
                        else      {H[i]=(H[i]>tgt)?H[i]:tgt;}
                        pcm[i]=dm16(&S,in+2*i); dst[i]=dm16(&S,outb+2*i);
                    }
                    for(i=0;i<8;i++) if(pcm[i]!=0) nz++;
                    for(i=1;i<8;i++) if(H[i]!=H[0]) distinct=1;
                    { int pair=0,x,y;
                      for(x=0;x<8;x++)for(y=x+1;y<8;y++)
                        if(pcm[x]&&pcm[y]&&H[x]!=H[y]) pair=1;
                      if(!(pair&&distinct)){ rejected_lanes++;
                        printf("  reject task %u cmd %u: nonzero-pcm lanes %d, distinct env %s, "
                               "pair-with-different-gain %s\n", fi,k,nz,distinct?"yes":"no",
                               pair?"yes":"no");
                        break; } }
                    printf("PCM WITNESS  task %u  command %u\n",fi,k);
                    printf("  cvol %d  target %d  rate mant %d low %u  dry gain %d\n",
                           cvol,tgt,ratm,ratl,dry);
                    printf("  input DMEM %#05x   destination DMEM %#05x\n\n",in,outb);
                    printf("  sample  envelope  gained   input PCM   dest before   dest NORMAL   dest FLAT\n");
                    { int16_t flatg = vmulf(H[0],dry); int chg=0;
                      for(i=0;i<8;i++){
                        int16_t g=vmulf(H[i],dry);
                        int16_t on=out_sample(dst[i],pcm[i],g);
                        int16_t of=out_sample(dst[i],pcm[i],flatg);
                        if(on!=of) chg++;
                        printf("    %d    %7d  %7d  %9d   %11d   %11d   %9d%s\n",
                               i,H[i],g,pcm[i],dst[i],on,of,(on!=of)?"  <-- differs":"");
                      }
                      printf("\n  nonzero input lanes: %d   distinct envelope lanes: yes\n",nz);
                      printf("  SCORED destination samples changed by flattening: %d of 8\n",chg);
                      printf("\n  %s\n", chg>0
                        ? "WITNESS VALID: flattening the per-sample ramp to one group-wide\n"
                          "    gain changes SCORED destination PCM, not merely internal state."
                        : "NOT A WITNESS: flattening changed no scored sample - reject and\n"
                          "    move to the next candidate.");
                      free(w); fclose(f);
                      printf("\n  candidates examined %lld, rejected for lane criteria %lld\n",
                             examined,rejected_lanes);
                      return chg>0?0:1; }
                }
            }
            cmd[0]=w0;cmd[1]=w1;
            if(sl_acmd_exec(&S,cmd,1)!=SL_ACMD_OK) break;
        }
        free(w);
    }
    printf("no qualifying witness found (examined %lld, rejected %lld)\n",examined,rejected_lanes);
    return 1;
}

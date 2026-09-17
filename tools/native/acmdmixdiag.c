/* Complete symbolic MIXER DAG, with taint kept STRICTLY separate from
 * expression resolution.
 *
 * WHY THIS WAS REWRITTEN AGAIN. The previous version dropped arithmetic taint
 * whenever a MIXER's operands failed to resolve symbolically: it did
 * `dmE[out] = -1; continue;` BEFORE propagating the contamination flags. A path
 * poisoned by POLEF or RESAMPLE could then reach the final writer looking
 * clean and be counted MIXER-only. The accounting proved it exactly - the
 * validated taint matrix expects 42084 R/P-contaminated changed samples, the
 * tool reported 26120, and the 15964 difference was precisely its
 * "unresolvable" population.
 *
 * THE RULE THIS FILE NOW OBEYS: an unknown expression must NEVER erase known
 * taint. Taint is unioned at every MIXER unconditionally; symbolic resolution
 * is a separate, purely additive question.
 *
 * Expression: BEFORE(addr) | ZERO | MIX(gain, src, dst, cmd).
 * eid: -1 unresolved; <= -2 BEFORE leaf at byte ((-eid-2)*2); >= 0 pool index.
 * Unknown REASON is tracked distinctly from the fact of being unknown.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include "../../src/platform/sl_acmd.h"

#define M_T 1
#define R_T 2
#define P_T 4
#define V_T 8
#define I_T 16
#define ND_ZERO 0
#define ND_MIX  1
#define MAXN 400000

enum { U_RESOLVED=0, U_UNINIT, U_POLEF, U_RESAMPLE, U_VOICE, U_PROP };
static const char *UN[6]={"resolved","uninitialised at task entry",
    "POLEF-contaminated","RESAMPLE-contaminated","voice-contaminated",
    "propagated unknown"};

typedef struct { int type; int gain, src, dst, cmd; int nsrc, ndst; } Node;
static Node *pool; static int npool;
static const uint8_t *g_before;

static int16_t g16(const uint8_t*b,uint32_t o){return (int16_t)((b[o]<<8)|b[o+1]);}
static int32_t sat(int32_t v){ if(v>32767)v=32767; if(v<-32768)v=-32768; return v; }
static int32_t r_asr   (int32_t s,int32_t g,int32_t d){ return sat(d+((s*g)>>15)); }
static int32_t r_trunc0(int32_t s,int32_t g,int32_t d){ return sat(d+(s*g)/32768); }
static int32_t r_bias  (int32_t s,int32_t g,int32_t d){ return sat(d+((s*g+0x4000)>>15)); }
static int32_t r_symm  (int32_t s,int32_t g,int32_t d){ int32_t p=s*g;
    return sat(d+((p<0)?-((-p+0x4000)>>15):((p+0x4000)>>15))); }
static int32_t r_wrap  (int32_t s,int32_t g,int32_t d){ return (int16_t)(d+((s*g)>>15)); }
/* DERIVED from the cartridge aspMain MIXER handler, not fitted:
 *   VMULF vDst, vDst, v31[e6]   with v31[6] = 0x7FFF
 *   VMACF vDst, vSrc, v30[e0]   with v30[0] = the command's signed gain
 * VMULF SETS the 48-bit accumulator to (a*b)<<1 + 0x8000 (its rounding
 * constant); VMACF ADDS (a*b)<<1 with no further constant; the written result
 * is clamp_s16(acc >> 16). Note 0x7FFF is NOT exact unity - that near-miss is
 * intrinsic to the hardware op, not a tuned constant. */
static int32_t r_rsp(int32_t s,int32_t g,int32_t d){
    long long acc = ((long long)d * 0x7FFF) << 1;
    acc += 0x8000;
    acc += ((long long)s * g) << 1;
    long long v = acc >> 16;
    if(v > 32767) v = 32767;
    if(v < -32768) v = -32768;
    return (int32_t)v; }
static int32_t r_rsp(int32_t,int32_t,int32_t);
typedef struct { const char*n; int32_t(*f)(int32_t,int32_t,int32_t); long long b2,b4; } Rule;

static int32_t ev(int eid, int32_t(*rule)(int32_t,int32_t,int32_t)){
    if(eid==-1) return 0;
    if(eid<=-2) return g16(g_before,(uint32_t)((-eid-2)*2));
    if(pool[eid].type==ND_ZERO) return 0;
    { Node*n=&pool[eid];
      return rule(ev(n->src,rule),n->gain,ev(n->dst,rule)); }
}
static void nodes(int eid,int *seen,int *cnt){
    if(eid<0||pool[eid].type==ND_ZERO||seen[eid]) return;
    seen[eid]=1; (*cnt)++;
    nodes(pool[eid].src,seen,cnt); nodes(pool[eid].dst,seen,cnt);
}
/* prefer a specific contamination over a generic unknown */
static int worse(int a,int b){
    int rk[6]={0,2,4,4,4,1};
    return rk[b]>rk[a]?b:a;
}

static uint32_t be32(const uint8_t*p){return ((uint32_t)p[0]<<24)|(p[1]<<16)|(p[2]<<8)|p[3];}

int main(int argc,char**argv){
    const char*path=argc>1?argv[1]:"/tmp/w/fixed.bin";
    int brk=argc>2?atoi(argv[2]):0;
    FILE*f=fopen(path,"rb"); uint8_t hdr[20];
    uint32_t nf,lo,dsz,fi;
    uint8_t *before,*after,*work; uint32_t*words;
    static int32_t dmE[0x1000]; static uint8_t dmH[0x1000],dmF[0x1000],dmU[0x1000];
    int32_t *drE; uint8_t *drH,*drF,*drU; int32_t *lastw; int *seen;
    Rule R[]={{"asr>>15",r_asr,0,0},{"trunc->0",r_trunc0,0,0},
              {"+0x4000 asr",r_bias,0,0},{"symmetric",r_symm,0,0},{"asr wrap",r_wrap,0,0},{"RSP VMULF+VMACF",r_rsp,0,0}};
    int NR=sizeof R/sizeof*R;
    long long chg=0,mo=0,rpc=0,resolv=0,unres=0,ndist[16],ureason[6],opreason[6];
    /* Control 1: full-node symbolic/native mirror, using the CURRENT committed
     * sl_acmd MIXER semantics on both sides. Both are meant to express the same
     * thing, so any nonzero count invalidates the DAG model. */
    long long mir_src=0,mir_dst=0,mir_post=0,mir_n=0;
    /* Control 2: ABI sizing audit */
    long long al_mix_ok=0,al_mix_bad=0,al_o16_ok=0,al_o16_bad=0;
    /* Control 3: alias relationship census, per command and per sample */
    long long ac_cmd[4],ac_smp[4]; 
    long long conv=0,reuse=0,ilv=0,alias_nodes=0;
    memset(ndist,0,sizeof ndist); memset(ureason,0,sizeof ureason); memset(opreason,0,sizeof opreason);
    memset(ac_cmd,0,sizeof ac_cmd); memset(ac_smp,0,sizeof ac_smp);

    if(!f){fprintf(stderr,"open %s\n",path);return 2;}
    fread(hdr,1,20,f); nf=be32(hdr+8); lo=be32(hdr+12); dsz=be32(hdr+16);
    before=malloc(dsz);after=malloc(dsz);work=malloc(dsz);words=malloc(4096*8);
    drE=malloc(dsz*4);drH=malloc(dsz);drF=malloc(dsz);drU=malloc(dsz);lastw=malloc(dsz*4);
    pool=malloc(sizeof(Node)*MAXN); seen=malloc(sizeof(int)*MAXN);
    g_before=before;

    for(fi=0;fi<nf;fi++){
        uint8_t fh[12]; uint32_t af,n,k,si=0,so=0,sc=0,q; sl_acmd_state st;
        if(fread(fh,1,12,f)!=12)break; n=be32(fh+8); af=be32(fh); if(n>4096)break;
        for(k=0;k<n;k++){uint8_t cw[8];fread(cw,1,8,f);words[2*k]=be32(cw);words[2*k+1]=be32(cw+4);}
        fread(before,1,dsz,f); fread(after,1,dsz,f);
        npool=1; pool[0].type=ND_ZERO;
        for(q=0;q<dsz;q++){drE[q]=-(int32_t)(2+q/2);drH[q]=q&1;drF[q]=0;drU[q]=U_RESOLVED;lastw[q]=-1;}
        for(q=0;q<0x1000;q++){dmE[q]=-1;dmH[q]=0;dmF[q]=0;dmU[q]=U_UNINIT;}
        memcpy(work,before,dsz); sl_acmd_init(&st,work,lo,dsz); si=so=sc=0;

        for(k=0;k<n;k++){
            uint32_t w0=words[2*k],w1=words[2*k+1]; int op=(int)((w0>>24)&0xFF); uint32_t j;
            static uint32_t lane_off[4096]; static int lane_id[4096]; int nlane=0;
            if(op==8){si=w0&0xFFFF;so=(w1>>16)&0xFFFF;sc=w1&0xFFFF;}
            else if(op==12){
                uint32_t i=(w1>>16)&0xFFFF,o=w1&0xFFFF; int g=(int16_t)(w0&0xFFFF);
                { int rel; 
                  if(i==o) rel=1;
                  else if(i+sc<=o||o+sc<=i) rel=0;
                  else rel=(i<o)?2:3;
                  ac_cmd[rel]++; ac_smp[rel]+= (sc/2);
                  if(sc%32==0) al_mix_ok++; else al_mix_bad++; }
                if(i+sc<=0x1000&&o+sc<=0x1000)
                  for(j=0;j+1<sc;j+=2){
                    int se=dmE[i+j],de=dmE[o+j],id;
                    int sok=(se!=-1&&dmE[i+j+1]==se&&dmH[i+j]==0&&dmH[i+j+1]==1);
                    int dok=(de!=-1&&dmE[o+j+1]==de&&dmH[o+j]==0&&dmH[o+j+1]==1);
                    uint8_t nfl=(uint8_t)(dmF[i+j]|dmF[o+j]|M_T);
                    if(sok&&dok){    /* Control 1, PRE side */
                        mir_n++;
                        if(ev(se,r_rsp)!=g16(st.dmem,i+j)) mir_src++;
                        if(ev(de,r_rsp)!=g16(st.dmem,o+j)) mir_dst++;
                    }
                    /* operand-read census, by REASON, never conflated */
                    opreason[sok?U_RESOLVED:dmU[i+j]]++;
                    opreason[dok?U_RESOLVED:dmU[o+j]]++;
                    /* TAINT FIRST, UNCONDITIONALLY - an unknown expression must
                     * never erase known contamination. */
                    dmF[o+j]=dmF[o+j+1]=nfl;
                    if(sok&&dok&&npool<MAXN){
                        id=npool++;
                        pool[id].type=ND_MIX;pool[id].gain=g;pool[id].src=se;pool[id].dst=de;
                        pool[id].cmd=(int)k;
                        pool[id].nsrc=g16(st.dmem,i+j);pool[id].ndst=g16(st.dmem,o+j);
                        dmE[o+j]=dmE[o+j+1]=id;dmH[o+j]=0;dmH[o+j+1]=1;
                        dmU[o+j]=dmU[o+j+1]=U_RESOLVED;
                        if(nlane<4096){ lane_off[nlane]=o+j; lane_id[nlane]=id; nlane++; }
                        if(se==de) alias_nodes++;
                    } else {
                        int rr=U_PROP;
                        if(!sok) rr=worse(rr,dmU[i+j]);
                        if(!dok) rr=worse(rr,dmU[o+j]);
                        dmE[o+j]=dmE[o+j+1]=-1;
                        dmU[o+j]=dmU[o+j+1]=(uint8_t)rr;
                    }
                  }
            }
            if(op==2||op==10||op==13){
                uint32_t cc = (op==2)? (w1&0xFFFF) : (op==10? (w1&0xFFFF) : sc);
                if(cc%16==0) al_o16_ok++; else al_o16_bad++;
            }
            if(sl_acmd_exec(&st,words+2*k,1)!=SL_ACMD_OK) break;
            if(op==12){ int z; for(z=0;z<nlane;z++)
                if(ev(lane_id[z],r_rsp)!=g16(st.dmem,lane_off[z])) mir_post++; }

            if(op==2){uint32_t d=w0&0xFFFF,c=w1&0xFFFF;
                if(d+c<=0x1000)for(j=0;j<c;j++){dmE[d+j]=ND_ZERO;dmH[d+j]=j&1;dmF[d+j]=0;dmU[d+j]=U_RESOLVED;}}
            else if(op==4){if(w1>=lo&&w1+sc<=lo+dsz&&si+sc<=0x1000)
                for(j=0;j<sc;j++){uint32_t s=w1-lo+j;
                    dmE[si+j]=(brk==1)?-1:drE[s];dmH[si+j]=drH[s];
                    dmF[si+j]=drF[s];dmU[si+j]=(brk==1)?U_PROP:drU[s];}}
            else if(op==6){if(w1>=lo&&w1+sc<=lo+dsz&&so+sc<=0x1000)
                for(j=0;j<sc;j++){uint32_t d=w1-lo+j;
                    drE[d]=(brk==2)?-1:dmE[so+j];drH[d]=dmH[so+j];
                    drF[d]=dmF[so+j];drU[d]=(brk==2)?U_PROP:dmU[so+j];lastw[d]=(int32_t)k;}}
            else if(op==10){uint32_t i=w0&0xFFFF,o=(w1>>16)&0xFFFF,c=w1&0xFFFF;
                if(i+c<=0x1000&&o+c<=0x1000){
                    static int32_t tE[0x1000];static uint8_t tH[0x1000],tF[0x1000],tU[0x1000];
                    memcpy(tE,dmE+i,c*4);memcpy(tH,dmH+i,c);memcpy(tF,dmF+i,c);memcpy(tU,dmU+i,c);
                    if(brk==3){for(j=0;j<c;j++){dmE[o+j]=-1;dmU[o+j]=U_PROP;}}
                    else {memcpy(dmE+o,tE,c*4);memcpy(dmU+o,tU,c);}
                    memcpy(dmH+o,tH,c);memcpy(dmF+o,tF,c);}}
            else if(op==13){uint32_t L=(w1>>16)&0xFFFF,Rr=w1&0xFFFF;
                if(L+sc<=0x1000&&Rr+sc<=0x1000&&so+2*sc<=0x1000)
                  for(j=0;j<sc;j+=2){
                    dmE[so+2*j]=dmE[L+j];dmE[so+2*j+1]=dmE[L+j+1];
                    dmH[so+2*j]=dmH[L+j];dmH[so+2*j+1]=dmH[L+j+1];
                    dmF[so+2*j]=dmF[L+j];dmF[so+2*j+1]=dmF[L+j+1];
                    dmU[so+2*j]=dmU[L+j];dmU[so+2*j+1]=dmU[L+j+1];
                    dmE[so+2*j+2]=dmE[Rr+j];dmE[so+2*j+3]=dmE[Rr+j+1];
                    dmH[so+2*j+2]=dmH[Rr+j];dmH[so+2*j+3]=dmH[Rr+j+1];
                    dmF[so+2*j+2]=dmF[Rr+j];dmF[so+2*j+3]=dmF[Rr+j+1];
                    dmU[so+2*j+2]=dmU[Rr+j];dmU[so+2*j+3]=dmU[Rr+j+1];
                    dmF[so+2*j]|=I_T;dmF[so+2*j+1]|=I_T;
                    dmF[so+2*j+2]|=I_T;dmF[so+2*j+3]|=I_T;}}
            else if(op==5){if(si+sc<=0x1000&&so+sc<=0x1000)
                for(j=0;j<sc;j++){dmF[so+j]=(uint8_t)(dmF[si+j]|R_T);
                    dmE[so+j]=-1;dmU[so+j]=U_RESAMPLE;}}
            else if(op==14){if(so+sc<=0x1000)
                for(j=0;j<sc;j++){dmF[so+j]|=P_T;dmE[so+j]=-1;dmU[so+j]=U_POLEF;}}
            else if(op==1||op==3){if(so+sc<=0x1000)
                for(j=0;j<sc;j++){dmF[so+j]|=V_T;dmE[so+j]=-1;dmU[so+j]=U_VOICE;}}
        }

        for(q=0;q+1<dsz;q+=2){
            int e=drE[q],cnt=0,r,cart,pr[8];
            if(lastw[q]<0||lastw[q]!=lastw[q+1]) continue;
            if(after[q]==before[q]&&after[q+1]==before[q+1]) continue;
            chg++;
            if(drF[q]&(R_T|P_T|V_T)){ rpc++; continue; }
            if(drF[q]&I_T) ilv++;
            if(!(drF[q]&M_T)) continue;
            mo++;
            if(e==-1||drE[q+1]!=e||drH[q]!=0||drH[q+1]!=1){ unres++; ureason[drU[q]]++; continue; }
            memset(seen,0,sizeof(int)*npool); nodes(e,seen,&cnt);
            if(cnt==0){ unres++; ureason[U_PROP]++; continue; }
            ndist[cnt<15?cnt:15]++; resolv++;
            { int z,sh=0,ru=0; static int usecnt[400000];
              for(z=0;z<npool;z++) usecnt[z]=0;
              for(z=0;z<npool;z++) if(seen[z]&&pool[z].type==ND_MIX){
                if(pool[z].src>=0)usecnt[pool[z].src]++;
                if(pool[z].dst>=0)usecnt[pool[z].dst]++; }
              for(z=0;z<npool;z++) if(usecnt[z]>1) ru=1;
              if(ru) reuse++;
              for(z=0;z<npool;z++) if(seen[z]&&pool[z].type==ND_MIX){
                int s2=pool[z].src,d2=pool[z].dst;
                if(s2==d2) sh=1; }
              if(sh) conv++; }
            cart=g16(after,q);
            for(r=0;r<NR;r++){ pr[r]=ev(e,R[r].f);
                if(pr[r]!=cart){ if(cnt==2)R[r].b2++; else R[r].b4++; } }
        }
    }
    fclose(f);
    printf("=== RECONCILED CLASS ACCOUNTING (changed S16 samples) ===\n");
    printf("(1) MIXER-only changed samples        : %lld\n",mo);
    printf("(2) R/P/voice-contaminated changed    : %lld\n",rpc);
    printf("    total changed                     : %lld\n",chg);
    printf("(3) symbolically resolvable MIXER-only: %lld\n",resolv);
    printf("(4) unresolved MIXER-only             : %lld\n",unres);
    for(int i=0;i<6;i++) if(ureason[i]) printf("      %-30s : %lld\n",UN[i],ureason[i]);
    printf("(5) MIXER operand reads by reason (task-entry kept SEPARATE from\n"
           "    arithmetic contamination):\n");
    for(int i=0;i<6;i++) if(opreason[i]) printf("      %-30s : %lld\n",UN[i],opreason[i]);
    printf("\n  node-count distribution:\n");
    for(int i=0;i<16;i++) if(ndist[i]) printf("     %2d nodes : %lld\n",i,ndist[i]);
    printf("\n=== CONTROL 1: FULL-NODE symbolic/native mirror ===\n");
    printf("  MIXER lanes mirrored          : %lld\n",mir_n);
    printf("  pre-src  mismatches           : %lld\n",mir_src);
    printf("  pre-dst  mismatches           : %lld\n",mir_dst);
    printf("  post-MIXER mismatches         : %lld\n",mir_post);
    printf("  -> %s\n",(mir_src||mir_dst||mir_post)?
           "*** MIRROR FAILED - DAG model is wrong, scoreboard NOT admissible ***":
           "mirror EXACT (DAG reproduces the native interpreter node for node)");
    printf("\n=== CONTROL 2: ABI sizing audit ===\n");
    printf("  MIXER counts 32-byte aligned  : %lld ok, %lld NOT\n",al_mix_ok,al_mix_bad);
    printf("  CLEAR/DMEMMOVE/INTERLEAVE 16B : %lld ok, %lld NOT\n",al_o16_ok,al_o16_bad);
    printf("\n=== CONTROL 3: MIXER alias relationship census ===\n");
    { const char*an[4]={"disjoint","exact src==dst","overlap, src before dst","overlap, dst before src"};
      for(int z=0;z<4;z++) printf("  %-26s : %lld commands, %lld samples\n",an[z],ac_cmd[z],ac_smp[z]); }
    printf("  MIX nodes BUILT with src expr == dst expr (exact alias)        : %lld\n",alias_nodes);
    printf("  final DAGs where a node's src and dst are the SAME expression: %lld\n",conv);
    printf("  final DAGs where an expression is consumed by >1 node (reuse) : %lld\n",reuse);
    printf("  MIXER-only changed samples that passed through INTERLEAVE     : %lld\n",ilv);
    printf("\n=== RULE SCOREBOARD (wrong of total) ===\n");
    { long long t2=ndist[2],t4=ndist[4];
      for(int r=0;r<NR;r++)
        printf("  %-12s : 2-node %lld/%lld   4-node %lld/%lld\n",R[r].n,R[r].b2,t2,R[r].b4,t4); }
    return 0;
}

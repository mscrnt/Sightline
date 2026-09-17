/* Voice-path provenance and classification over the full ACMD capture.
 *
 * Purely symbolic: it walks command and state dependencies and never executes
 * ADPCM or ENVMIXER, so it works before those opcodes are implemented.
 *
 * MIXER, POLEF and RESAMPLE are derived and exact, so they no longer
 * contaminate - they propagate provenance but introduce no unvalidated taint.
 * The remaining unvalidated arithmetic is ADPCM and ENVMIXER.
 *
 * CODEBOOK PROVENANCE IS BY EPOCH, NOT BY LABEL. DMEM 0x4C0 is written by
 * LOADADPCM and read by BOTH POLEF and ADPCM, so the region has no intrinsic
 * semantic role - the consumer decides. Each LOADADPCM therefore bumps an
 * epoch, and every later read of that region records the epoch in force. That
 * makes last-writer identity travel with the bytes instead of being guessed
 * from what they look like.
 *
 * STATE PROVENANCE IS SEPARATE FROM SAMPLE PROVENANCE. SETLOOP writes the audio
 * state block at +0x10 and never touches a sample buffer, yet a later ADPCM
 * consumes it. A single generic "voice taint" bit would lose that entirely.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define A_T 1   /* ADPCM output        */
#define E_T 2   /* ENVMIXER output     */
#define CODEBOOK_DMEM 0x4C0
#define CODEBOOK_LEN  0x200

static uint32_t be32(const uint8_t*p){return ((uint32_t)p[0]<<24)|(p[1]<<16)|(p[2]<<8)|p[3];}

/* REAL per-byte provenance for the LOADADPCM destination region. The first
 * version of this file only counted LOADADPCM commands and consumer reads; it
 * recorded nothing about WHICH bytes a consumer actually read, so its "distinct
 * epochs consumed" was really "tasks ending with distinct last epochs". These
 * arrays fix that: every byte of the region carries the epoch, command index
 * and DRAM source that last wrote it, and consumers record the epochs of the
 * exact bytes they touch. The role of an epoch is decided by its CONSUMER, not
 * at load time. */
static int32_t  cb_ep[CODEBOOK_LEN];
static int32_t  cb_cmd[CODEBOOK_LEN];
static uint32_t cb_src[CODEBOOK_LEN];
/* A real DMEM data model, populated by the plumbing opcodes only, so an ADPCM
 * frame header (and therefore its predictor index) can actually be read. No
 * arithmetic is executed here. */
static uint8_t  dmem[0x1000];
/* Mutable DRAM image, initialised from the task's cartridge `before` snapshot.
 * LOADBUFF reads it and SAVEBUFF writes it, so an intra-task
 * SAVEBUFF -> LOADBUFF round trip sees what was saved rather than stale
 * pre-task bytes. Only already-exact plumbing writes here; no unvalidated
 * arithmetic is executed to populate it. */
static uint8_t *dram_value;
static uint8_t *dram_saved;      /* byte was written by a SAVEBUFF this task */
static uint8_t  dmem_fromsaved[0x1000];
static long long hdr_from_saved=0, hdr_total=0;
static long long la_total=0, la_over_saved=0, ev_from_saved_cb=0;
static uint8_t cb_saved[CODEBOOK_LEN];

#define MAXEP 65536
static uint8_t ep_by_polef[MAXEP];
static uint8_t ep_by_adpcm[MAXEP];
static long long mixed_polef=0, mixed_adpcm=0;
/* PER-CONSUMER-EVENT accounting. Distinct-epoch-ID counts and consumer-event
 * counts are different quantities, and subtracting one from the other is
 * meaningless - an earlier entry did exactly that and inferred "40
 * unattributed reads" from 4427 events minus 4387 distinct epochs. These
 * counters make the event side explicit so an unresolved-read figure can
 * actually be stated. */
static long long ev_tot[2], ev_res[2], ev_unres[2], ev_mix[2];   /* 0=POLEF 1=ADPCM */
/* Load-side: which loads are ever consumed, and by which role. */
static uint8_t ld_polef[MAXEP], ld_adpcm[MAXEP];
/* Selected-load control: a serial independent of the epoch id, so exactly ONE
 * load can be made to reuse the preceding epoch. The old selector tested
 * `epoch % brk`, and since epoch starts at 0 the first load matched and the
 * epoch never advanced, freezing EVERY load. */
static long long load_serial=0;

static FILE *evlog=NULL;
static uint32_t ev_task=0, ev_cmd=0; static int ev_pred=-1;
static void note_consumer(int who, uint8_t *tab, uint8_t *ld,
                          uint32_t off, uint32_t len, long long *mixed){
    uint32_t i; int32_t first=-2; int mix=0, any=0, allres=1;
    ev_tot[who]++;
    for(i=0;i<len;i++){
        uint32_t o=off+i; int32_t e;
        if(o>=CODEBOOK_LEN){ allres=0; break; }
        e=cb_ep[o];
        if(e>=0 && e<MAXEP){ tab[e]=1; ld[e]=1; any=1; } else allres=0;
        if(first==-2) first=e; else if(e!=first) mix=1;
    }
    if(mix){ (*mixed)++; ev_mix[who]++; }
    if(allres && any) ev_res[who]++; else ev_unres[who]++;
    { uint32_t z; int sv=0;
      for(z=0;z<len && off+z<CODEBOOK_LEN;z++) if(cb_saved[off+z]) sv=1;
      if(sv) ev_from_saved_cb++; }
    if(evlog){
        /* One record per consumer event: task, command, role, byte range,
         * predictor, and the epoch actually read. Diffing two runs isolates
         * exactly which events a selected-load perturbation moves. */
        fprintf(evlog,"t=%u c=%u role=%s off=%u len=%u pred=%d ep=%d\n",
                ev_task, ev_cmd, who?"ADPCM":"POLEF", off, len, ev_pred,
                (off<CODEBOOK_LEN)?cb_ep[off]:-1);
    }
}

int main(int argc,char**argv){
    const char*path=argc>1?argv[1]:"/tmp/w/all.bin";
    int brk_epoch = argc>2 ? atoi(argv[2]) : 0;   /* control: freeze the epoch */
    FILE*f=fopen(path,"rb"); uint8_t hdr[20];
    uint32_t nf,lo,dsz,fi;
    uint8_t *before,*after,*dt; uint32_t*words;
    static uint8_t dmt[0x1000];
    static int32_t lastw[1<<20];
    long long ncmd[16]; long long tasks_with[16];
    long long cls[4]={0,0,0,0}, chg=0;
    long long epoch=0, cb_reads_polef=0, cb_reads_adpcm=0;
    long long distinct_epochs_used=0, setloop_writes=0, adpcm_loop_reads=0;
    static long long ep_seen[4096]; long long n_ep_seen=0;

    memset(ncmd,0,sizeof ncmd); memset(tasks_with,0,sizeof tasks_with);
    if(!f){fprintf(stderr,"open %s failed\n",path);return 2;}
    if(fread(hdr,1,20,f)!=20||memcmp(hdr,"SLACMD01",8)){fprintf(stderr,"bad magic\n");return 2;}
    nf=be32(hdr+8); lo=be32(hdr+12); dsz=be32(hdr+16);
    if(getenv("SL_EVLOG")) evlog=fopen(getenv("SL_EVLOG"),"w");
    printf("capture: %u tasks, window 0x%06x..0x%06x%s\n",nf,lo,lo+dsz,
           brk_epoch?"   *** CONTROL: epoch frozen ***":"");
    before=malloc(dsz); after=malloc(dsz); dt=malloc(dsz); words=malloc(4096*8);
    dram_value=malloc(dsz); dram_saved=malloc(dsz);

    for(fi=0;fi<nf;fi++){
        uint8_t fh[12]; uint32_t n,k,si=0,so=0,sc=0,q; int seen[16];
        long long cb_epoch_here=-1;
        int state_loop_set=0;      /* has SETLOOP written +0x10 this task */
        if(fread(fh,1,12,f)!=12)break; n=be32(fh+8); if(n>4096)break;
        for(k=0;k<n;k++){uint8_t cw[8];fread(cw,1,8,f);words[2*k]=be32(cw);words[2*k+1]=be32(cw+4);}
        fread(before,1,dsz,f); fread(after,1,dsz,f);
        memset(dmt,0,sizeof dmt); memset(dt,0,dsz);
        memset(dmem,0,sizeof dmem);
        memset(dmem_fromsaved,0,sizeof dmem_fromsaved);
        memcpy(dram_value, before, dsz);
        memset(dram_saved, 0, dsz);
        { uint32_t z; for(z=0;z<CODEBOOK_LEN;z++){cb_ep[z]=-1;cb_cmd[z]=-1;cb_src[z]=0;cb_saved[z]=0;} }
        memset(seen,0,sizeof seen);
        for(q=0;q<dsz;q++) lastw[q]=-1;

        for(k=0;k<n;k++){
            uint32_t w0=words[2*k],w1=words[2*k+1]; int op=(int)((w0>>24)&0xFF); uint32_t j;
            if(op<16){ ncmd[op]++; seen[op]=1; }
            switch(op){
            case 8: si=w0&0xFFFF; so=(w1>>16)&0xFFFF; sc=w1&0xFFFF; break;
            case 2:{uint32_t d=w0&0xFFFF,c=w1&0xFFFF;
                    if(d+c<=0x1000){ memset(dmt+d,0,c); memset(dmem+d,0,c); }}break;
            case 10:{uint32_t i=w0&0xFFFF,o=(w1>>16)&0xFFFF,c=w1&0xFFFF;
                     if(i+c<=0x1000&&o+c<=0x1000){ memmove(dmt+o,dmt+i,c);
                                                   memmove(dmem+o,dmem+i,c);
                                                   memmove(dmem_fromsaved+o,dmem_fromsaved+i,c); }}break;
            case 4: if(w1>=lo&&w1+sc<=lo+dsz&&si+sc<=0x1000){
                        memcpy(dmt+si,dt+(w1-lo),sc);
                        memcpy(dmem+si,dram_value+(w1-lo),sc);
                        memcpy(dmem_fromsaved+si,dram_saved+(w1-lo),sc); } break;
            case 6: if(w1>=lo&&w1+sc<=lo+dsz&&so+sc<=0x1000){memcpy(dt+(w1-lo),dmt+so,sc);
                      memcpy(dram_value+(w1-lo),dmem+so,sc);
                      memset(dram_saved+(w1-lo),1,sc);
                      for(q=0;q<sc;q++) lastw[w1-lo+q]=(int32_t)k;}break;
            case 13:{uint32_t L=(w1>>16)&0xFFFF,R=w1&0xFFFF;
                     /* Real S16 interleave on actual bytes, mirroring the
                      * derived form in sl_acmd.c - the taint-only version left
                      * dmem and dmem_fromsaved stale, so a predictor byte
                      * descending through INTERLEAVE would have been
                      * mis-modelled. */
                     if(L+sc<=0x1000&&R+sc<=0x1000&&so+2*sc<=0x1000)
                       for(j=0;j<sc;j+=2){
                         dmt[so+2*j]=dmt[so+2*j+1]=dmt[L+j];
                         dmt[so+2*j+2]=dmt[so+2*j+3]=dmt[R+j];
                         dmem[so+2*j+0]=dmem[L+j+0]; dmem[so+2*j+1]=dmem[L+j+1];
                         dmem[so+2*j+2]=dmem[R+j+0]; dmem[so+2*j+3]=dmem[R+j+1];
                         dmem_fromsaved[so+2*j+0]=dmem_fromsaved[L+j+0];
                         dmem_fromsaved[so+2*j+1]=dmem_fromsaved[L+j+1];
                         dmem_fromsaved[so+2*j+2]=dmem_fromsaved[R+j+0];
                         dmem_fromsaved[so+2*j+3]=dmem_fromsaved[R+j+1];
                       }}break;
            /* validated arithmetic: propagate, introduce nothing */
            case 5: if(si+sc<=0x1000&&so+sc<=0x1000) memmove(dmt+so,dmt+si,sc); break;
            case 12:{uint32_t i=(w1>>16)&0xFFFF,o=w1&0xFFFF;
                     if(i+sc<=0x1000&&o+sc<=0x1000)
                       for(j=0;j<sc;j++) dmt[o+j]|=dmt[i+j];}break;
            case 14: /* POLEF consumes the 32-byte filter table at the region
                      * base (LQV/LRV span established in its derivation). */
                     cb_reads_polef++;
                     ev_task=fi; ev_cmd=k; ev_pred=-1;
                     note_consumer(0, ep_by_polef, ld_polef, 0, 32, &mixed_polef);
                     break;
            /* codebook epoch: the region's role is decided by its consumer */
            case 11: {   /* LOADADPCM: a new epoch, stamped on every byte it
                          * actually writes. brk_epoch freezes ONE selected
                          * load so the negative control changes real consumer
                          * provenance, not a counter. */
                        uint32_t cnt=w0&0xFFFF, z;
                        int frozen;
                        long long stamp;
                        load_serial++;
                        frozen = (brk_epoch && load_serial==(long long)brk_epoch);
                        /* ALWAYS advance the counter, so later loads keep their
                         * ids; the perturbation instead MIS-STAMPS this one load
                         * with its predecessor's epoch. An earlier version
                         * skipped the increment, which renumbered every
                         * subsequent epoch and made 13474 records differ - that
                         * showed propagation but not ISOLATION. */
                        epoch++;
                        stamp = frozen ? epoch-1 : epoch;
                        cb_epoch_here=stamp;
                        if(cnt>CODEBOOK_LEN) cnt=CODEBOOK_LEN;
                        for(z=0; z<cnt; z++){
                            cb_ep[z]=(int32_t)stamp; cb_cmd[z]=(int32_t)k;
                            cb_src[z]=w1;
                        }
                        la_total++;
                        if(w1>=lo&&w1+cnt<=lo+dsz){
                            uint32_t z2; int ov=0;
                            for(z2=0;z2<cnt;z2++) if(dram_saved[w1-lo+z2]) ov=1;
                            if(ov) la_over_saved++;
                            /* MUTABLE image, not the pre-task snapshot: a
                             * LOADADPCM is a DRAM DMA like any other, and the
                             * codebook path is exactly where stale bytes would
                             * corrupt provenance most. */
                            memcpy(dmem+CODEBOOK_DMEM, dram_value+(w1-lo), cnt);
                            for(z2=0;z2<cnt && z2<CODEBOOK_LEN;z2++)
                                cb_saved[z2]=dram_saved[w1-lo+z2];
                        }
                     } break;
            /* unvalidated voice arithmetic */
            case 1:  /* ADPCM consumes ONE 32-byte predictor entry, selected by
                      * the low nibble of the frame header - which is why the
                      * DMEM data model above exists. */
                     cb_reads_adpcm++;
                     { uint32_t hdr_off = si;
                       uint32_t pred = (hdr_off<0x1000) ? (dmem[hdr_off]&15) : 0;
                       hdr_total++;
                       if(hdr_off<0x1000 && dmem_fromsaved[hdr_off]) hdr_from_saved++;
                       ev_task=fi; ev_cmd=k; ev_pred=(int)pred;
                       note_consumer(1, ep_by_adpcm, ld_adpcm, pred*32, 32, &mixed_adpcm); }
                     if(((w0>>16)&2) && state_loop_set) adpcm_loop_reads++;
                     if(so+sc<=0x1000) for(j=0;j<sc;j++) dmt[so+j]|=A_T; break;
            case 3:  /* ENVMIXER: output carries its INPUT's taint too. An
                      * earlier version only OR-ed in E_T and dropped whatever
                      * the input already carried, which silently erased every
                      * ADPCM->ENVMIXER dependency and made the ADPCM classes
                      * read as empty. Losing taint at an unvalidated opcode is
                      * exactly the failure the controls exist to catch. */
                     if(si+sc<=0x1000&&so+sc<=0x1000)
                       for(j=0;j<sc;j++) dmt[so+j]|=(uint8_t)(dmt[si+j]|E_T);
                     else if(so+sc<=0x1000)
                       for(j=0;j<sc;j++) dmt[so+j]|=E_T;
                     break;
            case 15: setloop_writes++; state_loop_set=1; break;
            case 9:  break;   /* SETVOL: state mutation, decoded next */
            }
        }
        if(cb_epoch_here>=0 && n_ep_seen<4096) ep_seen[n_ep_seen++]=cb_epoch_here;
        for(k=0;k<16;k++) if(seen[k]) tasks_with[k]++;
        for(q=0;q+1<dsz;q+=2){
            if(lastw[q]<0||lastw[q]!=lastw[q+1]) continue;
            if(after[q]==before[q]&&after[q+1]==before[q+1]) continue;
            chg++;
            cls[dt[q]&(A_T|E_T)]++;
        }
    }
    fclose(f);
    { long long i,u=0; for(i=0;i<n_ep_seen;i++){ long long j2,dup=0;
        for(j2=0;j2<i;j2++) if(ep_seen[j2]==ep_seen[i]) dup=1; if(!dup)u++; }
      distinct_epochs_used=u; }

    printf("\n=== OPCODE PRESENCE, recomputed independently ===\n");
    { const char*nm[16]={"SPNOOP","ADPCM","CLEARBUFF","ENVMIXER","LOADBUFF",
        "RESAMPLE","SAVEBUFF","SEGMENT","SETBUFF","SETVOL","DMEMMOVE",
        "LOADADPCM","MIXER","INTERLEAVE","POLEF","SETLOOP"};
      int i; for(i=0;i<16;i++) if(tasks_with[i])
        printf("  %-11s in %4lld/%u tasks   (%lld commands)\n",nm[i],tasks_with[i],nf,ncmd[i]); }

    printf("\n=== FINAL CHANGED S16 SAMPLES BY REMAINING-UNVALIDATED SET ===\n");
    printf("  (MIXER, POLEF, RESAMPLE are exact and introduce no taint)\n");
    printf("  %-28s %lld\n","no unvalidated arithmetic:",cls[0]);
    printf("  %-28s %lld\n","ADPCM only:",cls[A_T]);
    printf("  %-28s %lld\n","ENVMIXER only:",cls[E_T]);
    printf("  %-28s %lld\n","ADPCM+ENVMIXER:",cls[A_T|E_T]);
    printf("  total changed samples: %lld\n",chg);

    printf("\n=== CODEBOOK EPOCH / STATE PROVENANCE ===\n");
    { long long i,np=0,na=0,nb=0;
      for(i=0;i<MAXEP;i++){ if(ep_by_polef[i])np++; if(ep_by_adpcm[i])na++;
                            if(ep_by_polef[i]&&ep_by_adpcm[i])nb++; }
      printf("  LOADADPCM epochs created       : %lld\n",epoch);
      printf("  epochs CONSUMED by POLEF       : %lld\n",np);
      printf("  epochs CONSUMED by ADPCM       : %lld\n",na);
      printf("  epochs consumed in BOTH roles  : %lld\n",nb);
      printf("  epochs never consumed          : %lld\n",epoch-np-na+nb);
      printf("\n  --- consumer EVENTS (not epoch ids) ---\n");
      printf("  %-8s %10s %10s %10s %10s\n","role","events","resolved","unresolved","mixed");
      printf("  %-8s %10lld %10lld %10lld %10lld\n","POLEF",ev_tot[0],ev_res[0],ev_unres[0],ev_mix[0]);
      printf("  %-8s %10lld %10lld %10lld %10lld\n","ADPCM",ev_tot[1],ev_res[1],ev_unres[1],ev_mix[1]);
      printf("\n  LOADADPCM commands             : %lld\n",la_total);
      printf("  ...whose source overlaps an earlier task-local SAVEBUFF: %lld\n",la_over_saved);
      printf("  consumer events reading such bytes: %lld\n",ev_from_saved_cb);
      printf("\n  ADPCM header bytes read        : %lld\n",hdr_total);
      printf("  of which depend on an intra-task SAVEBUFF->LOADBUFF: %lld\n",hdr_from_saved);
    }
    (void)distinct_epochs_used;
    printf("  codebook reads by POLEF        : %lld\n",cb_reads_polef);
    printf("  codebook reads by ADPCM        : %lld\n",cb_reads_adpcm);
    printf("  SETLOOP writes to state +0x10  : %lld\n",setloop_writes);
    printf("  ADPCM A_LOOP reads of that field: %lld\n",adpcm_loop_reads);
    return 0;
}

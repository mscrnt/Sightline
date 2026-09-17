/* Full ENVMIXER evaluator, validated directly against the cartridge.
 *
 * Command structure, transcribed from the handler's control flow:
 *   A_INIT   construct both channels, clamp, emit one peeled output group,
 *            advance LEFT, then enter the loop.
 *   CONTINUE load the five state vectors, advance LEFT, then enter the loop.
 *   loop     clamp left; advance right; store v20/v21 to the state scratch;
 *            emit left dry and left wet; clamp right; advance left;
 *            emit right dry and right wet; decrement and step pointers.
 *   exit     store v18/v19/v24 to the state scratch, DMA the 80 bytes back.
 *
 * Output arithmetic is MIXER's already-validated form: the destination passes
 * through a near-unity multiply and the gained input is accumulated.
 */
#include "../../src/platform/sl_acmd.h"
#include "xtask.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Output-semantic mutations, for the negative control the 13-task population
 * needs. A population only validates output arithmetic if perturbing that
 * arithmetic moves the SAVED bytes - changing persistent state alone does not
 * count. MUT: 1 flatten lanes, 2 swap dry/wet destinations, 3 overwrite
 * instead of accumulate, 4 shift the lane/sample mapping by one. */
static int FLAT=0, MUT=0;
/* Mutation hit counters, measured AT THE SOURCE inside the mixer, restricted to
 * the 13 mixer-only tasks. This distinguishes "the mutation is inert on these
 * commands" from "the mutation changes output that is never saved" - a
 * distinction the global effect cannot make. */
static int IN13=0;
/* ACTIVE PCM WITNESS. Descent is MEASURED, not inferred through a taint model
 * that would have to replicate every mover: the witness command's emitted
 * values are perturbed by an arbitrary MARKER and the DRAM bytes that move are
 * by construction the ones descended from it, whatever path they took. The
 * flattening control is then scored on that pre-established set, so the descent
 * set and the control are two different measurements rather than one. */
static int WT_scan=0, WT_task=-1, WT_cmd=-1, WT_mode=0, WT_active=0, WT_g=0;
static const char *WT_dump=0;
/* the marker is parameterised and the descent set is the UNION over several
 * markers: a single low-byte marker can only reveal low-byte descendants, and
 * any one marker can be masked downstream by saturation. */
static int WT_mark=0x4141;
static int16_t WT_in[8], WT_gain[8], WT_out[8], WT_env[8], WT_dst[8], WT_dstw[8], WT_gw[8];
/* the group whose lanes are DUMPED must be the only group MUTATED, or the
 * scored differences are not attributable to the displayed lanes. */
static int WT_group=0; static int PRODMIX=0;
/* The five output controls, each scoped to the SINGLE group whose lanes are
 * dumped, so the differences they cause are attributable to those lanes.
 *   WT_mode 2 flatten | 3 gain selection | 4 overwrite | 5 lane shift
 *           6 destination routing swap
 * The global MUT codes carry the same semantics but apply to every command;
 * these reuse the semantics, not a second copy of them. */
#define WTM(x) (WT_active && WT_g==WT_group && WT_mode==(x))
/* DMEM WRITER EPOCHS. Nonzero input is not grounded input: what matters is
 * which command last wrote each input byte, and what that command's own source
 * was. Tracked per byte by diffing DMEM across every executed command, so the
 * writer is OBSERVED rather than inferred from a permanent address label. */
/* ACTUAL WRITER, updated on every PHYSICAL write whether or not the value
 * changes; ORIGIN, which movers transfer from their source rather than
 * relabelling the destination; and CHANGED, tracked separately. */
static int32_t DW_cmd[SL_DMEM_SIZE]; static uint8_t DW_op[SL_DMEM_SIZE];
static int32_t DW_task[SL_DMEM_SIZE];
static int32_t OR_cmd[SL_DMEM_SIZE]; static uint8_t OR_op[SL_DMEM_SIZE];
static int32_t OR_task[SL_DMEM_SIZE]; static uint8_t OR_kind[SL_DMEM_SIZE];
static uint32_t OR_src[SL_DMEM_SIZE];
static uint8_t DW_changed[SL_DMEM_SIZE];
static long long EV_writes=0, EV_bytes=0, EV_samevalue=0;
static sl_acmd_state *TRK_S=0; static int TRK_task=-1, TRK_cmd=-1, TRK_on=0;
/* pending write, so value-change is evaluated AFTER the physical store */
static uint32_t PD_off=0, PD_len=0; static uint8_t PD_prev[4096]; static int PD_live=0;
static void trk_flush(void)
{
    uint32_t q;
    if(!PD_live || !TRK_S) { PD_live=0; return; }
    for(q=0;q<PD_len;q++){ uint32_t ad=PD_off+q;
        if(TRK_S->dmem[ad]!=PD_prev[q]) DW_changed[ad]=1;
        else EV_samevalue++; }
    PD_live=0;
}
void sl_acmd_dwlog(uint32_t off, uint32_t len, int op, int kind, uint32_t src)
{
    uint32_t q; static uint32_t sv_t[4096], sv_c[4096], sv_o[4096], sv_k[4096], sv_s[4096];
    if(!TRK_on || !TRK_S) return;
    if(off+len>SL_DMEM_SIZE || len>4096) return;
    trk_flush();
    EV_writes++; EV_bytes+=len;
    /* snapshot the SOURCE origins before copying, so an overlapping move
     * cannot read origins it has already overwritten */
    if(kind==1) for(q=0;q<len;q++){ uint32_t sa=src+q;
        if(sa<SL_DMEM_SIZE){ sv_t[q]=(uint32_t)OR_task[sa]; sv_c[q]=(uint32_t)OR_cmd[sa];
            sv_o[q]=OR_op[sa]; sv_k[q]=OR_kind[sa]; sv_s[q]=OR_src[sa]; }
        else { sv_t[q]=sv_c[q]=0xFFFFFFFFu; sv_o[q]=0xFF; sv_k[q]=0; sv_s[q]=0; } }
    for(q=0;q<len;q++){ uint32_t ad=off+q;
        DW_task[ad]=TRK_task; DW_cmd[ad]=TRK_cmd; DW_op[ad]=(uint8_t)op;
        if(kind==1){ OR_task[ad]=(int32_t)sv_t[q]; OR_cmd[ad]=(int32_t)sv_c[q];
                     OR_op[ad]=(uint8_t)sv_o[q]; OR_kind[ad]=(uint8_t)sv_k[q];
                     OR_src[ad]=sv_s[q]; }
        else { OR_task[ad]=TRK_task; OR_cmd[ad]=TRK_cmd; OR_op[ad]=(uint8_t)op;
               OR_kind[ad]=(uint8_t)kind;
               /* byte i of a load comes from source base + i, NOT the base for
                * every byte - the whole point of a per-byte source record */
               OR_src[ad]=(kind==2)?(src+q):src; } }
    PD_off=off; PD_len=len; PD_live=1;
    for(q=0;q<len;q++) PD_prev[q]=TRK_S->dmem[off+q];
}
static uint32_t CMD_w0[4096], CMD_w1[4096];
static uint16_t CMD_in[4096], CMD_out[4096], CMD_cnt[4096];
#define A_ADPCM_N 1
#define A_CLEARBUFF_N 2
#define A_RESAMPLE_N 5
static long long CENS_op[17], CENS_active=0, CENS_grounded=0;
static long long CENS_wetnz=0, CENS_dstneq=0, CENS_discrim=0;
static long long CENS_mask[64], CENS_load_taskstart=0, CENS_load_ourwrite=0,
    CENS_load_offmap=0;
static long long RS_n=0,RS_init=0,RS_cont=0,RS_ph0=0,RS_phnz=0,RS_ph_ontap=0,
    RS_ph_offtap=0,RS_unitpitch=0,RS_cnt_odd=0; static unsigned RS_phmax=0;
static int RS_show=0;
static long long RS_inmisal=0, RS_inalign=0;
static int WT_taps=0, WT_rcmd=-1;
/* Scoring for the resampler challenges. A mutation only counts if it moves
 * bytes the cartridge OBSERVES and that are NOT the resampler's own 32-byte
 * bookkeeping - otherwise a change to state we never consume would read as a
 * firing control. The two populations are counted separately. */
extern int sl_rs_mut;
extern long long sl_rs_wb_hist, sl_rs_wb_input;
extern long long sl_rs_elig, sl_rs_applied, sl_rs_taps_moved, sl_rs_taps_seen;
extern uint32_t sl_rs_mA[512];
extern unsigned char sl_rs_win[512][128];
static long long SRC_off[129]; static int SRC_shown=0;
static long long MAP_best[17], MAP_d0[17], MAP_n=0, MAP_full=0;
/* EMPIRICAL derivation of the load behaviour, anchored on an ACCEPTED result:
 * the history quad is LDV v16[0],0(s1) + SDV -> +0x00..07, and that region is
 * cartridge-exact across the whole population, so an LDV at THAT address maps
 * byte-for-byte. The failing load differs only in its address, so the contrast
 * between the two addresses' alignments is the measurement. */
static long long ALN_all[16], ALN_fail[16], QAL_all[16], QAL_fail[16];
static long long PRES_both=0, PRES_one=0, PRES_none=0;
/* Self-chaining the resampler now runs on the SHARED cross-task scorer
 * (xtask.h) rather than a second hand-rolled one. The ungrounded bytes are
 * declared as a mask so they are EXCLUDED and counted, never absorbed. */
static const uint8_t RS_UNGROUNDED[32] = {
    /* +0x0C..0F: written by neither local store path (task-start scratch) */
    [0x0C]=1,[0x0D]=1,[0x0E]=1,[0x0F]=1,
    /* +0x10..11: the one-byte-displacement halfword, still unexplained */
    [0x10]=1,[0x11]=1 };
static xt_state RSX = { "resampler 32-byte state", 32, RS_UNGROUNDED };
/* The mixer on the SAME scorer. No ungrounded mask: all 80 bytes are grounded.
 * Run alongside the original hand-rolled oracle so the two can be compared
 * before the old one is removed - if a figure moves, that is a finding about
 * which scorer was wrong, not a number to reconcile. */
static xt_state MIX = { "mixer 80-byte state", 80, 0 };
static int MIXCHAIN=1;
static int RSCHAIN=0, RSCTL=0; static int RSC_ptask=-1;
/* ITEM 4: the outbound state DMA writes all 32 bytes, so EXPECTED writer is the
 * DMA for every byte of every resample state block, whether or not the scratch
 * value changed. ACTUAL writer is what the interpreter really performs.
 * VALUE-CHANGED is a third, separate fact. */
static uint8_t *EX_mark=0;            /* expected: DMA covers this byte */
static long long EX_bytes=0, EX_actual=0, EX_noactual=0, EX_valchg=0, EX_valsame=0;
/* ITEM 5: DMEM provenance of the four gap bytes (s7+0x0C..0x0F = 0xF9C..0xF9F,
 * s7 = 0xF90 from blob 0x004 addi s7,zero,3984 - the SAME scratch ENVMIXER uses) */
#define RS_S7 0xF90
static long long GAP_cls[6], GAP_op[17], GAP_init=0, GAP_cont=0;
extern int sl_rs_mV[512], sl_rs_mF[512], sl_rs_mR[512], sl_rs_mN;
static long long MIS_hist[2][16];
static int RSC_first_task=-1; static uint32_t RSC_first_addr=0;
static long long RSC_consumer_match=0, RSC_consumer_tot=0;
static long long RSC_state_match=0, RSC_state_tot=0;
static long long DPCM_live=0, DPCM_dead=0, DPCM_save=0, DPCM_save_live=0, DPCM_notsave[18];
static long long FW_mis[18], FW_rsoff[32], FW_nowriter=0, FW_other_writer=0, FW_rsoob=0;
static long long FW_unwr_off[32], FW_unwr_oob=0, FW_unwr_nostate=0;
static long long RES_init=0, RES_cont=0, RES_nocmd=0;
static long long TASK_exact=0, TASK_ung_only=0, TASK_other=0;
static uint8_t *RS_statemask=0; static uint32_t *RS_stateBase=0;
/* ACTUAL final DRAM writer, from the PHYSICAL write events (WRLOG), not from
 * each command's nominal range. Address membership in a state block is not
 * provenance; this is. */
static int32_t FW_task[1<<21], FW_cmd[1<<21]; static uint8_t FW_op[1<<21];
static uint32_t FW_base[1<<21];
static uint32_t DR_lo=0, DR_sz=0; static int FW_on=0;
void sl_acmd_wrlog(uint32_t addr, uint32_t len, int op)
{
    uint32_t q;
    if(!FW_on || DR_sz==0) return;
    for(q=0;q<len;q++){ uint32_t a2=addr+q;
        if(a2<DR_lo || a2>=DR_lo+DR_sz) continue;
        { uint32_t i=a2-DR_lo; if(i>=(1u<<21)) continue;
          FW_task[i]=TRK_task; FW_cmd[i]=TRK_cmd; FW_op[i]=(uint8_t)op;
          FW_base[i]=addr; } }
}
static uint32_t WT_in0=0;
static int WT_prov=0, WT_pcmd=-1; static uint32_t WT_paddr=0, WT_plen=16;
static const char *OPN[16]={"SPNOOP","ADPCM","CLEARBUFF","ENVMIXER","LOADBUFF",
    "RESAMPLE","SAVEBUFF","SEGMENT","SETBUFF","SETVOL","DMEMMOVE","LOADADPCM",
    "MIXER","INTERLEAVE","POLEF","SETLOOP"};
static void prov_dump(const sl_acmd_state*S,uint32_t fi,uint32_t k,uint32_t ad0,uint32_t len)
{
    uint32_t q;
    printf("WRITE-EVENT PROVENANCE - task %u, before command %u, DMEM %#05x..%#05x\n",
           fi,k,ad0,ad0+len-1);
    printf("  dmem   val | ACTUAL WRITER      | chg | ORIGIN (movers transfer)      \n");
    printf("             | task  cmd  opcode  |     | task  cmd  opcode     source  \n");
    for(q=0;q<len;q++){ uint32_t ad=ad0+q; int32_t wc=DW_cmd[ad], oc=OR_cmd[ad];
        printf("  %#05x  %02x  | ",ad,S->dmem[ad]);
        if(wc<0) printf("%-18s| %d   |","  (none this task)",DW_changed[ad]);
        else printf("%4d %5d  %-8s| %d   |",DW_task[ad],wc,OPN[DW_op[ad]&15],DW_changed[ad]);
        if(oc<0) printf(" %s\n","(none this task) - task-start DMEM");
        else { printf(" %4d %5d  %-10s",OR_task[ad],oc,OPN[OR_op[ad]&15]);
               if(OR_kind[ad]==2) printf(" DRAM %#08x\n",OR_src[ad]);
               else printf(" -\n"); } }
    printf("\n");
}
long long MH_cmds=0, MH_groups=0, MH_dryeqwet=0, MH_flatL=0;
long long MH_init_cmds=0, MH_cont_cmds=0, MH_g_peel=0, MH_g_loop=0;
long long MH_in_tot=0, MH_in_nz=0, MH_in_tot_peel=0, MH_in_nz_peel=0;
long long DST_tot[4]={0,0,0,0}, DST_nz[4]={0,0,0,0}, MH_bus[4]={0,0,0,0};
/* the 380: partitioned by 16-byte state slot, with observable denominators */
long long SLOT_obs[5]={0,0,0,0,0}, SLOT_mis[5]={0,0,0,0,0};
long long SLOT_lane[5][8]; long long SLOT_init[5]={0,0,0,0,0}, SLOT_cont[5]={0,0,0,0,0};
/* The partition above is scored only in ENVMIXER-ONLY tasks. That silently
 * excludes every MIXED task, which is where the first divergence lives, so an
 * all-zero result there was never evidence about the mixer as a whole.
 * Scored again over EVERY task containing an ENVMIXER command. */
long long SA_obs[5]={0,0,0,0,0}, SA_mis[5]={0,0,0,0,0};
long long SA_lane[5][8]; long long SA_init[5]={0,0,0,0,0}, SA_cont[5]={0,0,0,0,0};
long long SA_tasks=0;
static const char *SLOTN[5]={"+0x00 L.hi","+0x10 L.lo","+0x20 R.hi","+0x30 R.lo","+0x40 params"};
static const char *BUSN[4]={"dry-left","wet-left","dry-right","wet-right"};
#define HIT(b,dstaddr,gmut,gbase) do{ if(IN13){ DST_tot[b]++; \
    if(g16(S->dmem,(dstaddr))) DST_nz[b]++; \
    if(MUT&&MUT!=5){ int sv=MUT; int16_t a1,b1; MUT=0; \
        a1=mixstep(g16(S->dmem,(dstaddr)),iv,(gbase)); MUT=sv; \
        b1=mixstep(g16(S->dmem,(dstaddr)),iv,(gmut)); \
        if(a1!=b1) MH_bus[b]++; } } }while(0)

static int16_t cl16(int64_t v){ if(v>32767)return 32767; if(v<-32768)return -32768; return (int16_t)v; }
static int64_t acc48(int64_t v){ v&=0xFFFFFFFFFFFFLL; if(v&0x800000000000LL)v-=0x1000000000000LL; return v; }
static int fires(int64_t a){ int64_t t=a>>31; return !(t==0||t==-1); }
long long NEG_UNGROUNDED=0;
static int16_t low_sat32(int64_t a){ if(!fires(a)) return (int16_t)(a&0xFFFF);
    if(a>0) return (int16_t)0xFFFF; NEG_UNGROUNDED++; return (int16_t)0x0000; }
static int16_t vmulf(int16_t a,int16_t b)
{ return cl16(((((int64_t)a*(int64_t)b)<<1)+0x8000)>>16); }
static int16_t mixstep(int16_t dst,int16_t in,int16_t g)
{ int64_t acc;
  if(MUT==3||WTM(4)){ acc=((int64_t)in*(int64_t)g)<<1; return cl16(acc>>16); } /* overwrite */
  acc=(((int64_t)dst*(int64_t)0x7FFF)<<1)+0x8000;
  acc+=((int64_t)in*(int64_t)g)<<1; return cl16(acc>>16); }

typedef struct { int16_t hi[8], lo[8]; } Env;

static void construct(const int16_t*ramp,int16_t cvol,int16_t ratm,uint16_t ratl,Env*e)
{
    int k;
    for(k=0;k<8;k++){
        uint16_t f=(uint16_t)ramp[k]; int64_t a;
        a=acc48(((int64_t)f*(int64_t)ratl)>>16);
        a=acc48(a+(int64_t)f*(int64_t)ratm);
        a=acc48(a+(((int64_t)1*(int64_t)cvol)<<16));
        e->hi[k]=cl16(a>>16); e->lo[k]=low_sat32(a);
    }
}
static void clampe(Env*e,int16_t tgt,int16_t ratm)
{ int k; for(k=0;k<8;k++){
    if(ratm>0){ uint16_t d=(uint16_t)((uint16_t)e->hi[k]-(uint16_t)tgt);
                e->hi[k]=((int16_t)d>=0)?tgt:e->hi[k]; }
    else      { e->hi[k]=(e->hi[k]>tgt)?e->hi[k]:tgt; } } }
static void advance(Env*e,int16_t ratm,uint16_t ratl)
{ int k; for(k=0;k<8;k++){
    uint32_t s=(uint32_t)(uint16_t)e->lo[k]+(uint32_t)ratl;
    e->lo[k]=(int16_t)(s&0xFFFF);
    e->hi[k]=cl16((int32_t)e->hi[k]+(int32_t)ratm+(int)((s>>16)&1)); } }

static int16_t g16(const uint8_t*m,uint32_t o){return (int16_t)((m[o]<<8)|m[o+1]);}
static void p16(uint8_t*m,uint32_t o,int16_t v){ m[o]=(uint8_t)((v>>8)&0xFF); m[o+1]=(uint8_t)(v&0xFF); }

/* FLAT != 0 collapses the eight lane gains to lane 0's, for the witness control */


/* THE SINGLE EMISSION POINT. Every output halfword the evaluator writes goes
 * through here, and so does the destination-swap control (MUT==5) and its
 * self-test. A self-test that merely showed two fixture values differ never
 * executed this routing at all, which is why it was withdrawn. */
static void emit2(uint8_t*dmem, uint32_t aDry, uint32_t aWet,
                  int16_t iv, int16_t gDry, int16_t gWet, int busbase)
{
    int16_t vd=mixstep(g16(dmem,aDry),iv,gDry);
    int16_t vw=mixstep(g16(dmem,aWet),iv,gWet);
    if(WT_active && WT_mode==1 && WT_g==WT_group){
        vd=(int16_t)(vd^WT_mark); vw=(int16_t)(vw^WT_mark); }
    if(IN13&&(MUT==5||WTM(6))&&vd!=vw){ MH_bus[busbase]++; MH_bus[busbase+1]++; }
    sl_acmd_dwlog(aDry,2,3,0,0); sl_acmd_dwlog(aWet,2,3,0,0);
    if(MUT==5||WTM(6)){ p16(dmem,aDry,vw); p16(dmem,aWet,vd); }
    else      { p16(dmem,aDry,vd); p16(dmem,aWet,vw); }
}
static void env_exec(sl_acmd_state*S,uint32_t w0,uint32_t w1,const int16_t*ramp,
                     uint32_t lo,uint32_t dsz)
{
    int init=(int)((w0>>16)&1), aux=(int)((w0>>16)&8);
    uint32_t st=w1&0xFFFFFF;
    uint32_t in=sl_acmd_sget(S,0x00), d3=sl_acmd_sget(S,0x02);
    uint32_t d2=sl_acmd_sget(S,0x0A), d1=sl_acmd_sget(S,0x0C), d0=sl_acmd_sget(S,0x0E);
    int cnt=(int)sl_acmd_sget(S,0x04);
    int16_t tgtL,ratmL,tgtR,ratmR,dry,wet; uint16_t ratlL,ratlR;
    Env L,R; int g,G,step=aux?16:0; uint8_t *p;
    tgtL=sl_acmd_sget(S,0x10); ratmL=sl_acmd_sget(S,0x12); ratlL=sl_acmd_sget(S,0x14);
    tgtR=sl_acmd_sget(S,0x16); ratmR=sl_acmd_sget(S,0x18); ratlR=sl_acmd_sget(S,0x1A);
    dry =sl_acmd_sget(S,0x1C); wet =sl_acmd_sget(S,0x1E);
    if(!aux){ d1=d0=0xF90+80; }
    G=(cnt>0)?((cnt+15)/16):1;
    if(IN13){ MH_cmds++; MH_groups+=G; if(init) MH_init_cmds++; else MH_cont_cmds++;
        if(dry==wet) MH_dryeqwet++;
        { int z; for(z=0;z<8;z++) if(L.hi[z]!=L.hi[0]) break;
          if(z==8) MH_flatL++; } }
    if(init){
        construct(ramp,(int16_t)sl_acmd_sget(S,0x06),ratmL,ratlL,&L);
        construct(ramp,(int16_t)sl_acmd_sget(S,0x08),ratmR,ratlR,&R);
    } else {
        p=(st>=lo&&st+80<=lo+dsz)?S->dram+(st-lo):0;
        if(!p) return;
        for(g=0;g<8;g++){ L.hi[g]=g16(p,2*g); L.lo[g]=g16(p,16+2*g);
                          R.hi[g]=g16(p,32+2*g); R.lo[g]=g16(p,48+2*g); }
        /* The parameter vector is reloaded from the SAVED STATE at +0x40, not
         * from the live task-state block. The handler's delay-slot load from
         * the task block is overwritten by this one before first use, so on a
         * continuation the command runs with the parameters captured when the
         * segment began - not with whatever SETVOL last wrote. Reading the live
         * fields here was the defect. */
        tgtL =g16(p,64+0);  ratmL=g16(p,64+2);  ratlL=(uint16_t)g16(p,64+4);
        tgtR =g16(p,64+6);  ratmR=g16(p,64+8);  ratlR=(uint16_t)g16(p,64+10);
        dry  =g16(p,64+12); wet  =g16(p,64+14);
    }
    if(init){                                  /* peeled output group */
        int k; clampe(&L,tgtL,ratmL); clampe(&R,tgtR,ratmR);
        for(k=0;k<8;k++){
            int16_t iv=g16(S->dmem,in+2*k);
            int kk=(MUT==4||WTM(5))?((k+1)&7):k;
            int fl=FLAT||(WT_active&&WT_mode==2&&WT_g==WT_group);
            int16_t gl=vmulf(fl?L.hi[0]:L.hi[kk],(MUT==2||WTM(3))?wet:dry), wl=vmulf(fl?L.hi[0]:L.hi[kk],(MUT==2||WTM(3))?dry:wet);
            int16_t gr=vmulf((FLAT||(WT_active&&WT_mode==2&&WT_g==WT_group))?R.hi[0]:R.hi[kk],(MUT==2||WTM(3))?wet:dry), wr=vmulf((FLAT||(WT_active&&WT_mode==2&&WT_g==WT_group))?R.hi[0]:R.hi[kk],(MUT==2||WTM(3))?dry:wet);
            if(IN13){ MH_in_tot_peel++; if(iv) MH_in_nz_peel++; }
            HIT(0,d3+2*k,gl,vmulf(L.hi[k],dry));
            HIT(1,d1+2*k,wl,vmulf(L.hi[k],wet));
            HIT(2,d2+2*k,gr,vmulf(R.hi[k],dry));
            HIT(3,d0+2*k,wr,vmulf(R.hi[k],wet));
            if(WT_active&&WT_g==WT_group){ WT_in[k]=iv; WT_gain[k]=gl; WT_gw[k]=wl;
                WT_env[k]=fl?L.hi[0]:L.hi[kk]; WT_dst[k]=g16(S->dmem,d3+2*k);
                WT_dstw[k]=g16(S->dmem,d1+2*k); }
            emit2(S->dmem, d3+2*k, d1+2*k, iv, gl, wl, 0);
            emit2(S->dmem, d2+2*k, d0+2*k, iv, gr, wr, 2);
            if(WT_active&&WT_g==WT_group) WT_out[k]=g16(S->dmem,d3+2*k);
        }
        if(IN13) MH_g_peel++;
        WT_g++;
        in+=16; d3+=16; d2+=16; d1+=step; d0+=step; cnt-=16;
    }
    advance(&L,ratmL,ratlL);                   /* the pre-loop left advance */
    for(g=0;g<G-(init?1:0);g++){
        int k;
        clampe(&L,tgtL,ratmL);
        advance(&R,ratmR,ratlR);
        for(k=0;k<8;k++){ sl_acmd_dwlog(0xF90+2*k,2,3,0,0);
                          sl_acmd_dwlog(0xF90+16+2*k,2,3,0,0);
                          p16(S->dmem,0xF90+2*k,L.hi[k]); p16(S->dmem,0xF90+16+2*k,L.lo[k]); }
        for(k=0;k<8;k++){
            int16_t iv=g16(S->dmem,in+2*k);
            int kk=(MUT==4||WTM(5))?((k+1)&7):k;
            int fl=FLAT||(WT_active&&WT_mode==2);
            int16_t gl=vmulf(fl?L.hi[0]:L.hi[kk],(MUT==2||WTM(3))?wet:dry), wl=vmulf(fl?L.hi[0]:L.hi[kk],(MUT==2||WTM(3))?dry:wet);
            if(IN13){ MH_in_tot++; if(iv) MH_in_nz++; }
            HIT(0,d3+2*k,gl,vmulf(L.hi[k],dry));
            HIT(1,d1+2*k,wl,vmulf(L.hi[k],wet));
            if(WT_active&&WT_g==WT_group){ WT_in[k]=iv; WT_gain[k]=gl; WT_gw[k]=wl;
                WT_env[k]=fl?L.hi[0]:L.hi[kk]; WT_dst[k]=g16(S->dmem,d3+2*k);
                WT_dstw[k]=g16(S->dmem,d1+2*k); }
            emit2(S->dmem, d3+2*k, d1+2*k, iv, gl, wl, 0);
            if(WT_active&&WT_g==WT_group) WT_out[k]=g16(S->dmem,d3+2*k);
        }
        clampe(&R,tgtR,ratmR);
        advance(&L,ratmL,ratlL);
        for(k=0;k<8;k++){
            int16_t iv=g16(S->dmem,in+2*k);
            int kk=(MUT==4||WTM(5))?((k+1)&7):k;
            int16_t gr=vmulf((FLAT||(WT_active&&WT_mode==2))?R.hi[0]:R.hi[kk],(MUT==2||WTM(3))?wet:dry), wr=vmulf((FLAT||(WT_active&&WT_mode==2))?R.hi[0]:R.hi[kk],(MUT==2||WTM(3))?dry:wet);
            HIT(2,d2+2*k,gr,vmulf(R.hi[k],dry));
            HIT(3,d0+2*k,wr,vmulf(R.hi[k],wet));
            emit2(S->dmem, d2+2*k, d0+2*k, iv, gr, wr, 2);
        }
        if(IN13) MH_g_loop++;
        WT_g++;
        in+=16; d3+=16; d2+=16; d1+=step; d0+=step;
    }
    for(g=0;g<8;g++){ sl_acmd_dwlog(0xF90+32+2*g,2,3,0,0);
                      sl_acmd_dwlog(0xF90+48+2*g,2,3,0,0);
                      p16(S->dmem,0xF90+32+2*g,R.hi[g]); p16(S->dmem,0xF90+48+2*g,R.lo[g]); }
    /* The parameter vector written back is the one this command RAN WITH, not
     * whatever the live task-state block currently holds. On a continuation
     * that is the vector loaded from the saved state at +0x40; on an init it is
     * the task-state block. Reading the live fields here unconditionally was
     * the same class of defect as the read side. */
    sl_acmd_dwlog(0xF90+64,16,3,0,0);
    p16(S->dmem,0xF90+64+0 ,tgtL);  p16(S->dmem,0xF90+64+2 ,ratmL);
    p16(S->dmem,0xF90+64+4 ,(int16_t)ratlL);
    p16(S->dmem,0xF90+64+6 ,tgtR);  p16(S->dmem,0xF90+64+8 ,ratmR);
    p16(S->dmem,0xF90+64+10,(int16_t)ratlR);
    p16(S->dmem,0xF90+64+12,dry);   p16(S->dmem,0xF90+64+14,wet);
    p=(st>=lo&&st+80<=lo+dsz)?S->dram+(st-lo):0;
    if(p) memcpy(p,S->dmem+0xF90,80);
}

/* every opcode capable of writing DRAM, with the extent it writes, so a
 * differing byte can be attributed to the command that last wrote it */
static int dram_write_range(uint32_t w0,uint32_t w1,uint16_t setcount,
                            uint32_t *a,uint32_t *n)
{
    switch((int)((w0>>24)&0xFF)){
    case 6:  *a=w1;          *n=setcount; return 1;   /* SAVEBUFF   */
    case 1:  *a=w1&0xFFFFFF; *n=32;       return 1;   /* ADPCM      */
    case 5:  *a=w1&0xFFFFFF; *n=32;       return 1;   /* RESAMPLE   */
    case 14: *a=w1&0xFFFFFF; *n=8;        return 1;   /* POLEF      */
    case 3:  *a=w1&0xFFFFFF; *n=80;       return 1;   /* ENVMIXER   */
    default: return 0; }
}
/* CONSTRUCTED self-test of the destination-swap control, driven through the
 * SAME emit2 helper and the SAME MUT==5 switch the evaluator uses - not a pair
 * of fixture values computed alongside it. It writes into a real DMEM buffer at
 * the canonical dry/wet addresses, runs the emission unswapped and swapped from
 * identical starting state, and requires the BYTES AT THOSE ADDRESSES to differ.
 * Distinct destination contents and distinct gains, so a genuine bus swap
 * cannot be masked by symmetry. Not a cartridge measurement - it validates the
 * instrument, which a silent population never can. */
static int dswap_selftest(void)
{
    uint8_t m[64]; const uint32_t aDry=0, aWet=16;
    const int16_t iv=1000, dst0=100, dst1=-250, gDry=0x4000, gWet=0x1000;
    uint8_t un[4], sw[4]; int saveMUT=MUT, saveIN13=IN13, k, differ=0;
    IN13=0;
    memset(m,0,sizeof m); p16(m,aDry,dst0); p16(m,aWet,dst1);
    MUT=0; emit2(m,aDry,aWet,iv,gDry,gWet,0);
    un[0]=m[aDry];un[1]=m[aDry+1];un[2]=m[aWet];un[3]=m[aWet+1];
    memset(m,0,sizeof m); p16(m,aDry,dst0); p16(m,aWet,dst1);
    MUT=5; emit2(m,aDry,aWet,iv,gDry,gWet,0);
    sw[0]=m[aDry];sw[1]=m[aDry+1];sw[2]=m[aWet];sw[3]=m[aWet+1];
    MUT=saveMUT; IN13=saveIN13;
    for(k=0;k<4;k++) if(un[k]!=sw[k]) differ++;
    printf("CONSTRUCTED destination-swap self-test (instrument check, not measured)\n");
    printf("  driven through emit2, the evaluator's only emission point\n");
    printf("  input %d   dry dst %d gain %#06x   wet dst %d gain %#06x\n",
           iv,dst0,(unsigned)(uint16_t)gDry,dst1,(unsigned)(uint16_t)gWet);
    printf("  unswapped bytes at dry/wet: %02x %02x / %02x %02x  ->  %6d %6d\n",
           un[0],un[1],un[2],un[3],(int16_t)((un[0]<<8)|un[1]),(int16_t)((un[2]<<8)|un[3]));
    printf("  swapped   bytes at dry/wet: %02x %02x / %02x %02x  ->  %6d %6d\n",
           sw[0],sw[1],sw[2],sw[3],(int16_t)((sw[0]<<8)|sw[1]),(int16_t)((sw[2]<<8)|sw[3]));
    printf("  %d of 4 emitted bytes change.  %s\n\n", differ, differ
        ? "CONTROL CAN FIRE: routing the buses differently changes what lands at each address."
        : "CONTROL CANNOT FIRE - the instrument is still broken.");
    return differ>0;
}
/* CONSTRUCTED same-value control. A tracker that infers writes from value
 * changes cannot see a store of the value already present - the exact flaw
 * already repaired on the DRAM side. This challenges the instrument before any
 * census is read off it:
 *   arm A  destination already zero, CLEARBUFF physically stores zero over it
 *          -> ACTUAL WRITER must update while VALUE-CHANGED stays false
 *   arm B  the same command not executed
 *          -> writer must NOT update, though the bytes read identically
 * A tracker that passes A but fails B is relabelling; one that fails A is the
 * old value-diff tracker wearing a new name. */
static int samevalue_selftest(void)
{
    static sl_acmd_state S2; static uint8_t dr[64];
    uint32_t d=0x5c0, c=32, q; uint32_t cmd[2]; int okA,okB,identical;
    int32_t wcA; uint8_t chA;
    memset(dr,0,sizeof dr);
    /* arm A */
    sl_acmd_init(&S2,dr,0,sizeof dr);
    memset(DW_cmd,0xFF,sizeof DW_cmd); memset(DW_task,0xFF,sizeof DW_task);
    memset(DW_changed,0,sizeof DW_changed);
    TRK_S=&S2; TRK_on=1; PD_live=0; TRK_task=7; TRK_cmd=42;
    for(q=0;q<c;q++) S2.dmem[d+q]=0;          /* already holds the value */
    cmd[0]=(uint32_t)A_CLEARBUFF_N<<24 | (d-0x5c0); cmd[1]=c;
    sl_acmd_exec(&S2,cmd,1); trk_flush();
    wcA=DW_cmd[d]; chA=DW_changed[d];
    okA = (DW_cmd[d]==42 && DW_task[d]==7 && DW_changed[d]==0);
    for(q=0;q<c;q++) if(DW_cmd[d+q]!=42 || DW_changed[d+q]) okA=0;
    /* arm B */
    sl_acmd_init(&S2,dr,0,sizeof dr);
    memset(DW_cmd,0xFF,sizeof DW_cmd); memset(DW_task,0xFF,sizeof DW_task);
    memset(DW_changed,0,sizeof DW_changed);
    TRK_S=&S2; TRK_on=1; PD_live=0; TRK_task=7; TRK_cmd=43;
    for(q=0;q<c;q++) S2.dmem[d+q]=0;
    /* command deliberately NOT executed */
    trk_flush();
    identical=1; for(q=0;q<c;q++) if(S2.dmem[d+q]!=0) identical=0;
    okB = (DW_cmd[d]==-1);
    for(q=0;q<c;q++) if(DW_cmd[d+q]!=-1) okB=0;
    TRK_on=0; TRK_S=0;
    printf("CONSTRUCTED same-value control (challenges the write-event tracker)\n");
    printf("  arm A  same-value store over %u bytes: writer cmd %d (want 42), "
           "value-changed %d (want 0)  -> %s\n", c, wcA, chA, okA?"PASS":"FAIL");
    printf("  arm B  store disabled, bytes read identically (%s): writer cmd %d "
           "(want -1)  -> %s\n", identical?"yes":"no", DW_cmd[d], okB?"PASS":"FAIL");
    printf("  %s\n\n", (okA&&okB)
        ? "TRACKER RECORDS WRITE EVENTS, not value changes."
        : "TRACKER IS INVALID - no census may be read off it.");
    return okA && okB && identical;
}
/* TRACKER GATES. Not investigations - each is a fixture the tracker must pass
 * before anything is read off it. */
#define A_DMEMMOVE_N 10
#define A_INTERLEAVE_N 13
#define A_LOADBUFF_N 4
static void trk_reset(sl_acmd_state*S2)
{
    memset(DW_cmd,0xFF,sizeof DW_cmd); memset(DW_task,0xFF,sizeof DW_task);
    memset(DW_op,0xFF,sizeof DW_op);
    memset(OR_cmd,0xFF,sizeof OR_cmd); memset(OR_op,0xFF,sizeof OR_op);
    memset(OR_task,0xFF,sizeof OR_task); memset(OR_kind,0,sizeof OR_kind);
    memset(OR_src,0,sizeof OR_src); memset(DW_changed,0,sizeof DW_changed);
    TRK_S=S2; TRK_on=1; PD_live=0;
}
static int mover_gate(void)
{
    static sl_acmd_state S2; static uint8_t dr[256];
    uint32_t src=0x600, dst=0x700, c=16, q, cmd[2]; int ok1,ok2,ok3,ok4;
    int32_t wsave; memset(dr,0,sizeof dr);
    /* arm A: destination ALREADY holds identical values, so a value-diff
     * tracker sees nothing at all */
    sl_acmd_init(&S2,dr,0,sizeof dr); trk_reset(&S2);
    TRK_task=1; TRK_cmd=10;
    for(q=0;q<c;q++){ S2.dmem[src+q]=(uint8_t)(0xA0+q); S2.dmem[dst+q]=(uint8_t)(0xA0+q); }
    sl_acmd_dwlog(src,c,A_ADPCM_N,0,0);      /* seed the ORIGINAL producer */
    trk_flush();
    TRK_cmd=11;
    cmd[0]=(uint32_t)A_DMEMMOVE_N<<24 | (src-0x5c0);
    cmd[1]=((dst-0x5c0)<<16) | c;
    sl_acmd_exec(&S2,cmd,1); trk_flush();
    ok1 = (DW_op[dst]==A_DMEMMOVE_N && DW_cmd[dst]==11);   /* mover is the ACTUAL writer */
    ok2 = (OR_op[dst]==A_ADPCM_N && OR_cmd[dst]==10);      /* ORIGIN is the producer */
    ok3 = 1; for(q=0;q<c;q++) if(DW_changed[dst+q]) ok3=0; /* value did NOT change */
    wsave=DW_cmd[dst];
    /* arm B: mover not executed */
    sl_acmd_init(&S2,dr,0,sizeof dr); trk_reset(&S2);
    TRK_task=1; TRK_cmd=10;
    for(q=0;q<c;q++){ S2.dmem[src+q]=(uint8_t)(0xA0+q); S2.dmem[dst+q]=(uint8_t)(0xA0+q); }
    sl_acmd_dwlog(src,c,A_ADPCM_N,0,0); trk_flush();
    ok4 = (DW_cmd[dst]==-1);
    printf("  mover gate      : actual writer DMEMMOVE@11 %s | origin ADPCM@10 %s | "
           "value-changed false %s | not-run writer untouched %s\n",
           ok1?"ok":"FAIL",ok2?"ok":"FAIL",ok3?"ok":"FAIL",ok4?"ok":"FAIL");
    (void)wsave; return ok1&&ok2&&ok3&&ok4;
}
static int interleave_gate(void)
{
    static sl_acmd_state S2; static uint8_t dr[256];
    uint32_t L=0x600, R=0x680, c=8, q, cmd[2]; int okA,okB,okC;
    memset(dr,0,sizeof dr);
    sl_acmd_init(&S2,dr,0,sizeof dr); trk_reset(&S2);
    TRK_task=2; TRK_cmd=20;
    sl_acmd_dwlog(L,c,A_ADPCM_N,0,0); trk_flush();     /* left side producer */
    TRK_cmd=21;
    sl_acmd_dwlog(R,c,A_RESAMPLE_N,0,0); trk_flush();  /* right side producer */
    sl_acmd_sset(&S2,0x02,0x700); sl_acmd_sset(&S2,0x04,c);
    TRK_cmd=22;
    cmd[0]=(uint32_t)A_INTERLEAVE_N<<24; cmd[1]=((L-0x5c0)<<16)|(R-0x5c0);
    sl_acmd_exec(&S2,cmd,1); trk_flush();
    okA=okB=okC=1;
    for(q=0;q<c;q+=2){ uint32_t dL=0x700+2*q, dR=0x700+2*q+2;
        if(OR_op[dL]!=A_ADPCM_N || OR_cmd[dL]!=20) okA=0;      /* left inherits left */
        if(OR_op[dR]!=A_RESAMPLE_N || OR_cmd[dR]!=21) okB=0;   /* right inherits right */
        if(DW_op[dL]!=A_INTERLEAVE_N || DW_op[dR]!=A_INTERLEAVE_N) okC=0; }
    printf("  interleave gate : left dest inherits LEFT origin %s | right dest inherits "
           "RIGHT origin %s | actual writer INTERLEAVE %s\n",
           okA?"ok":"FAIL",okB?"ok":"FAIL",okC?"ok":"FAIL");
    return okA&&okB&&okC;
}
static int load_gate(void)
{
    static sl_acmd_state S2; static uint8_t dr[256];
    uint32_t base=0x40, cmd[2]; int ok1,ok2;
    memset(dr,0,sizeof dr); dr[base]=0x11; dr[base+1]=0x22;
    sl_acmd_init(&S2,dr,0,sizeof dr); trk_reset(&S2);
    sl_acmd_sset(&S2,0x00,0x600); sl_acmd_sset(&S2,0x04,2);
    TRK_task=3; TRK_cmd=30;
    cmd[0]=(uint32_t)A_LOADBUFF_N<<24; cmd[1]=base;
    sl_acmd_exec(&S2,cmd,1); trk_flush();
    ok1 = (OR_src[0x600]==base);
    ok2 = (OR_src[0x601]==base+1);   /* NOT the base again */
    printf("  load gate       : byte 0 source %#x (want %#x) %s | byte 1 source %#x "
           "(want %#x) %s\n",OR_src[0x600],base,ok1?"ok":"FAIL",
           OR_src[0x601],base+1,ok2?"ok":"FAIL");
    return ok1&&ok2;
}
/* ITEM 4 CONTROL: a byte whose EXPECTED writer is the outbound state DMA while
 * VALUE-CHANGED is false. Running the same RESAMPLE twice from identical state
 * makes the second writeback store the values already present: the DMA is still
 * expected to write all 32 bytes, the interpreter still physically writes them,
 * and yet nothing changes. A model that inferred "written" from "changed" would
 * report this byte as unwritten. */
static int statedma_samevalue_gate(void)
{
    static sl_acmd_state S2; static uint8_t dr[512];
    uint32_t cmd[2]; uint8_t first[32]; int q, same, wrote;
    memset(dr,0,sizeof dr);
    for(q=0;q<64;q++) dr[128+q]=(uint8_t)(0x20+q);     /* something to resample */
    sl_acmd_init(&S2,dr,0,sizeof dr);
    sl_acmd_sset(&S2,0x00,0x600); sl_acmd_sset(&S2,0x02,0x700);
    sl_acmd_sset(&S2,0x04,32);
    for(q=0;q<32;q++) S2.dmem[0x600+q]=(uint8_t)(0x40+q);
    cmd[0]=((uint32_t)5<<24)|0x4000; cmd[1]=64;         /* CONTINUE, state at 64 */
    sl_acmd_exec(&S2,cmd,1);
    memcpy(first,dr+64,32);
    /* second identical run from the identical inbound state */
    memset(FW_cmd,0xFF,sizeof(int32_t)*256);
    DR_lo=0; DR_sz=sizeof dr; FW_on=1; TRK_task=9; TRK_cmd=99;
    for(q=0;q<256;q++){ FW_cmd[q]=-1; FW_op[q]=0xFF; }
    memcpy(dr+64,first,32);
    for(q=0;q<32;q++) S2.dmem[0x600+q]=(uint8_t)(0x40+q);
    sl_acmd_exec(&S2,cmd,1);
    same = (memcmp(dr+64,first,32)==0);
    wrote = 0; for(q=0;q<32;q++) if(FW_cmd[64+q]==99) wrote++;
    FW_on=0;
    printf("  state-DMA same-value gate: expected writer = DMA for 32 bytes, "
           "actual writes %d, value unchanged %s -> %s\n",
           wrote, same?"yes":"no",
           (same && wrote>0)?"PASS":"FAIL");
    return same && wrote>0;
}
static int tracker_gates(void)
{
    int a,b,c2;
    printf("TRACKER GATES (fixtures the tracker must pass before it is read)\n");
    a=mover_gate(); b=interleave_gate(); c2=load_gate();
    if(!statedma_samevalue_gate()) a=0;
    TRK_on=0; TRK_S=0;
    printf("  %s\n\n",(a&&b&&c2)?"ALL TRACKER GATES PASS."
                                 :"TRACKER GATE FAILURE - nothing may be read off it.");
    return a&&b&&c2;
}
static uint32_t be32(const uint8_t*p){return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];}
/* CHRONOLOGICAL STATE TRACE: one record per ENVMIXER command in traversal
 * order, so the FIRST divergence against a reduced world can be located at the
 * earliest command rather than at a downstream symptom. */
static FILE *CHRON=0; static uint32_t CHRON_hdr[5];

int main(int argc,char**argv)
{
    const char*path=argc>1?argv[1]:"/tmp/w/all.bin";
    MUT=(argc>2)?atoi(argv[2]):0; FLAT=(MUT==1);
    if(!dswap_selftest()) return 3;
    if(!samevalue_selftest()){ fprintf(stderr,
        "FATAL: the write-event tracker failed its own control\n"); return 8; }
    if(!tracker_gates()){ fprintf(stderr,
        "FATAL: a tracker gate failed\n"); return 9; }
    static sl_acmd_state S; int16_t ramp[8]; uint8_t db[0x2c0];
    FILE*f,*d; uint8_t h[20]; uint32_t nf,lo,dsz,fi; uint8_t*bef,*aft,*dram;
    long long tasks=0,done=0,cmp=0,mis=0,envn=0;
    long long mis_state=0,mis_out=0,mis_wewrote=0,mis_wemissed=0,wrote=0;
    uint8_t *instate; uint16_t *lastop; uint32_t *lastcmd;
    /* HYBRID BOUNDARY MODEL. Ordinary task inputs come from the measured
     * before-image and task-local DMEM starts fresh, but ACTIVE PERSISTENT
     * STATE is carried from this world's own prior result. A single epoch
     * spans up to 86 tasks and 85 boundary transitions, so a wholesale reseed
     * severs real chains. Only the mixer's 80-byte blocks are carried;
     * unrelated DRAM still comes from the measured task. */
#define WS_MAX 64
    static uint32_t ws_addr[WS_MAX]; static uint8_t ws_data[WS_MAX][80];
    static int ws_n=0; int ws_i;
    long long carried=0, carried_tasks=0;
    /* DIRECT boundary oracle on the FULL evaluator: at each cross-task
     * consumer, compare its carried 80 bytes against that task's cartridge
     * before-image BEFORE executing. Tests the localisation rather than
     * inferring it from aggregate attribution. */
    xt_init(&MIX); xt_init(&RSX);
    static uint32_t lastT[64], lastA[64]; int lastN=0;
    long long attrib[16], attrib_nv[16], owned_nv[16]; long long a_init=0,a_cont=0;
    /* three-way population split: 0=neither, 2=ENVMIXER only, 3=both */
    long long P_tasks[4], P_diff[4], P_own[4][16], P_att[4][16];
    /* byte-level init coverage: state bytes where an A_INIT ENVMIXER command is
       the FINAL DRAM writer across ALL opcodes, not merely the last ENVMIXER */
    long long init_bytes_final=0, init_bytes_mismatch=0, init_cmds=0;
    /* SEMANTIC vacuity check: a large owned-byte count proves nothing if the
       cartridge never changed those bytes. Split by whether each side moved. */
    long long sv13_cart_chg=0, sv13_ev_chg=0, sv13_both_agree=0,
              sv13_both_unchanged=0, sv13_mismatch=0, sv13_total=0;
    long long ad_cart_chg=0, ad_unchanged=0, ad_mismatch=0, ad_total=0;
    int firstdiv_task=-1,firstdiv_cmd=-1,firstdiv_op=-1; uint32_t firstdiv_addr=0;
    long long firstdiv_pos=0, ncmds_total=0;
    long long exact_tasks=0,firstbad_diff=0,firstbad_unwr=0;
    int firstbad=-1; uint32_t fb_addr=0,fu_addr=0,fu_len=0;
    int has_env=0,has_adp=0;
    long long cls_n[4]={0,0,0,0}, cls_ok[4]={0,0,0,0}; int nov=0;
    int fb_exp=0,fb_got=0,fb_bef=0;
    d=fopen("bin/aspboot.data.bin","rb");
    if(!d){fprintf(stderr,"FATAL: extraction missing\n");return 2;}
    fread(db,1,sizeof db,d); fclose(d);
    { int k; for(k=0;k<8;k++) ramp[k]=(int16_t)((db[0xB0+2*k]<<8)|db[0xB0+2*k+1]); }
    { const char*e;
      if((e=getenv("WT_SCAN"))) WT_scan=atoi(e);
      if((e=getenv("WT_TASK"))) WT_task=atoi(e);
      if((e=getenv("WT_CMD")))  WT_cmd=atoi(e);
      if((e=getenv("WT_MODE"))) WT_mode=atoi(e);
      if((e=getenv("WT_MARK"))) WT_mark=(int)strtol(e,0,0);
      if((e=getenv("WT_PROV"))) WT_prov=atoi(e);
      if((e=getenv("WT_PCMD"))) WT_pcmd=atoi(e);
      if((e=getenv("WT_PADDR"))) WT_paddr=(uint32_t)strtoul(e,0,0);
      if((e=getenv("WT_PLEN"))) WT_plen=(uint32_t)strtoul(e,0,0);
      if((e=getenv("WT_GROUP"))) WT_group=atoi(e);
      if((e=getenv("WT_TAPS"))) WT_taps=atoi(e);
      if((e=getenv("WT_RCMD"))) WT_rcmd=atoi(e);
      if((e=getenv("RS_MUT"))) sl_rs_mut=atoi(e);
      if((e=getenv("RSCHAIN"))) RSCHAIN=atoi(e);
      if((e=getenv("RSCTL"))) RSCTL=atoi(e);
      if((e=getenv("PRODMIX"))) PRODMIX=atoi(e);
      WT_dump=getenv("WT_DUMP");
      if(WT_dump && (WT_task<0||WT_cmd<0)){ fprintf(stderr,
          "FATAL: witness dump needs WT_TASK and WT_CMD\n"); return 2; } }
    { const char*cp=getenv("CHRON"); if(cp){ CHRON=fopen(cp,"wb");
        if(!CHRON){fprintf(stderr,"FATAL: cannot open CHRON %s\n",cp);return 2;} } }
    f=fopen(path,"rb"); if(!f){perror(path);return 2;}
    fread(h,1,20,f); nf=be32(h+8); lo=be32(h+12); dsz=be32(h+16);
    bef=malloc(dsz); aft=malloc(dsz); dram=malloc(dsz); instate=malloc(dsz);
    RS_statemask=malloc(dsz); EX_mark=malloc(dsz); RS_stateBase=malloc(dsz*sizeof(uint32_t));
    lastop=malloc(dsz*sizeof(uint16_t)); lastcmd=malloc(dsz*sizeof(uint32_t));
    memset(attrib,0,sizeof attrib); memset(attrib_nv,0,sizeof attrib_nv);
    memset(owned_nv,0,sizeof owned_nv);
    memset(P_tasks,0,sizeof P_tasks); memset(P_diff,0,sizeof P_diff);
    memset(P_own,0,sizeof P_own); memset(P_att,0,sizeof P_att);
    memset(SLOT_lane,0,sizeof SLOT_lane);
    memset(SA_lane,0,sizeof SA_lane);
    for(fi=0;fi<nf;fi++){
        uint8_t fh[12]; uint32_t n,k,*w; int ok=1;
        if(fread(fh,1,12,f)!=12)break; n=be32(fh+8); if(n>4096)break;
        w=malloc(8*n);
        for(k=0;k<n;k++){uint8_t c[8];fread(c,1,8,f);w[2*k]=be32(c);w[2*k+1]=be32(c+4);}
        fread(bef,1,dsz,f); fread(aft,1,dsz,f);
        memcpy(dram,bef,dsz);
        /* overlay this world's own persistent state over the imported image */
        { int did=0;
          for(ws_i=0; ws_i<ws_n; ws_i++){ uint32_t a=ws_addr[ws_i];
              if(a>=lo && a+80<=lo+dsz){ memcpy(dram+(a-lo), ws_data[ws_i], 80);
                                         carried++; did=1; } }
          if(did) carried_tasks++; }
        if(MIXCHAIN){ uint32_t qm; xt_task_begin(&MIX,(int)fi);
            for(qm=0;qm<n;qm++) if(((w[2*qm]>>24)&0xFF)==3){
                uint32_t sa=w[2*qm+1]&0xFFFFFF;
                if(sa>=lo && sa+80<=lo+dsz)
                    xt_consume(&MIX, sa, (int)((w[2*qm]>>16)&1),
                               bef+(sa-lo), dram+(sa-lo)); } }
        if(RSCHAIN){ uint32_t qc; xt_task_begin(&RSX,(int)fi);
            for(qc=0;qc<n;qc++) if(((w[2*qc]>>24)&0xFF)==5){
                uint32_t sa=w[2*qc+1]&0xFFFFFF;
                if(sa>=lo && sa+32<=lo+dsz)
                    xt_consume(&RSX, sa, (int)((w[2*qc]>>16)&1),
                               bef+(sa-lo), dram+(sa-lo)); } }
        sl_acmd_init(&S,dram,lo,dsz); tasks++;
        memset(DW_cmd,0xFF,sizeof DW_cmd); memset(DW_op,0xFF,sizeof DW_op);
        memset(DW_task,0xFF,sizeof DW_task); memset(OR_cmd,0xFF,sizeof OR_cmd);
        memset(OR_op,0xFF,sizeof OR_op); memset(OR_task,0xFF,sizeof OR_task);
        memset(OR_kind,0,sizeof OR_kind); memset(OR_src,0,sizeof OR_src);
        memset(DW_changed,0,sizeof DW_changed);
        TRK_S=&S; TRK_on=1; PD_live=0; sl_rs_mN=0;
        DR_lo=lo; DR_sz=dsz; FW_on=1;
        if(dsz<=(1u<<21)){ memset(FW_task,0xFF,dsz*sizeof(int32_t));
            memset(FW_cmd,0xFF,dsz*sizeof(int32_t)); memset(FW_op,0xFF,dsz);
            memset(FW_base,0,dsz*sizeof(uint32_t)); }
        has_env=has_adp=0;
        for(k=0;k<n;k++){ int o2=(int)((w[2*k]>>24)&0xFF);
            if(o2==3) has_env=1; if(o2==1) has_adp=1; }
        IN13 = (has_env && !has_adp);
        memset(instate,0,dsz);
        memset(lastop,0xFF,dsz*sizeof(uint16_t)); memset(lastcmd,0xFF,dsz*sizeof(uint32_t));
        for(k=0;k<n;k++){ uint32_t a2=w[2*k+1]&0xFFFFFF;
            if(((w[2*k]>>24)&0xFF)==3 && a2>=lo && a2+80<=lo+dsz)
                memset(instate+(a2-lo),1,80); }
        for(k=0;k<n && ok;k++){
            uint32_t w0=w[2*k],w1=w[2*k+1],cmd[2];
            { uint32_t wa,wn; ncmds_total++;
              if(dram_write_range(w0,w1,sl_acmd_sget(&S,0x04),&wa,&wn)
                 && wa>=lo && wa+wn<=lo+dsz){ uint32_t q3;
                  for(q3=0;q3<wn;q3++){ lastop[wa-lo+q3]=(uint16_t)((w0>>24)&0xFF);
                                        lastcmd[wa-lo+q3]=k; } } }
            if(((w0>>24)&0xFF)==3){ uint32_t sa=w1&0xFFFFFF;
                if(!((w0>>16)&1) && sa>=lo && sa+80<=lo+dsz){
                    int z,pt=-1;
                    for(z=0;z<lastN;z++) if(lastA[z]==sa){pt=(int)lastT[z];break;}
                    (void)pt;   /* scoring now lives in the shared xtask
                                 * scorer; this block only maintains lastT/lastA
                                 * for the chronology dump */ }
                { int z,fz=-1; for(z=0;z<lastN;z++) if(lastA[z]==sa){fz=z;break;}
                  if(fz<0&&lastN<64){fz=lastN;lastA[lastN++]=sa;}
                  if(fz>=0) lastT[fz]=fi; }
                { uint32_t hdr[5]; hdr[0]=fi; hdr[1]=k; hdr[2]=(w0>>16)&1;
                  hdr[3]=(uint32_t)(uint16_t)sl_acmd_sget(&S,0x04); hdr[4]=sa;
                  CHRON_hdr[0]=hdr[0];CHRON_hdr[1]=hdr[1];CHRON_hdr[2]=hdr[2];
                  CHRON_hdr[3]=hdr[3];CHRON_hdr[4]=hdr[4]; }
                WT_active = (WT_scan) || ((int)fi==WT_task && (int)k==WT_cmd);
                WT_in0 = sl_acmd_sget(&S,0x00);
                WT_g=0; if(WT_active){ memset(WT_in,0,sizeof WT_in);
                    memset(WT_gain,0,sizeof WT_gain); memset(WT_out,0,sizeof WT_out); }
                if(k<4096){ CMD_w0[k]=w0; CMD_w1[k]=w1;
                    CMD_in[k]=sl_acmd_sget(&S,0x00); CMD_out[k]=sl_acmd_sget(&S,0x02);
                    CMD_cnt[k]=sl_acmd_sget(&S,0x04); }
                TRK_task=(int)fi; TRK_cmd=(int)k;
                if(WT_prov && (int)fi==WT_task && (int)k==WT_pcmd)
                    prov_dump(&S,fi,k,WT_paddr?WT_paddr:sl_acmd_sget(&S,0x00),WT_plen);
                if(PRODMIX){ uint32_t pc[2]; pc[0]=w0; pc[1]=w1;
                    /* route the mixer through the PRODUCTION interpreter, so
                     * the port is scored by the same oracles that accepted the
                     * evaluator rather than by inspection */
                    if(sl_acmd_exec(&S,pc,1)!=SL_ACMD_OK) ok=0; }
                else env_exec(&S,w0,w1,ramp,lo,dsz);
                envn++; trk_flush();
                sl_acmd_wrlog(w1&0xFFFFFF,80,3);
                if(WT_scan==2 && WT_active){
                    /* CENSUS: for every ACTIVE command (>=2 nonzero input lanes
                     * with >=2 distinct gains), tally the last-writer opcode of
                     * its 16 group-0 input bytes. Answers whether an active
                     * witness with grounded input exists AT ALL. */
                    int q,nz=0,dg=0,seen=0; int16_t g0=0;
                    for(q=0;q<8;q++) if(WT_in[q]){ nz++;
                        if(!seen){g0=WT_gain[q];seen=1;} else if(WT_gain[q]!=g0) dg=1; }
                    if(nz>=2 && dg){ uint32_t in0=WT_in0; int mask=0;
                        int q4,wnz=0,dneq=0;
                        for(q4=0;q4<8;q4++){ if(WT_gw[q4]) wnz=1;
                                             if(WT_dst[q4]!=WT_dstw[q4]) dneq=1; }
                        if(wnz) CENS_wetnz++;
                        if(dneq) CENS_dstneq++;
                        if(wnz||dneq) CENS_discrim++;
                        CENS_active++;
                        for(q=0;q<16;q++){ uint32_t ad=in0+q;
                            int32_t oc=OR_cmd[ad];
                            int op=(oc<0)?16:(OR_op[ad]&15);
                            CENS_op[op]++;
                            switch(op){
                            case 4:  mask|=1;                       /* LOADBUFF */
                                     { uint32_t ds=OR_src[ad];
                                       if(ds>=lo && ds<lo+dsz){
                                           if(lastop[ds-lo]==0xFF) CENS_load_taskstart++;
                                           else CENS_load_ourwrite++; }
                                       else CENS_load_offmap++; } break;
                            case 5:  mask|=2;  break;               /* RESAMPLE */
                            case 1:  mask|=4;  break;               /* ADPCM    */
                            case 3:  mask|=8;  break;               /* ENVMIXER */
                            case 16: mask|=32; break;               /* no writer */
                            default: mask|=16; break; } }
                        CENS_mask[mask&63]++; } }
                if((WT_scan==1||WT_scan==3) && WT_active){
                    /* a witness needs GROUNDED nonzero input PCM on at least two
                     * lanes, carrying DIFFERENT envelope gains - a flat envelope
                     * or a single active lane cannot discriminate anything. */
                    int q,nz=0,dg=0; int16_t g0=0; int seen=0;
                    for(q=0;q<8;q++) if(WT_in[q]){ nz++;
                        if(!seen){ g0=WT_gain[q]; seen=1; }
                        else if(WT_gain[q]!=g0) dg=1; }
                    /* WT_SCAN=3 additionally requires the two buses to be
                     * DISTINGUISHABLE: a nonzero wet gain, or destinations that
                     * differ. Without that, swapping the gains and swapping the
                     * destinations are the same operation - measured on the
                     * first witness, where wet gain was 0 on all eight lanes and
                     * both destinations started at 0, and the two controls
                     * scored byte-identically. */
                    { int q2,wnz=0,dneq=0;
                      for(q2=0;q2<8;q2++){ if(WT_gw[q2]) wnz=1;
                                           if(WT_dst[q2]!=WT_dstw[q2]) dneq=1; }
                      if(WT_scan==3 && !(wnz||dneq)) nz=0; }
                    if(nz>=2 && dg){
                        printf("ACTIVE PCM WITNESS selected\n");
                        printf("  task %u  command %u  %s  count %d  state %#08x\n",
                               fi,k,((w0>>16)&1)?"A_INIT":"CONTINUE",
                               (int)(uint16_t)sl_acmd_sget(&S,0x04),w1&0xFFFFFF);
                        printf("  group %d, the only group mutated in either mode\n",WT_group);
                        printf("  lane          0      1      2      3      4      5      6      7\n");
                        printf("  input     ");for(q=0;q<8;q++)printf("%7d",WT_in[q]);printf("\n");
                        printf("  envelope  ");for(q=0;q<8;q++)printf("%7d",WT_env[q]);printf("\n");
                        printf("  dry gain  ");for(q=0;q<8;q++)printf("%7d",WT_gain[q]);printf("\n");
                        printf("  dry gain  ");for(q=0;q<8;q++)printf("%7d",WT_gain[q]);printf("\n");
                        printf("  wet gain  ");for(q=0;q<8;q++)printf("%7d",WT_gw[q]);printf("\n");
                        printf("  dst before(dry)");for(q=0;q<8;q++)printf("%7d",WT_dst[q]);printf("\n");
                        printf("  dst before(wet)");for(q=0;q<8;q++)printf("%7d",WT_dstw[q]);printf("\n");
                        printf("  out after ");for(q=0;q<8;q++)printf("%7d",WT_out[q]);printf("\n");
                        printf("  nonzero input lanes %d, distinct gains among them: yes\n",nz);
                        return 0; } }
                WT_active=0;
                if(CHRON){ uint8_t rec[100]; memset(rec,0,sizeof rec);
                    memcpy(rec,CHRON_hdr,20);
                    if(sa>=lo && sa+80<=lo+dsz) memcpy(rec+20,dram+(sa-lo),80);
                    fwrite(rec,1,100,CHRON); }
                if(sa>=lo && sa+80<=lo+dsz){ int q9,found=-1;
                    for(q9=0;q9<ws_n;q9++) if(ws_addr[q9]==sa){found=q9;break;}
                    if(found<0 && ws_n<WS_MAX){ found=ws_n; ws_addr[ws_n++]=sa; }
                    if(found>=0) memcpy(ws_data[found], dram+(sa-lo), 80); }
                continue; }
            if(WT_prov && (int)fi==WT_task && (int)k==WT_pcmd)
                prov_dump(&S,fi,k,WT_paddr,WT_plen);
            if(((w0>>24)&0xFF)==5){
                int ini=(int)((w0>>16)&1), gq;
                if(ini) GAP_init++; else GAP_cont++;
                if(ini) for(gq=0;gq<4;gq++){
                    uint32_t ad=RS_S7+0x0C+gq; int32_t wc=DW_cmd[ad];
                    if(wc<0)            GAP_cls[0]++;            /* task-start DMEM */
                    else { GAP_cls[1]++; GAP_op[DW_op[ad]&15]++; } /* prior command */
                }
            }
            if(WT_taps && ((w0>>24)&0xFF)==5 && (int)fi==WT_task && (int)k==WT_rcmd){
                /* DERIVED tap addressing, from the handler's own chain:
                 *   acc_n = phase + n*(pitch<<1), Q16
                 *   integer advance ia = acc>>16 ; phase index = (acc&0xFFFF)>>10
                 *   tap j reads buf[ia+j], buf[0..3] = history, buf[4+m] = input[m]
                 * Every referenced SOURCE byte is then resolved through the
                 * write-event tracker - not a guessed input window. */
                uint32_t sa=w1&0xFFFFFF; int ini=(int)((w0>>16)&1);
                uint32_t pitch=(uint32_t)(w0&0xFFFF), in0=sl_acmd_sget(&S,0x00);
                unsigned ph=0; int n,j; int hist_used=0,inp_used=0,unres=0;
                if(!ini && sa>=lo && sa+32<=lo+dsz)
                    ph=(unsigned)((dram[sa-lo+8]<<8)|dram[sa-lo+9]);
                printf("TAP DERIVATION - task %u RESAMPLE cmd %u  %s\n",fi,k,
                       ini?"A_INIT (history and phase forced to zero)":"CONTINUE");
                printf("  state %#08x  pitch %#06x  step %#08x  phase %#06x  in %#05x  count %u\n",
                       sa,pitch,pitch<<1,ph,in0,(unsigned)sl_acmd_sget(&S,0x04));
                printf("  out  acc       ia  idx | tap sources and their ACTUAL writer\n");
                for(n=0;n<8;n++){
                    unsigned acc=ph+(unsigned)n*(pitch<<1);
                    int ia=(int)(acc>>16), idx=(int)((acc&0xFFFF)>>10);
                    printf("  %3d  %08x %3d %4d |",n,acc,ia,idx);
                    for(j=0;j<4;j++){ int jj=ia+j;
                        if(jj<4){ hist_used++; printf(" H%d",jj&3); }
                        else { uint32_t ad=in0+2*(jj-4); inp_used++;
                            if(ad<SL_DMEM_SIZE && DW_cmd[ad]>=0)
                                printf(" %#05x:%s#%d",ad,OPN[DW_op[ad]&15],DW_cmd[ad]);
                            else { printf(" %#05x:UNRESOLVED",ad); unres++; } } }
                    printf("\n"); }
                printf("  tap references: %d from history, %d from current input, "
                       "%d unresolved\n",hist_used,inp_used,unres);
                if(hist_used) printf("  history comes from the 32-byte DRAM state at %#08x, "
                       "written by the PREVIOUS RESAMPLE command's writeback\n",sa);
                printf("\n"); }
            if(((w0>>24)&0xFF)==5){   /* RESAMPLE alignment census */
                uint32_t sa=w1&0xFFFFFF; int ini=(int)((w0>>16)&1);
                RS_n++; if(ini) RS_init++; else RS_cont++;
                if(!ini && sa>=lo && sa+32<=lo+dsz){
                    unsigned ph=(unsigned)((dram[sa-lo+8]<<8)|dram[sa-lo+9]);
                    unsigned pitch=(unsigned)(w0&0xFFFF);
                    if(ph>=RS_phmax) RS_phmax=ph;
                    if(ph==0) RS_ph0++;
                    else { RS_phnz++;          /* exact-tap counted WITHIN non-zero */
                           if((ph&0x3FF)==0) RS_ph_ontap++; else RS_ph_offtap++; }
                    if(pitch==0x8000) RS_unitpitch++;
                    { unsigned in0=(unsigned)sl_acmd_sget(&S,0x00);
                      if(in0&15) RS_inmisal++; else RS_inalign++;
                      { unsigned f=(unsigned)(( (unsigned)((dram[sa-lo+8]<<8)|dram[sa-lo+9]) ) );
                        (void)f; } }
                    { unsigned cnt=(unsigned)sl_acmd_sget(&S,0x04);
                      if(cnt%32) RS_cnt_odd++; }
                    if(RS_show<6 && ph){ printf("   RESAMPLE task %u cmd %u  pitch %#06x "
                        "phase %#06x (tap idx %u, frac %u)  count %u\n",
                        fi,k,pitch,ph,(ph>>10)&63,ph&0x3FF,
                        (unsigned)sl_acmd_sget(&S,0x04)); RS_show++; } } }
            cmd[0]=w0;cmd[1]=w1;
            if(k<4096){ CMD_w0[k]=w0; CMD_w1[k]=w1;
                CMD_in[k]=sl_acmd_sget(&S,0x00); CMD_out[k]=sl_acmd_sget(&S,0x02);
                CMD_cnt[k]=sl_acmd_sget(&S,0x04); }
            TRK_task=(int)fi; TRK_cmd=(int)k;
            if(sl_acmd_exec(&S,cmd,1)!=SL_ACMD_OK) ok=0;
            trk_flush();
        }
        if(WT_dump && (int)fi==WT_task){
            FILE*o=fopen(WT_dump,"wb");
            if(!o){fprintf(stderr,"FATAL: cannot write %s\n",WT_dump);return 2;}
            fwrite(&lo,4,1,o); fwrite(&dsz,4,1,o);
            fwrite(dram,1,dsz,o); fwrite(aft,1,dsz,o);
            /* the ACTUAL final DRAM writer per byte, so the chain from the
             * mixer's emission through the movers into the save can be read
             * off rather than assumed */
            fwrite(FW_op,1,dsz,o); fwrite(FW_cmd,sizeof(int32_t),dsz,o);
            fclose(o);
            fprintf(stderr,"witness dump: task %u mode %d -> %s\n",fi,WT_mode,WT_dump);
            return 0; }
        { uint32_t qe; memset(EX_mark,0,dsz);
          for(qe=0;qe<n;qe++) if(((w[2*qe]>>24)&0xFF)==5){
              uint32_t sa=w[2*qe+1]&0xFFFFFF;
              if(sa>=lo && sa+32<=lo+dsz) memset(EX_mark+(sa-lo),1,32); }
          for(qe=0;qe<dsz;qe++) if(EX_mark[qe]){ EX_bytes++;
              if(FW_cmd[qe]>=0) EX_actual++; else EX_noactual++;
              if(dram[qe]!=bef[qe]) EX_valchg++; else EX_valsame++; } }
        { int q7; for(q7=0;q7<sl_rs_mN;q7++){ MIS_hist[0][sl_rs_mV[q7]&15]++;
            ALN_all[sl_rs_mR[q7]&15]++; QAL_all[(sl_rs_mR[q7]-8)&15]++; } }
        if(MIXCHAIN){ uint32_t qm;
            for(qm=0;qm<n;qm++) if(((w[2*qm]>>24)&0xFF)==3){
                uint32_t sa=w[2*qm+1]&0xFFFFFF;
                if(sa>=lo && sa+80<=lo+dsz) xt_save(&MIX, sa, dram+(sa-lo), (int)fi); } }
        if(RSCHAIN){ uint32_t qc;
            for(qc=0;qc<n;qc++) if(((w[2*qc]>>24)&0xFF)==5){
                uint32_t sa=w[2*qc+1]&0xFFFFFF;
                if(sa>=lo && sa+32<=lo+dsz)
                    xt_save(&RSX, sa, dram+(sa-lo), (int)fi); }
            /* Injection runs only AFTER every save for the task. Interleaving
             * them let a later command's save overwrite the injected block, so
             * the control applied and then undid itself - vacuous, and the
             * "1 of 1 consumed" counter still read as if it had fired. */
            if(RSCTL==1){
                if(RSX.edges>2 && RSC_ptask<0) RSC_ptask=(int)fi;
                if(RSC_ptask==(int)fi)
                    for(qc=0;qc<n;qc++) if(((w[2*qc]>>24)&0xFF)==5){
                        uint32_t sa=w[2*qc+1]&0xFFFFFF;
                        if(sa>=lo && sa+32<=lo+dsz) xt_inject(&RSX,sa,0,0xFF); } } }
        { uint32_t q2,q4; memset(RS_statemask,0,dsz);
          for(q2=0;q2<n;q2++) if(((w[2*q2]>>24)&0xFF)==5){
              uint32_t sa=w[2*q2+1]&0xFFFFFF;
              if(sa>=lo && sa+32<=lo+dsz){ memset(RS_statemask+(sa-lo),1,32);
                  for(q4=0;q4<32;q4++) RS_stateBase[sa-lo+q4]=sa; } } }
        { uint32_t q2; for(q2=0;q2<dsz;q2++){
            if(RS_statemask[q2]){ RSC_state_tot++; if(dram[q2]==aft[q2]) RSC_state_match++; }
            else { RSC_consumer_tot++;
                   if(dram[q2]!=aft[q2]){
                       /* moved by the mutation. NON-VACUITY: did the cartridge
                        * itself write anything there, or is it a byte the
                        * cartridge never touched? */
                       if(aft[q2]!=bef[q2]) DPCM_live++; else DPCM_dead++;
                       /* COMPOSED VOICE: the byte must have travelled the whole
                        * chain, so its actual final DRAM writer must be the
                        * SAVE. Bytes moved by the decoder but written by
                        * something else did not go through the mixer's output
                        * path and are counted separately rather than folded in. */
                       { int fo=(FW_cmd[q2]>=0)?(FW_op[q2]&15):17;
                         if(fo==6){ DPCM_save++;
                             /* non-vacuity for the composed-voice population
                              * specifically, not for all moved bytes */
                             if(aft[q2]!=bef[q2]) DPCM_save_live++; } 
                         else DPCM_notsave[fo>16?16:fo]++; } }
                   if(dram[q2]==aft[q2]) RSC_consumer_match++;
                   else if(RSC_first_task<0){ RSC_first_task=(int)fi;
                                              RSC_first_addr=q2+lo; } } } }
        { uint32_t q2; int only_ung=1, any=0;
          for(q2=0;q2<dsz;q2++) if(dram[q2]!=aft[q2]){ any=1;
              /* is this byte one of the explicitly UNGROUNDED resampler state
               * bytes, or does the task differ for some other reason? */
              int off = RS_statemask[q2] ? (int)(q2+lo-RS_stateBase[q2]) : -1;
              if(!((off>=0x0C&&off<=0x0F)||off==0x10||off==0x11)) only_ung=0; }
          if(!any) TASK_exact++;
          else if(only_ung) TASK_ung_only++;
          else TASK_other++; }
        { uint32_t q2; for(q2=0;q2<dsz;q2++) if(dram[q2]!=aft[q2]){
            int op2=(FW_cmd[q2]<0)?17:(FW_op[q2]&15);
            FW_mis[op2]++;
            if(FW_cmd[q2]<0) FW_nowriter++;
            else if(op2==5){
                /* offset must come from the STATE BLOCK base, not the WRLOG
                 * call's base - the writeback logs two disjoint ranges, so the
                 * second call's base is block+16 and would report 0-based */
                int off = RS_statemask[q2] ? (int)(q2+lo-RS_stateBase[q2]) : -1;
                if(off>=0&&off<32) FW_rsoff[off]++; else FW_rsoob++;
                if(off==0x10||off==0x11){   /* localise the residual */
                    uint32_t sb=RS_stateBase[q2], q5; int found=0;
                    for(q5=0;q5<n;q5++) if(((w[2*q5]>>24)&0xFF)==5 &&
                                           (w[2*q5+1]&0xFFFFFF)==sb){
                        int ini=(int)((w[2*q5]>>16)&1);
                        if(ini) RES_init++; else RES_cont++;
                        found=1; }
                    { int q6=-1,qq;   /* the LAST record for this state block is
                                       * the one whose writeback the cartridge's
                                       * after-image reflects, not the first */
                      for(qq=0;qq<sl_rs_mN;qq++) if(sl_rs_mA[qq]==sb) q6=qq;
                      if(q6>=0){
                        MIS_hist[1][sl_rs_mV[q6]&15]++;
                        if(off==0x10 && MAP_n<400){
                            /* MEASURE the whole 16-byte mapping, not just byte 0:
                             * for each candidate shift d, how many of the 16
                             * expected bytes match window[row+d+i]? A shifted
                             * block scores 16 at one d; an unshifted block with
                             * a different first halfword scores 14 at d=0. */
                            int d,best=-99,bestn=-1; uint32_t sb2=sb;
                            for(d=-8;d<=8;d++){ int n2=0,i2;
                                for(i2=0;i2<16;i2++){ int wi=64+d+i2;
                                    if(wi<0||wi>=128) break;
                                    if(sl_rs_win[q6][wi]==aft[(sb2-lo)+0x10+i2]) n2++; }
                                if(n2>bestn){ bestn=n2; best=d; } }
                            MAP_best[best+8]++; MAP_n++;
                            /* Is the cartridge's value simply the PRESERVED
                             * before-image, i.e. those two bytes were never
                             * written at all? That is a different claim from
                             * "loaded from row-1" and is cheap to separate. */
                            { uint32_t bi=(sb-lo)+0x10;
                              if(aft[bi]==bef[bi] && aft[bi+1]==bef[bi+1]) PRES_both++;
                              else if(aft[bi]==bef[bi] || aft[bi+1]==bef[bi+1]) PRES_one++;
                              else PRES_none++; }
                            ALN_fail[sl_rs_mR[q6]&15]++;
                            QAL_fail[(sl_rs_mR[q6]-8)&15]++;
                            if(bestn==16) MAP_full++;
                            { int i3,z=0; for(i3=0;i3<16;i3++)
                                if(sl_rs_win[q6][64+i3]==aft[(sb2-lo)+0x10+i3]) z++;
                              MAP_d0[z]++; } }
                        if(off==0x10){   /* where does the cartridge value live? */
                            int w2,hit=-1;
                            uint8_t e0=aft[q2], e1=aft[q2+1];
                            for(w2=0;w2+1<128;w2++)
                                if(sl_rs_win[q6][w2]==e0 && sl_rs_win[q6][w2+1]==e1){
                                    hit=w2-64; break; }
                            if(hit>=-64&&hit<=64) SRC_off[hit+64]++;
                            if(SRC_shown<4){ SRC_shown++;
                                printf("    residual case: state %#08x mis %d fa %d row %#05x "
                                       "cartridge %02x%02x found at row%+d\n",
                                       sb,sl_rs_mV[q6],sl_rs_mF[q6],sl_rs_mR[q6],e0,e1,hit); } }
                      } }
                    if(!found) RES_nocmd++; } }
            else FW_other_writer++;
            if(FW_cmd[q2]<0){                     /* unwritten: offset from the
                                                   * state block that contains it */
                if(RS_statemask[q2]){ int off=(int)(q2+lo-RS_stateBase[q2]);
                    if(off>=0&&off<32) FW_unwr_off[off]++; else FW_unwr_oob++; }
                else FW_unwr_nostate++; } } }
        if(ok){ uint32_t q; long long td=0,tu=0; done++;
            for(q=0;q<dsz;q++) if(dram[q]!=aft[q]){ td++; if(dram[q]==bef[q]) tu++; }
            { int cls=(has_env?2:0)|(has_adp?1:0); cls_n[cls]++; if(td==0) cls_ok[cls]++;
              { uint32_t r2; int pc=(cls==0)?0:(cls==2?2:3); P_tasks[pc]++;
                if(pc==2) for(r2=0;r2<dsz;r2++) if(lastop[r2]==3){
                    uint32_t ci=lastcmd[r2];
                    if(ci<n){ uint32_t st3=w[2*ci+1]&0xFFFFFF;
                      if(r2+lo>=st3 && r2+lo<st3+80){
                        int off=(int)(r2+lo-st3), sl=off/16, ln=(off%16)/2;
                        SLOT_obs[sl]++;
                        if(dram[r2]!=aft[r2]){ SLOT_mis[sl]++; SLOT_lane[sl][ln]++;
                            if((w[2*ci]>>16)&1) SLOT_init[sl]++; else SLOT_cont[sl]++; } } } }
                if(has_env){ SA_tasks++;
                  for(r2=0;r2<dsz;r2++) if(lastop[r2]==3){
                    uint32_t ci=lastcmd[r2];
                    if(ci<n){ uint32_t st3=w[2*ci+1]&0xFFFFFF;
                      if(r2+lo>=st3 && r2+lo<st3+80){
                        int off=(int)(r2+lo-st3), sl=off/16, ln=(off%16)/2;
                        SA_obs[sl]++;
                        if(dram[r2]!=aft[r2]){ SA_mis[sl]++; SA_lane[sl][ln]++;
                            if((w[2*ci]>>16)&1) SA_init[sl]++; else SA_cont[sl]++; } } } } }
                if(pc==2) for(r2=0;r2<dsz;r2++) if(lastop[r2]==6){
                    int cc=(aft[r2]!=bef[r2]), ec=(dram[r2]!=bef[r2]);
                    sv13_total++;
                    if(cc) sv13_cart_chg++; if(ec) sv13_ev_chg++;
                    if(cc&&ec&&dram[r2]==aft[r2]) sv13_both_agree++;
                    if(!cc&&!ec) sv13_both_unchanged++;
                    if(dram[r2]!=aft[r2]) sv13_mismatch++; }
                if(pc==3) for(r2=0;r2<dsz;r2++) if(lastop[r2]==1){
                    ad_total++;
                    if(aft[r2]!=bef[r2]) ad_cart_chg++; else ad_unchanged++;
                    if(dram[r2]!=aft[r2]) ad_mismatch++; }
                for(r2=0;r2<dsz;r2++){ int l4=lastop[r2];
                    if(l4>=0&&l4<16){ P_own[pc][l4]++; if(dram[r2]!=aft[r2]) P_att[pc][l4]++; }
                    if(dram[r2]!=aft[r2]) P_diff[pc]++; }
                /* byte-level init coverage, all-opcode final writer */
                for(k=0;k<n;k++){ uint32_t w0b=w[2*k],w1b=w[2*k+1];
                    if(((w0b>>24)&0xFF)!=3 || !((w0b>>16)&1)) continue;
                    { uint32_t st2=w1b&0xFFFFFF; int q7;
                      if(st2<lo||st2+80>lo+dsz) continue; init_cmds++;
                      for(q7=0;q7<80;q7++){ uint32_t idx=st2-lo+q7;
                          if(lastop[idx]==3 && lastcmd[idx]==k){ init_bytes_final++;
                              if(dram[idx]!=aft[idx]) init_bytes_mismatch++; } } } } }
              if(cls==0 && td!=0 && nov<9){   /* no-voice task that still fails */
                uint32_t r; nov++;
                printf("  NO-VOICE FAILURE task %3u: %lld differing bytes\n",fi,td);
                for(r=0;r<dsz;r++) if(dram[r]!=aft[r]){
                    printf("     first diff addr %#08x  expected %#04x  got %#04x  before %#04x  %s\n",
                           lo+r,aft[r],dram[r],bef[r],
                           (dram[r]==bef[r])?"UNWRITTEN by us":"we wrote it wrong");
                    break; } } }
            if(td==0) exact_tasks++;
            else if(firstbad<0){ firstbad=(int)fi; firstbad_diff=td; firstbad_unwr=tu;
                /* characterise the first differing byte, and the first unwritten run */
                for(q=0;q<dsz;q++) if(dram[q]!=aft[q]){ fb_addr=lo+q;
                    fb_exp=aft[q]; fb_got=dram[q]; fb_bef=bef[q]; break; }
                for(q=0;q<dsz;q++) if(dram[q]!=aft[q]&&dram[q]==bef[q]){
                    uint32_t r=q; while(r<dsz&&dram[r]!=aft[r]&&dram[r]==bef[r]) r++;
                    fu_addr=lo+q; fu_len=r-q; break; } }
            if(!has_env && !has_adp) for(q=0;q<dsz;q++){ int l3=lastop[q];
                if(l3>=0&&l3<16) owned_nv[l3]++; }
            for(q=0;q<dsz;q++){ cmp++; if(dram[q]!=aft[q]){ mis++;
                { int lo2=lastop[q]; if(lo2>=0&&lo2<16){ attrib[lo2]++;
                      if(!has_env && !has_adp) attrib_nv[lo2]++; }
                  if(lo2==3){ uint32_t ci=lastcmd[q];
                      if(ci<n && ((w[2*ci]>>16)&1)) a_init++; else a_cont++; }
                  if(firstdiv_task<0 || (int)fi<firstdiv_task ||
                     ((int)fi==firstdiv_task && (int)lastcmd[q]<firstdiv_cmd)){
                      if(firstdiv_task<0 || (int)fi==firstdiv_task){
                        firstdiv_task=(int)fi; firstdiv_cmd=(int)lastcmd[q];
                        firstdiv_op=lo2; firstdiv_addr=lo+q; } } }
                if(instate[q]) mis_state++; else mis_out++;
                if(dram[q]!=bef[q]) mis_wewrote++; else mis_wemissed++; } }
            for(q=0;q<dsz;q++) if(dram[q]!=bef[q]) wrote++; }
        free(w);
    }
    printf("Full ENVMIXER evaluator vs cartridge after-image\n\n");
    printf("  tasks walked                : %lld\n",tasks);
    printf("  tasks executed to completion: %lld\n",done);
    printf("  ENVMIXER commands executed  : %lld\n",envn);
    printf("  DRAM bytes compared         : %lld\n",cmp);
    printf("  DRAM bytes differing        : %lld\n",mis);
    printf("  ungrounded negative-branch takings: %lld\n",NEG_UNGROUNDED);
    printf("  HYBRID: distinct persistent-state blocks carried : %d\n",ws_n);
    printf("          block-overlays applied at boundaries     : %lld over %lld tasks\n",
           carried,carried_tasks);
    if(CHRON){ fclose(CHRON); CHRON=0; }
    if(WT_scan==2){ int q; long long tot=0;
        printf("ACTIVE-WITNESS INPUT PROVENANCE CENSUS\n");
        printf("  ENVMIXER commands qualifying as ACTIVE (>=2 nonzero lanes,\n");
        printf("  >=2 distinct gains)                                  : %lld\n",CENS_active);
        for(q=0;q<17;q++) tot+=CENS_op[q];
        printf("  last-writer opcode of their group-0 input bytes (%lld bytes):\n",tot);
        for(q=0;q<17;q++) if(CENS_op[q]) printf("      %-12s %lld\n",
            (q==16)?"(task-start)":OPN[q],CENS_op[q]);
        printf("  of those ACTIVE commands, how many can DISTINGUISH a gain swap\n");
        printf("  from a destination swap (needs nonzero wet gain, or destinations\n");
        printf("  that differ - otherwise the two mutations are the same operation):\n");
        printf("      nonzero wet gain on some lane   : %lld\n",CENS_wetnz);
        printf("      dry/wet destinations differ     : %lld\n",CENS_dstneq);
        printf("      DISCRIMINATING (either)         : %lld of %lld\n",
               CENS_discrim,CENS_active);
        printf("\n  commands by COMPLETE provenance class of their consumed input:\n");
        { int m; const char*nm[6]={"LOADBUFF","RESAMPLE","ADPCM","ENVMIXER","other","none"};
          long long pure_load=0;
          for(m=0;m<64;m++) if(CENS_mask[m]){
            int b2,first=1; printf("      ");
            for(b2=0;b2<6;b2++) if(m&(1<<b2)){ printf("%s%s",first?"":"+",nm[b2]); first=0; }
            printf("%*s %lld%s\n",(int)(34-0),"",CENS_mask[m],
                   (m==1)?"   <- fully load-groundable":"");
            if(m==1) pure_load=CENS_mask[m]; }
          printf("  fully load-groundable active commands : %lld\n",pure_load); }
        printf("  LOADBUFF-origin bytes, DRAM source status:\n");
        printf("      task-start DRAM (in the capture before-image) : %lld\n",CENS_load_taskstart);
        printf("      DRAM we wrote earlier in the task             : %lld\n",CENS_load_ourwrite);
        printf("      outside the mapped window                     : %lld\n\n",CENS_load_offmap); }
    { int q3; long long tot=0;
      printf("\nBASELINE MISMATCHES BY ACTUAL FINAL DRAM WRITER (physical writes)\n");
      for(q3=0;q3<18;q3++) tot+=FW_mis[q3];
      printf("  total baseline mismatching bytes : %lld\n",tot);
      for(q3=0;q3<18;q3++) if(FW_mis[q3]) printf("      %-12s %lld\n",
          (q3==17)?"(no writer)":OPN[q3&15],FW_mis[q3]);
      printf("  bytes with a LATER non-resampler physical writer : %lld\n",FW_other_writer);
      printf("  bytes with NO physical writer at all            : %lld\n",FW_nowriter);
      printf("  resampler-written mismatches by state offset:\n");
      for(q3=0;q3<32;q3++) if(FW_rsoff[q3]) printf("      +0x%02X  %lld\n",q3,FW_rsoff[q3]);
      if(FW_rsoob) printf("      outside 0..31 : %lld\n",FW_rsoob);
      printf("  EXPECTED-WRITER MODEL (outbound state DMA covers all 32 bytes)\n");
      printf("    bytes the DMA is expected to write     : %lld\n",EX_bytes);
      printf("      of those, interpreter actually wrote : %lld\n",EX_actual);
      printf("      of those, interpreter never wrote    : %lld\n",EX_noactual);
      printf("      value CHANGED vs before-image        : %lld\n",EX_valchg);
      printf("      value UNCHANGED (DMA still expected) : %lld\n",EX_valsame);
      { int qg; const char*CN[2]={"task-start DMEM (no writer this task)",
                                  "written by a prior command this task"};
        printf("  GAP BYTES +0x0C..0F at DMEM %#05x..%#05x, %lld A_INIT commands\n",
               RS_S7+0x0C,RS_S7+0x0F,GAP_init);
        for(qg=0;qg<2;qg++) if(GAP_cls[qg]) printf("      %-38s %lld\n",CN[qg],GAP_cls[qg]);
        for(qg=0;qg<17;qg++) if(GAP_op[qg]) printf("        by %-11s %lld\n",
            (qg==16)?"(none)":OPN[qg&15],GAP_op[qg]); }
      if(MIXCHAIN) xt_report(&MIX);
      if(RSCHAIN) xt_report(&RSX);
      printf("  IS THE CARTRIDGE VALUE JUST THE PRESERVED BEFORE-IMAGE?\n");
      printf("    both bytes equal before-image (never written) : %lld\n",PRES_both);
      printf("    one byte equal                               : %lld\n",PRES_one);
      printf("    neither equal (genuinely written)             : %lld\n",PRES_none);
      { int qa; printf("  ADDRESS ALIGNMENT CONTRAST (empirical, anchored on the exact quad)\n");
        printf("    row & 15, all commands :");
        for(qa=0;qa<16;qa++) if(ALN_all[qa]) printf(" %d:%lld",qa,ALN_all[qa]);
        printf("\n    row & 15, FAILING     :");
        for(qa=0;qa<16;qa++) if(ALN_fail[qa]) printf(" %d:%lld",qa,ALN_fail[qa]);
        printf("\n    quad addr & 15, all   :");
        for(qa=0;qa<16;qa++) if(QAL_all[qa]) printf(" %d:%lld",qa,QAL_all[qa]);
        printf("\n    quad addr & 15, FAIL  :");
        for(qa=0;qa<16;qa++) if(QAL_fail[qa]) printf(" %d:%lld",qa,QAL_fail[qa]);
        printf("\n"); }
      { int qm; printf("  16-BYTE BLOCK MAPPING in failing cases (%lld sampled)\n",MAP_n);
        printf("    best whole-block shift d :");
        for(qm=0;qm<17;qm++) if(MAP_best[qm]) printf(" %+d:%lld",qm-8,MAP_best[qm]);
        printf("\n    cases where some shift matches all 16 : %lld\n",MAP_full);
        printf("    bytes matching at d=0 (unshifted)      :");
        for(qm=0;qm<17;qm++) if(MAP_d0[qm]) printf(" %d:%lld",qm,MAP_d0[qm]);
        printf("\n"); }
      { int q9; printf("  where the cartridge +0x10 halfword lives, offset from row:\n   ");
        for(q9=0;q9<129;q9++) if(SRC_off[q9]) printf(" %+d:%lld",q9-64,SRC_off[q9]);
        printf("\n"); }
      { int q8; printf("  mis distribution:\n    all cmds :");
        for(q8=0;q8<16;q8+=2) printf("  %2d:%lld",q8,MIS_hist[0][q8]);
        printf("\n    residual :");
        for(q8=0;q8<16;q8+=2) printf("  %2d:%lld",q8,MIS_hist[1][q8]); printf("\n"); }
      printf("  +0x10..11 residual owners: A_INIT %lld, CONTINUE %lld, unmatched %lld\n",
             RES_init,RES_cont,RES_nocmd);
      printf("  UNWRITTEN mismatches, by offset within the containing state block:\n");
      for(q3=0;q3<32;q3++) if(FW_unwr_off[q3]) printf("      +0x%02X  %lld\n",q3,FW_unwr_off[q3]);
      printf("      not inside any resampler state block : %lld\n",FW_unwr_nostate);
      if(FW_unwr_oob) printf("      offset out of range : %lld\n",FW_unwr_oob); }
    printf("\n435-TASK FULL-DRAM STATUS\n");
    printf("    tasks byte-exact on full DRAM              : %lld\n",TASK_exact);
    printf("    tasks differing ONLY in ungrounded bytes   : %lld\n",TASK_ung_only);
    printf("    tasks differing for ANY OTHER reason       : %lld\n",TASK_other);
    printf("\nUNGROUNDED-PATH COUNTERS (production interpreter)\n");
    printf("    low-result negative overflow : %lu\n", sl_acmd_ungrounded_neg);
    printf("    RESAMPLE A_INIT scratch path : %lu\n", sl_acmd_ungrounded_rsinit);
    printf("    resampler state +0x10..11    : %lu\n", sl_acmd_ungrounded_state);
    printf("\nRESAMPLER CHALLENGE SCORING  [sl_rs_mut = %d]\n",sl_rs_mut);
    if(sl_rs_mut) printf("  mutation-moved consumer bytes: %lld the cartridge also "
                         "wrote, %lld it never touched\n",DPCM_live,DPCM_dead);
    if(sl_rs_mut==12){ int qv;
      printf("  COMPOSED VOICE (decoder -> resampler -> mixer -> save)\n");
      printf("    decoder-sensitive bytes whose final writer is SAVEBUFF : %lld\n",
             DPCM_save);
      printf("      of those, bytes the cartridge also wrote (non-vacuous) : %lld\n",
             DPCM_save_live);
      for(qv=0;qv<18;qv++) if(DPCM_notsave[qv])
          printf("    ... final writer %-10s (NOT the save path)  : %lld\n",
                 (qv>=16)?"(none)":OPN[qv&15],DPCM_notsave[qv]); }
    printf("  consumer bytes (outside every RESAMPLE state block)\n");
    printf("      matching the cartridge : %lld of %lld\n",RSC_consumer_match,RSC_consumer_tot);
    printf("  resampler state bytes (its own bookkeeping)\n");
    printf("      matching the cartridge : %lld of %lld\n",RSC_state_match,RSC_state_tot);
    printf("  writeback quad source: %lld references from HISTORY, %lld from INPUT\n",
           sl_rs_wb_hist, sl_rs_wb_input);
    printf("  eligible commands %lld   mutated %lld", sl_rs_elig, sl_rs_applied);
    if(sl_rs_taps_seen) printf("   tap refs whose SOURCE moved %lld of %lld",
                               sl_rs_taps_moved, sl_rs_taps_seen);
    printf("\n");
    if(RSC_first_task>=0) printf("  first divergence: task %d addr %#08x\n",
                                 RSC_first_task,RSC_first_addr);
    else printf("  first divergence: none\n");
    printf("\nRESAMPLE ALIGNMENT CENSUS (what the capture actually exercises)\n");
    printf("  commands %lld   A_INIT %lld   CONTINUE %lld\n",RS_n,RS_init,RS_cont);
    printf("  incoming phase ZERO                          : %lld\n",RS_ph0);
    printf("  incoming phase NON-ZERO                      : %lld  of %lld\n",RS_phnz,RS_cont);
    printf("    of those, exactly on a tap boundary        : %lld\n",RS_ph_ontap);
    printf("    of those, with a fractional remainder      : %lld\n",RS_ph_offtap);
    printf("  => non-zero FRACTIONAL phase                 : %lld of %lld\n",
           RS_ph_offtap,RS_cont);
    printf("  cross-check zero+nonzero == continuations    : %s\n",
           (RS_ph0+RS_phnz==RS_cont)?"yes":"NO - counters disagree");
    printf("  cross-check ontap+fractional == nonzero      : %s\n",
           (RS_ph_ontap+RS_ph_offtap==RS_phnz)?"yes":"NO - counters disagree");
    printf("  largest incoming phase observed             : %#06x\n",RS_phmax);
    printf("  continuations at unit pitch (0x8000)        : %lld\n",RS_unitpitch);
    printf("  continuations whose count is not a multiple of 32 : %lld\n",RS_cnt_odd);
    printf("  input base S_IN 16-byte aligned %lld, MISALIGNED %lld  (%.2f%%)\n\n",
           RS_inalign,RS_inmisal,
           (RS_inalign+RS_inmisal)?100.0*RS_inmisal/(RS_inalign+RS_inmisal):0.0);
    printf("  DIRECT BOUNDARY ORACLE on the FULL evaluator:\n");
    printf("    cross-task consumers scored : %lld   bytes %lld\n",MIX.edges,MIX.bytes);
    printf("    MISMATCHES vs cartridge     : %lld\n",MIX.mis);
    if(MIX.mis) printf("    first mismatch at task %d byte +0x%02X\n",
                       MIX.first_task,MIX.first_byte);
    if(!MIX.edges){ fprintf(stderr,"FATAL: boundary oracle scored nothing\n"); return 5; }
    printf("\n");
    printf("  mismatches inside ENVMIXER 80-byte state blocks : %lld\n",mis_state);
    printf("  mismatches elsewhere (output/PCM buffers)       : %lld\n",mis_out);
    printf("  of all mismatches: we wrote a wrong value       : %lld\n",mis_wewrote);
    printf("                     we left it unwritten         : %lld\n",mis_wemissed);
    printf("  total bytes we modified                         : %lld\n\n",wrote);
    { const char*N[16]={"SPNOOP","ADPCM","CLEARBUFF","ENVMIXER","LOADBUFF","RESAMPLE",
        "SAVEBUFF","SEGMENT","SETBUFF","SETVOL","DMEMMOVE","LOADADPCM","MIXER",
        "INTERLEAVE","POLEF","SETLOOP"}; int q4;
      printf("  differing bytes attributed to their LAST WRITER:\n");
      for(q4=0;q4<16;q4++) if(attrib[q4])
        printf("    %-11s %lld\n",N[q4],attrib[q4]);
      printf("    ENVMIXER split: A_INIT %lld  CONTINUE %lld\n",a_init,a_cont);
      printf("\n  CASCADE DISCRIMINATOR - the same attribution restricted to the\n");
      printf("  118 tasks containing NEITHER voice opcode:\n");
      { int q5; long long tot=0;
        for(q5=0;q5<16;q5++) if(attrib_nv[q5]){ printf("    %-11s %lld\n",N[q5],attrib_nv[q5]); tot+=attrib_nv[q5]; }
        if(!tot) printf("    (none)\n"); }
      printf("\n  VACUITY CHECK - bytes each opcode OWNS as last writer in those\n");
      printf("  same 118 tasks, differing or not (zero owned = zero scored):\n");
      { int q6; for(q6=0;q6<16;q6++) if(owned_nv[q6])
          printf("    %-11s owns %8lld   differing %lld\n",N[q6],owned_nv[q6],attrib_nv[q6]); }
printf("\n  SEMANTIC VACUITY CHECK - SAVEBUFF final-writer bytes in the 13\n");
      printf("  mixer-only tasks. A large denominator is worthless if the\n");
      printf("  cartridge never changed these bytes:\n");
      printf("    total final-writer bytes      : %lld\n",sv13_total);
      printf("    cartridge CHANGED (after!=before) : %lld\n",sv13_cart_chg);
      printf("    evaluator CHANGED                 : %lld\n",sv13_ev_chg);
      printf("    both changed AND agreeing         : %lld\n",sv13_both_agree);
      printf("    unchanged on BOTH sides           : %lld\n",sv13_both_unchanged);
      printf("    mismatches                        : %lld\n",sv13_mismatch);
      printf("\n  Same check on ADPCM state bytes in the 304 mixed tasks:\n");
      printf("    total %lld  cartridge-changed %lld  unchanged %lld  mismatches %lld\n",
             ad_total,ad_cart_chg,ad_unchanged,ad_mismatch);
printf("\n  MUTATION HIT COUNT AT THE SOURCE (13 mixer-only tasks only):\n");
      printf("    mixer commands reached : %lld\n",MH_cmds);
      printf("    groups reached         : %lld\n",MH_groups);
      printf("    commands: init %lld  continuation %lld\n",MH_init_cmds,MH_cont_cmds);
      printf("    groups: peeled-init %lld  common-loop %lld  total %lld\n",
             MH_g_peel,MH_g_loop,MH_g_peel+MH_g_loop);
      printf("    DENOMINATOR: groups*8 = %lld   instrumented input samples = %lld   %s\n",
             (MH_g_peel+MH_g_loop)*8, MH_in_tot+MH_in_tot_peel,
             ((MH_g_peel+MH_g_loop)*8==MH_in_tot+MH_in_tot_peel)?"RECONCILED":"MISMATCH");
      printf("      peeled-init  samples %lld nonzero %lld\n",MH_in_tot_peel,MH_in_nz_peel);
      printf("      common-loop  samples %lld nonzero %lld\n",MH_in_tot,MH_in_nz);
      printf("      COMBINED     samples %lld nonzero %lld\n",
             MH_in_tot+MH_in_tot_peel, MH_in_nz+MH_in_nz_peel);
      { int b; printf("    destination-before, per bus:\n");
        for(b=0;b<4;b++) printf("      %-10s examined %8lld  nonzero %lld\n",BUSN[b],DST_tot[b],DST_nz[b]);
        printf("    mutation-altered samples, per bus:\n");
        for(b=0;b<4;b++) printf("      %-10s %lld\n",BUSN[b],MH_bus[b]); }
      printf("    of %lld commands: dry gain == wet gain in %lld,"
             " left envelope flat across all 8 lanes in %lld\n",MH_cmds,MH_dryeqwet,MH_flatL);
      printf("\n  THE 380, PARTITIONED BY STATE SLOT (13 mixer-only tasks)\n");
      printf("    every slot reports its observable denominator:\n");
      { int sl,ln; for(sl=0;sl<5;sl++){
          printf("      %-12s observable %5lld  mismatches %5lld   init %lld cont %lld\n",
                 SLOTN[sl],SLOT_obs[sl],SLOT_mis[sl],SLOT_init[sl],SLOT_cont[sl]);
          if(SLOT_mis[sl]){ printf("          by lane:");
            for(ln=0;ln<8;ln++) printf(" %lld",SLOT_lane[sl][ln]); printf("\n"); } } }
    { int sl,ln; const char*SN[5]={"hiL","loL","hiR","loR","par"};
      printf("\n  SAME PARTITION over EVERY task containing ENVMIXER (%lld tasks)\n",SA_tasks);
      printf("  - the partition above sees only ENVMIXER-ONLY tasks and therefore\n");
      printf("    never scored the mixed tasks where the first divergence lives.\n");
      for(sl=0;sl<5;sl++){
        printf("    %-3s  observed %8lld  differing %8lld   (init %lld, cont %lld)\n",
               SN[sl],SA_obs[sl],SA_mis[sl],SA_init[sl],SA_cont[sl]);
        if(SA_mis[sl]){ printf("          by lane:");
          for(ln=0;ln<8;ln++) printf(" %lld",SA_lane[sl][ln]); printf("\n"); } } }
      printf("\n  THREE-WAY POPULATION SPLIT\n");
      { const char*PN[4]={"neither voice opcode","","ENVMIXER only","ADPCM + ENVMIXER"};
        int pc,q8; for(pc=0;pc<4;pc++){ if(pc==1) continue;
          printf("    %-20s tasks %3lld  differing %8lld\n",PN[pc],P_tasks[pc],P_diff[pc]);
          for(q8=0;q8<16;q8++) if(P_own[pc][q8])
            printf("        %-11s owns %8lld  differing %8lld\n",N[q8],P_own[pc][q8],P_att[pc][q8]); } }
      printf("\n  INIT WRITEBACK, byte-level all-opcode coverage:\n");
      printf("    A_INIT commands considered            : %lld\n",init_cmds);
      printf("    state bytes where it is FINAL writer  : %lld of %lld intended\n",
             init_bytes_final, init_cmds*80);
      printf("    mismatches among those                : %lld\n",init_bytes_mismatch);
      printf("  first divergence: task %d cmd %d op %s addr %#08x\n",
             firstdiv_task,firstdiv_cmd,
             (firstdiv_op>=0&&firstdiv_op<16)?N[firstdiv_op]:"?",firstdiv_addr); }
    printf("  TASKS BYTE-EXACT                                : %lld of %lld\n\n",exact_tasks,done);
    printf("  partition by what the task contains:\n");
    { const char*nm[4]={"neither ADPCM nor ENVMIXER","ADPCM only","ENVMIXER only","both"};
      int q2; for(q2=0;q2<4;q2++) if(cls_n[q2])
        printf("    %-28s %4lld tasks, %4lld byte-exact\n",nm[q2],cls_n[q2],cls_ok[q2]); }
    if(firstbad>=0){
      printf("  first failing task                              : %d\n",firstbad);
      printf("    differing bytes in it                         : %lld (unwritten %lld)\n",firstbad_diff,firstbad_unwr);
      printf("    first differing byte  addr %#08x  expected %#04x got %#04x (before %#04x)\n",fb_addr,fb_exp,fb_got,fb_bef);
      printf("    first UNWRITTEN run   addr %#08x  length %u bytes\n",fu_addr,fu_len); }
    return 0;
}

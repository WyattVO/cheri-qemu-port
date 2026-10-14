/*
 * QEMU RISC-V CPU
 *
 * Copyright (c) 2016-2017 Sagar Karandikar, sagark@eecs.berkeley.edu
 * Copyright (c) 2017-2018 SiFive, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef RISCV_CPU_H
#define RISCV_CPU_H

#include "hw/core/cpu.h"
#include "hw/registerfields.h"
#include "hw/qdev-properties.h"
#include "exec/cpu-common.h"
#include "exec/cpu-defs.h"
#include "exec/memop.h"

#include "qemu/units.h"
#include "exec/cpu-interrupt.h"
#include "exec/gdbstub.h"
#include "qemu/cpu-float.h"
#include "qom/object.h"
#include "qemu/int128.h"
#include "cpu_bits.h"
#include "rvfi_dii.h"
#include "cpu_cfg.h"
#include "qapi/qapi-types-common.h"
#include "cpu-qom.h"


typedef struct CPUArchState CPURISCVState;

#define CPU_RESOLVING_TYPE TYPE_RISCV_CPU

#if defined(TARGET_RISCV32)
# define TYPE_RISCV_CPU_BASE            TYPE_RISCV_CPU_BASE32
#elif defined(TARGET_RISCV64)
# define TYPE_RISCV_CPU_BASE            TYPE_RISCV_CPU_BASE64
#endif

/*
 * b0: Whether a instruction always raise a store AMO or not.
 */
#define RISCV_UW2_ALWAYS_STORE_AMO 1

#define RV(x) BIT(x - 'A')

/*
 * Update misa_bits[], misa_ext_info_arr[] and misa_ext_cfgs[]
 * when adding new MISA bits here.
 */
#define RVI RV('I')
#define RVE RV('E') /* E and I are mutually exclusive */
#define RVM RV('M')
#define RVA RV('A')
#define RVF RV('F')
#define RVD RV('D')
#define RVV RV('V')
#define RVC RV('C')
#define RVS RV('S')
#define RVU RV('U')
#define RVH RV('H')
#define RVG RV('G')
#define RVB RV('B')

extern const uint32_t misa_bits[];
const char *riscv_get_misa_ext_name(uint32_t bit);
const char *riscv_get_misa_ext_description(uint32_t bit);

#define CPU_CFG_OFFSET(_prop) offsetof(struct RISCVCPUConfig, _prop)
#define ENV_CSR_OFFSET(_csr) offsetof(CPURISCVState, _csr)

typedef struct riscv_cpu_profile {
    struct riscv_cpu_profile *u_parent;
    struct riscv_cpu_profile *s_parent;
    const char *name;
    uint32_t misa_ext;
    /*
     * The profile is enabled/disabled via command line or
     * via cpu_init(). Enabling a profile will add all its
     * mandatory extensions in the CPU during init().
     */
    bool enabled;
    /*
     * The profile is present in the CPU, i.e. the current set of
     * CPU extensions complies with it. A profile can be enabled
     * and not present (e.g. the user disabled a mandatory extension)
     * and the other way around (e.g. all mandatory extensions are
     * present in a non-profile CPU).
     *
     * QMP uses this flag.
     */
    bool present;
    bool user_set;
    int priv_spec;
    int satp_mode;
    const int32_t ext_offsets[];
} RISCVCPUProfile;

#define RISCV_PROFILE_EXT_LIST_END -1
#define RISCV_PROFILE_ATTR_UNUSED -1

extern RISCVCPUProfile *riscv_profiles[];

/* Privileged specification version */
#define PRIV_VER_1_10_0_STR "v1.10.0"
#define PRIV_VER_1_11_0_STR "v1.11.0"
#define PRIV_VER_1_12_0_STR "v1.12.0"
#define PRIV_VER_1_13_0_STR "v1.13.0"
enum {
    PRIV_VERSION_1_10_0 = 0,
    PRIV_VERSION_1_11_0,
    PRIV_VERSION_1_12_0,
    PRIV_VERSION_1_13_0,

    PRIV_VERSION_LATEST = PRIV_VERSION_1_13_0,
};

#define VEXT_VERSION_1_00_0 0x00010000
#define VEXT_VER_1_00_0_STR "v1.0"

enum {
    TRANSLATE_SUCCESS,
    TRANSLATE_FAIL,
    TRANSLATE_PMP_FAIL,
    TRANSLATE_G_STAGE_FAIL,
#if defined(TARGET_CHERI) && !defined(TARGET_RISCV32)
    TRANSLATE_CHERI_FAIL,
#endif
};

/* Extension context status */
typedef enum {
    EXT_STATUS_DISABLED = 0,
    EXT_STATUS_INITIAL,
    EXT_STATUS_CLEAN,
    EXT_STATUS_DIRTY,
} RISCVExtStatus;

/* Enum holds PMM field values for Zjpm v1.0 extension */
typedef enum {
    PMM_FIELD_DISABLED = 0,
    PMM_FIELD_RESERVED = 1,
    PMM_FIELD_PMLEN7   = 2,
    PMM_FIELD_PMLEN16  = 3,
} RISCVPmPmm;

typedef struct riscv_cpu_implied_exts_rule {
#ifndef CONFIG_USER_ONLY
    /*
     * Bitmask indicates the rule enabled status for the harts.
     * This enhancement is only available in system-mode QEMU,
     * as we don't have a good way (e.g. mhartid) to distinguish
     * the SMP cores in user-mode QEMU.
     */
    unsigned long *enabled;
#endif
    /* True if this is a MISA implied rule. */
    bool is_misa;
    /* ext is MISA bit if is_misa flag is true, else multi extension offset. */
    const uint32_t ext;
    const uint32_t implied_misa_exts;
    const uint32_t implied_multi_exts[];
} RISCVCPUImpliedExtsRule;

extern RISCVCPUImpliedExtsRule *riscv_misa_ext_implied_rules[];
extern RISCVCPUImpliedExtsRule *riscv_multi_ext_implied_rules[];

#define RISCV_IMPLIED_EXTS_RULE_END -1

#define MMU_USER_IDX 3

#define MAX_RISCV_PMPS (64)
#define OLD_MAX_RISCV_PMPS (16)
#define MIN_RISCV_PMP_GRANULARITY 4

#ifdef TARGET_CHERI
#include "cheri-lazy-capregs-types.h"
#endif
#if !defined(CONFIG_USER_ONLY)
#include "pmp.h"
#include "debug.h"
#endif

#define RV_VLEN_MAX 1024
#define RV_MAX_MHPMEVENTS 32
#define RV_MAX_MHPMCOUNTERS 32

FIELD(VTYPE, VLMUL, 0, 3)
FIELD(VTYPE, VSEW, 3, 3)
FIELD(VTYPE, VTA, 6, 1)
FIELD(VTYPE, VMA, 7, 1)
FIELD(VTYPE, VEDIV, 8, 2)
FIELD(VTYPE, RESERVED, 10, sizeof(target_ulong) * 8 - 11)

typedef struct PMUCTRState {
    /* Current value of a counter */
    target_ulong mhpmcounter_val;
    /* Current value of a counter in RV32 */
    target_ulong mhpmcounterh_val;
    /* Snapshot values of counter */
    target_ulong mhpmcounter_prev;
    /* Snapshort value of a counter in RV32 */
    target_ulong mhpmcounterh_prev;
    /* Value beyond UINT32_MAX/UINT64_MAX before overflow interrupt trigger */
    target_ulong irq_overflow_left;
} PMUCTRState;

typedef struct PMUFixedCtrState {
        /* Track cycle and icount for each privilege mode */
        uint64_t counter[4];
        uint64_t counter_prev[4];
        /* Track cycle and icount for each privilege mode when V = 1*/
        uint64_t counter_virt[2];
        uint64_t counter_virt_prev[2];
} PMUFixedCtrState;

struct CPUArchState {
#ifdef TARGET_CHERI
    struct GPCapRegs gpcapregs;
#else
    target_ulong gpr[32];
    target_ulong gprh[32]; /* 64 top bits of the 128-bit registers */
#endif

    /* vector coprocessor state. */
    uint64_t vreg[32 * RV_VLEN_MAX / 64] QEMU_ALIGNED(16);
    target_ulong vxrm;
    target_ulong vxsat;
    target_ulong vl;
    target_ulong vstart;
    target_ulong vtype;
    bool vill;

#ifdef TARGET_CHERI
    cap_register_t pcc; // SCR 0 Program counter cap. (PCC) TODO: implement this properly
    cap_register_t ddc; // SCR 1 Default data cap. (DDC)
#else
    target_ulong pc;
#endif
#ifdef CONFIG_DEBUG_TCG
    target_ulong _pc_is_current;
#endif

    target_ulong load_res;
    target_ulong load_val;
#ifdef TARGET_CHERI
    target_ulong load_pesbt;
    bool load_tag;
#endif

    /* Floating-Point state */
    uint64_t fpr[32]; /* assume both F and D extensions */
    target_ulong frm;
    float_status fp_status;

    target_ulong badaddr;
    target_ulong bins;

    target_ulong guest_phys_fault_addr;

    target_ulong priv_ver;
    target_ulong vext_ver;

    /* RISCVMXL, but uint32_t for vmstate migration */
    uint32_t misa_mxl;      /* current mxl */
    uint32_t misa_ext;      /* current extensions */
    uint32_t misa_ext_mask; /* max ext for this cpu */
    uint32_t xl;            /* current xlen */

#ifdef TARGET_CHERI
    // The cause field reports the cause of the last capability exception,
    // following the encoding described in Table 3.9.2.
    // See enum CheriCapExc in cheri-archspecific.h
    uint8_t last_cap_cause; // Used to populate xtval
    // The cap idx field reports the index of the capability register that
    // caused the last exception. When the most significant bit is set, the 5
    // least significant bits are used to index the special purpose capability
    // register file described in Table 5.3, otherwise, they index the
    // general-purpose capability register file.
    uint8_t last_cap_index; /* Used to populate xtval in v9 (now just debug). */
#ifdef TARGET_CHERI_RISCV_STD_093
    /* Cheri093CapExcType */ uint8_t last_cap_type; /* To populate xtval2 */
#endif
#else
    /* 128-bit helpers upper part return value */
    target_ulong retxh;
#endif

    target_ulong jvt;

    /* elp state for zicfilp extension */
    bool      elp;
    /* shadow stack register for zicfiss extension */
    target_ulong ssp;
    /* env place holder for extra word 2 during unwind */
    target_ulong excp_uw2;
    /* sw check code for sw check exception */
    target_ulong sw_check_code;
#ifdef CONFIG_USER_ONLY
    uint32_t elf_flags;
#endif

    target_ulong priv;
    /* CSRs for execution environment configuration */
    uint64_t menvcfg;
    target_ulong senvcfg;

#ifndef CONFIG_USER_ONLY
    /* This contains QEMU specific information about the virt state. */
    bool virt_enabled;
    target_ulong geilen;
    uint64_t resetvec;

    /*
     * For RV32 this is 32-bit mstatus and 32-bit mstatush.
     * For RV64 this is a 64-bit mstatus.
     */
    uint64_t mstatus;

    uint64_t mip;
    /*
     * MIP contains the software writable version of SEIP ORed with the
     * external interrupt value. The MIP register is always up-to-date.
     * To keep track of the current source, we also save booleans of the values
     * here.
     */
    bool external_seip;
    bool software_seip;

    uint64_t miclaim;

    uint64_t mie;
    uint64_t mideleg;

    /*
     * When mideleg[i]=0 and mvien[i]=1, sie[i] is no more
     * alias of mie[i] and needs to be maintained separately.
     */
    uint64_t sie;

    /*
     * When hideleg[i]=0 and hvien[i]=1, vsie[i] is no more
     * alias of sie[i] (mie[i]) and needs to be maintained separately.
     */
    uint64_t vsie;

    target_ulong satp;   /* since: priv-1.10.0 */
    target_ulong stval;
    target_ulong medeleg;

#if defined(TARGET_CHERI) && !defined(TARGET_RISCV32)
    target_ulong sccsr;
#endif

#ifdef TARGET_CHERI
    cap_register_t stvecc;    // SCR 12 Supervisor trap code cap. (STCC)
    cap_register_t sepcc;     // SCR 15 Supervisor exception PC cap. (SEPCC)
#else
    target_ulong stvec;
    target_ulong sepc;
#endif
    target_ulong scause;

#ifdef TARGET_CHERI
    cap_register_t mtvecc;    // SCR 28 Machine trap code cap. (MTCC)
    cap_register_t mepcc;     // SCR 31 Machine exception PC cap. (MEPCC)
#else
    target_ulong mtvec;
    target_ulong mepc;
#endif
    target_ulong mcause;
    target_ulong mtval;  /* since: priv-1.10.0 */

    uint64_t mctrctl;
    uint32_t sctrdepth;
    uint32_t sctrstatus;
    uint64_t vsctrctl;

    uint64_t ctr_src[16 << SCTRDEPTH_MAX];
    uint64_t ctr_dst[16 << SCTRDEPTH_MAX];
    uint64_t ctr_data[16 << SCTRDEPTH_MAX];

    /* Machine and Supervisor interrupt priorities */
    uint8_t miprio[64];
    uint8_t siprio[64];

    /* AIA CSRs */
    target_ulong miselect;
    target_ulong siselect;
    uint64_t mvien;
    uint64_t mvip;

    /* Hypervisor CSRs */
    target_ulong hstatus;
    target_ulong hedeleg;
    uint64_t hideleg;
    uint32_t hcounteren;
    target_ulong htval;
    target_ulong htinst;
    target_ulong hgatp;
    target_ulong hgeie;
    target_ulong hgeip;
    uint64_t htimedelta;
    uint64_t hvien;

    /*
     * Bits VSSIP, VSTIP and VSEIP in hvip are maintained in mip. Other bits
     * from 0:12 are reserved. Bits 13:63 are not aliased and must be separately
     * maintain in hvip.
     */
    uint64_t hvip;

    /* Hypervisor controlled virtual interrupt priorities */
    target_ulong hvictl;
    uint8_t hviprio[64];

    /* Virtual CSRs */
#ifdef TARGET_CHERI
    cap_register_t vstvecc;
    cap_register_t vsscratchc;
    cap_register_t vsepcc;
#else
    /* Upper 64-bits of 128-bit CSRs */
    uint64_t mscratchh;
    uint64_t sscratchh;

    target_ulong vstvec;
    target_ulong vsepc;
    target_ulong vsscratch;
#endif
    /*
     * For RV32 this is 32-bit vsstatus and 32-bit vsstatush.
     * For RV64 this is a 64-bit vsstatus.
     */
    uint64_t vsstatus;
    target_ulong vscause;
    target_ulong vstval;
    target_ulong vsatp;

    /* AIA VS-mode CSRs */
    target_ulong vsiselect;

    target_ulong mtval2;
    target_ulong mtinst;

    /* HS Backup CSRs */
#ifdef TARGET_CHERI
    cap_register_t stcc_hs;
    cap_register_t sepcc_hs;
    cap_register_t sscratchc_hs;

#ifdef TARGET_CHERI_RISCV_STD_093
    target_ulong stval2;
    target_ulong vstval2;
    target_ulong stval2_hs;
#endif
#else
    target_ulong stvec_hs;
    target_ulong sepc_hs;
    target_ulong sscratch_hs;
#endif
    target_ulong scause_hs;
    target_ulong stval_hs;
    target_ulong satp_hs;
    uint64_t mstatus_hs;

    /*
     * Signals whether the current exception occurred with two-stage address
     * translation active.
     */
    bool two_stage_lookup;
    /*
     * Signals whether the current exception occurred while doing two-stage
     * address translation for the VS-stage page table walk.
     */
    bool two_stage_indirect_lookup;

    uint32_t scounteren;
    uint32_t mcounteren;

    uint32_t scountinhibit;
    uint32_t mcountinhibit;

    /* PMU cycle & instret privilege mode filtering */
    target_ulong mcyclecfg;
    target_ulong mcyclecfgh;
    target_ulong minstretcfg;
    target_ulong minstretcfgh;

    /* PMU counter state */
    PMUCTRState pmu_ctrs[RV_MAX_MHPMCOUNTERS];

    /* PMU event selector configured values. First three are unused */
    target_ulong mhpmevent_val[RV_MAX_MHPMEVENTS];

#ifdef TARGET_CHERI
    /* zstid registers */
    cap_register_t mtidc;
    cap_register_t stidc;
    cap_register_t utidc;
    cap_register_t vstidc;
    cap_register_t stidc_hs;
#else
    /* zstid registers in integer mode */
    target_ulong mtid;
    target_ulong stid;
    target_ulong utid;
    target_ulong vstid;
    target_ulong stid_hs;
#endif

#ifdef TARGET_CHERI_RISCV_V9
    /* V9-only special capability registers */
    cap_register_t mtdc;  /* Machine trap data cap */
    cap_register_t stdc;  /* Supervisor trap data cap */
    cap_register_t vstdc; /* Virtual Supervisor trap data cap */
#endif

    /* PMU event selector configured values for RV32 */
    target_ulong mhpmeventh_val[RV_MAX_MHPMEVENTS];

    PMUFixedCtrState pmu_fixed_ctrs[2];

#ifdef TARGET_CHERI
    cap_register_t sscratchc;
    cap_register_t mscratchc;
#else
    target_ulong sscratch;
    target_ulong mscratch;
#endif

    /* temporary htif regs */
    uint64_t mfromhost;
    uint64_t mtohost;

    /* Sstc CSRs */
    uint64_t stimecmp;

    uint64_t vstimecmp;

    /* physical memory protection */
    pmp_table_t pmp_state;
    target_ulong mseccfg;

    /* trigger module */
    target_ulong trigger_cur;
    target_ulong tdata1[RV_MAX_TRIGGERS];
    target_ulong tdata2[RV_MAX_TRIGGERS];
    target_ulong tdata3[RV_MAX_TRIGGERS];
    target_ulong mcontext;
    struct CPUBreakpoint *cpu_breakpoint[RV_MAX_TRIGGERS];
    struct CPUWatchpoint *cpu_watchpoint[RV_MAX_TRIGGERS];
    QEMUTimer *itrigger_timer[RV_MAX_TRIGGERS];
    int64_t last_icount;
    bool itrigger_enabled;

    /* machine specific rdtime callback */
    uint64_t (*rdtime_fn)(void *);
    void *rdtime_fn_arg;

    /* machine specific AIA ireg read-modify-write callback */
#define AIA_MAKE_IREG(__isel, __priv, __virt, __vgein, __xlen) \
    ((((__xlen) & 0xff) << 24) | \
     (((__vgein) & 0x3f) << 20) | \
     (((__virt) & 0x1) << 18) | \
     (((__priv) & 0x3) << 16) | \
     (__isel & 0xffff))
#define AIA_IREG_ISEL(__ireg)                  ((__ireg) & 0xffff)
#define AIA_IREG_PRIV(__ireg)                  (((__ireg) >> 16) & 0x3)
#define AIA_IREG_VIRT(__ireg)                  (((__ireg) >> 18) & 0x1)
#define AIA_IREG_VGEIN(__ireg)                 (((__ireg) >> 20) & 0x3f)
#define AIA_IREG_XLEN(__ireg)                  (((__ireg) >> 24) & 0xff)
    int (*aia_ireg_rmw_fn[4])(void *arg, target_ulong reg,
        target_ulong *val, target_ulong new_val, target_ulong write_mask);
    void *aia_ireg_rmw_fn_arg[4];

    /* True if in debugger mode.  */
    bool debugger;

    uint64_t mstateen[SMSTATEEN_MAX_COUNT];
    uint64_t hstateen[SMSTATEEN_MAX_COUNT];
    uint64_t sstateen[SMSTATEEN_MAX_COUNT];
    uint64_t henvcfg;
#endif

#ifdef TARGET_CHERI
    // Some statcounters:
    uint64_t statcounters_cap_read;
    uint64_t statcounters_cap_read_tagged;
    uint64_t statcounters_cap_write;
    uint64_t statcounters_cap_write_tagged;

    uint64_t statcounters_imprecise_setbounds;
    uint64_t statcounters_unrepresentable_caps;

#endif

    /* Fields up to this point are cleared by a TestRIG reset */
    struct {} end_testrig_reset_fields;


#ifdef CONFIG_RVFI_DII
    struct {
        struct rvfi_dii_instruction_metadata INST;
        struct rvfi_dii_pc_data PC;
        struct rvfi_dii_integer_data INTEGER;
        struct rvfi_dii_memory_access_data MEM;
        // TODO: struct rvfi_dii_csr_data CSR;
        // TODO: struct rvfi_dii_fp_data FP;
        // TODO: struct rvfi_dii_cheri_data CHERI;
        // TODO: struct rvfi_dii_cheri_scr_data CHERI_SCR;
        // TODO: struct rvfi_dii_trap_data TRAP;
        uint32_t available_fields;
    } rvfi_dii_trace;
    bool rvfi_dii_have_injected_insn;
    uint32_t rvfi_dii_injected_insn;
#endif



    target_ulong mhartid;

    uint32_t features;

#ifdef CONFIG_USER_ONLY
    uint32_t elf_flags;
#endif
    /* Fields from here on are preserved across CPU reset. */
    QEMUTimer *stimer; /* Internal timer for S-mode interrupt */
    QEMUTimer *vstimer; /* Internal timer for VS-mode interrupt */
    bool vstime_irq;

    hwaddr kernel_addr;
    hwaddr fdt_addr;

#ifdef CONFIG_KVM
    /* kvm timer */
    bool kvm_timer_dirty;
    uint64_t kvm_timer_time;
    uint64_t kvm_timer_compare;
    uint64_t kvm_timer_state;
    uint64_t kvm_timer_frequency;
#endif /* CONFIG_KVM */

    /* RNMI */
    target_ulong mnscratch;
    target_ulong mnepc;
    target_ulong mncause; /* mncause without bit XLEN-1 set to 1 */
    target_ulong mnstatus;
    target_ulong rnmip;
    uint64_t rnmi_irqvec;
    uint64_t rnmi_excpvec;
};

/*
 * map is a 16-bit bitmap: the most significant set bit in map is the maximum
 * satp mode that is supported. It may be chosen by the user and must respect
 * what qemu implements (valid_1_10_32/64) and what the hw is capable of
 * (supported bitmap below).
 *
 * init is a 16-bit bitmap used to make sure the user selected a correct
 * configuration as per the specification.
 */
typedef struct {
    uint16_t map, init;
} RISCVSATPModes;

/*
 * RISCVCPU:
 * @env: #CPURISCVState
 *
 * A RISCV CPU.
 */
struct ArchCPU {
    CPUState parent_obj;

    CPURISCVState env;

    GDBFeature dyn_csr_feature;
    GDBFeature dyn_vreg_feature;
#ifdef TARGET_CHERI_RISCV_STD
    GDBFeature dyn_ycsr_feature;
#elif defined(TARGET_CHERI_RISCV_V9)
    GDBFeature dyn_scr_feature;
#endif

    /* Configuration Settings */
    RISCVCPUConfig cfg;
    RISCVSATPModes satp_modes;

    QEMUTimer *pmu_timer;
    /* A bitmask of Available programmable counters */
    uint32_t pmu_avail_ctrs;
    /* Mapping of events to counters */
    GHashTable *pmu_event_ctr_map;
    const GPtrArray *decoders;
};

typedef struct RISCVCSR RISCVCSR;

typedef struct RISCVCPUDef {
    RISCVMXL misa_mxl_max;  /* max mxl for this cpu */
    RISCVCPUProfile *profile;
    uint32_t misa_ext;
    int priv_spec;
    int32_t vext_spec;
    RISCVCPUConfig cfg;
    bool bare;
    const RISCVCSR *custom_csrs;
} RISCVCPUDef;

/**
 * RISCVCPUClass:
 * @parent_realize: The parent class' realize handler.
 * @parent_phases: The parent class' reset phase handlers.
 *
 * A RISCV CPU model.
 */
struct RISCVCPUClass {
    CPUClass parent_class;

    DeviceRealize parent_realize;
    ResettablePhases parent_phases;
    RISCVCPUDef *def;
};

static inline int riscv_has_ext(CPURISCVState *env, uint32_t ext)
{
    return (env->misa_ext & ext) != 0;
}


#include "cpu_user.h"

extern const char * const riscv_int_regnames[];
extern const char * const riscv_int_regnamesh[];
extern const char * const riscv_fpr_regnames[];
extern const char * const riscv_rvv_regnames[];
#ifdef TARGET_CHERI
/* Needed for cheri-common logging */
extern const char * const cheri_gp_regnames[];
#endif

#ifdef CONFIG_TCG_LOG_INSTR
void riscv_log_instr_csr_changed(CPURISCVState *env, int csrno);

#define log_changed_special_reg(env, name, newval, index, type)                \
    do {                                                                       \
        if (qemu_log_instr_enabled(env))                                       \
            qemu_log_instr_reg(env, name, newval, index, type);                \
    } while (0)
#else /* !CONFIG_TCG_LOG_INSTR */
#define log_changed_special_reg(env, name, newval) ((void)0)
#define riscv_log_instr_csr_changed(env, csrno) ((void)0)
#endif /* !CONFIG_TCG_LOG_INSTR */

#define CHK_BLK_POW2(prop) \
do { \
    if ((cpu->cfg.prop == 0) || \
            (cpu->cfg.prop & (cpu->cfg.prop - 1))) { \
        error_setg(errp, "%s must be a power of 2.", tostring(prop)); \
        return; \
    } \
} while (0)

/*
 * From 5.3.6 Special Capability Registers (SCRs)
 * Where an SCR extends a RISC-V CSR, e.g. MTCC extending mtvec, any read to the
 * CSR shall return the address (offset for ISAv8) of the corresponding SCR.
 * Similarly, any write to the CSR shall set the address (offset for ISAv8) of
 * the SCR to the value written.
 */
#ifdef TARGET_CHERI
#define SCR_TO_PROGRAM_COUNTER(env, scr)                                       \
    (CHERI_NO_RELOCATION(env) ? cap_get_cursor(scr)                            \
                              : (target_ulong)cap_get_offset(scr))
/**
 * @returns the architectural view of the underlying SCR,address/offset
 * depending on CHERI ISA version.
 */
#define GET_SPECIAL_REG_ARCH(env, name, cheri_name)                            \
    SCR_TO_PROGRAM_COUNTER(env, &((env)->cheri_name))
/**
 * @returns the address of a given SCR as we feed our PC around as an address
 * not the architectural offset.
 */
#define GET_SPECIAL_REG_ADDR(env, name, cheri_name)                            \
    ((target_ulong)cap_get_cursor(&((env)->cheri_name)))
void update_special_register(CPURISCVState *env, cap_register_t *scr,
                             const char *name, target_ulong value);
#define SCR_SET_PROGRAM_COUNTER(env, scr, name, value)                         \
    update_special_register(env, scr, name, value)
#define SET_SPECIAL_REG(env, name, cheri_name, value)                          \
    SCR_SET_PROGRAM_COUNTER(env, &((env)->cheri_name), #cheri_name, value)

#else /* ! TARGET_CHERI */
#define GET_SPECIAL_REG_ARCH(env, name, cheri_name) ((env)->name)
#define GET_SPECIAL_REG_ADDR(env, name, cheri_name) ((env)->name)
#define SET_SPECIAL_REG(env, name, cheri_name, value)                          \
    do {                                                                       \
        env->name = value;                                                     \
        log_changed_special_reg(env, #name, value, 0, LRI_CSR_ACCESS);         \
    } while (false)
#endif /* ! TARGET_CHERI */

#ifdef CONFIG_RVFI_DII
#define RVFI_DII_RAM_START 0x80000000
#define RVFI_DII_RAM_SIZE (8 * MiB)
#define RVFI_DII_RAM_END (RVFI_DII_RAM_START + RVFI_DII_RAM_SIZE)
void rvfi_dii_communicate(CPUState *cs, CPURISCVState *env, bool was_trap);
void riscv_rvfi_dii_translate_mem_addr(CPURISCVState *env);
#define CHECK_SAME_TYPE(a, b, msg)                                             \
    _Static_assert(__builtin_types_compatible_p(a*, b*), msg)
#define rvfi_dii_offset(type, field)                                           \
    offsetof(CPURISCVState, rvfi_dii_trace.type.rvfi_##field)
#define gen_rvfi_dii_set_field(type, field, arg)                               \
    do {                                                                       \
        CHECK_SAME_TYPE(                                                       \
            typeof(((CPURISCVState *)NULL)->rvfi_dii_trace.type.rvfi_##field), \
            uint64_t, "Should only be used for uint64_t fields");              \
        CHECK_SAME_TYPE(TCGv_i64, typeof(arg), "Expected 64-bit store");       \
        tcg_gen_st_i64(arg, tcg_env, rvfi_dii_offset(type, field));            \
        tcg_gen_ori_i32(cpu_rvfi_available_fields, cpu_rvfi_available_fields,  \
                        RVFI_##type##_DATA);                                   \
    } while (0)
#define gen_rvfi_dii_set_field_const_iN(n, st_op, type, field, constant)       \
    do {                                                                       \
        CHECK_SAME_TYPE(                                                       \
            typeof(((CPURISCVState *)NULL)->rvfi_dii_trace.type.rvfi_##field), \
            uint##n##_t, "Should only be used for uint64_t fields");           \
        TCGv_i64 rvfi_tc = tcg_constant_i64(constant);                            \
        tcg_gen_##st_op(rvfi_tc, tcg_env, rvfi_dii_offset(type, field));       \
        tcg_gen_ori_i32(cpu_rvfi_available_fields, cpu_rvfi_available_fields,  \
                        RVFI_##type##_DATA);                                   \
    } while (0)
#define gen_rvfi_dii_set_field_const_i8(type, field, constant)                 \
    gen_rvfi_dii_set_field_const_iN(8, st8_i64, type, field, constant)
#define gen_rvfi_dii_set_field_const_i16(type, field, constant)                \
    gen_rvfi_dii_set_field_const_iN(16, st16_i64, type, field, constant)
#define gen_rvfi_dii_set_field_const_i32(type, field, constant)                \
    gen_rvfi_dii_set_field_const_iN(32, st32_i64, type, field, constant)
#define gen_rvfi_dii_set_field_const_i64(type, field, constant)                \
    gen_rvfi_dii_set_field_const_iN(64, st_i64, type, field, constant)
#define gen_rvfi_dii_set_field_zext_i32(type, field, arg)                      \
    do {                                                                       \
        CHECK_SAME_TYPE(TCGv_i32, typeof(arg), "Expected i32");                \
        TCGv_i64 tmp = tcg_temp_new_i64();                                     \
        tcg_gen_extu_i32_i64(tmp, arg);                                        \
        gen_rvfi_dii_set_field(type, field, tmp);                              \
    } while (0)
#if TARGET_LONG_BITS == 32
#define gen_rvfi_dii_set_field_zext_tl(type, field, arg)                       \
    gen_rvfi_dii_set_field_zext_i32(type, field, arg)
#else
#define gen_rvfi_dii_set_field_zext_tl(type, field, arg)                       \
    gen_rvfi_dii_set_field(type, field, arg)
#endif
#define gen_rvfi_dii_set_field_zext_addr(type, field, arg)                     \
    do {                                                                       \
        CHECK_SAME_TYPE(TCGv_cap_checked_ptr, typeof(arg), "Expected addr");   \
        TCGv_i64 tmp = tcg_temp_new_i64();                                     \
        tcg_gen_extu_tl_i64(tmp, (TCGv)arg);                                   \
        gen_rvfi_dii_set_field(type, field, tmp);                              \
    } while (0)
#define gen_rvfi_dii_set_mem_data(rw, addr, val, memop, extend_to_i64)         \
    do {                                                                       \
        TCGv_i64 tmp = tcg_temp_new_i64();                                     \
        extend_to_i64(tmp, val);                                               \
        tcg_gen_andi_i64(tmp, tmp, MAKE_64BIT_MASK(0, 8 * memop_size(memop))); \
        gen_rvfi_dii_set_field_zext_addr(MEM, mem_addr, addr);                 \
        gen_rvfi_dii_set_field(MEM, mem_##rw##data[0], tmp);                   \
        gen_rvfi_dii_set_field_const_i32(MEM, mem_##rw##mask,                  \
                                         memop_rvfi_mask(memop));              \
    } while (0)
#define gen_rvfi_dii_set_mem_data_i32(rw, addr, val_i32, memop)                \
    gen_rvfi_dii_set_mem_data(rw, addr, val_i32, memop, tcg_gen_extu_i32_i64)
#define gen_rvfi_dii_set_mem_data_i64(rw, addr, val_i64, memop)                \
    gen_rvfi_dii_set_mem_data(rw, addr, val_i64, memop, tcg_gen_mov_i64)
#else
#define gen_rvfi_dii_set_field(type, field, arg) ((void)0)
#define gen_rvfi_dii_set_field_zext_i32(type, field, arg) ((void)0)
#define gen_rvfi_dii_set_field_zext_addr(type, field, arg) ((void)0)
#define gen_rvfi_dii_set_field_zext_tl(type, field, arg) ((void)0)
#define gen_rvfi_dii_set_field_const_i8(type, field, constant) ((void)0)
#define gen_rvfi_dii_set_field_const_i16(type, field, constant) ((void)0)
#define gen_rvfi_dii_set_field_const_i32(type, field, constant) ((void)0)
#define gen_rvfi_dii_set_field_const_i64(type, field, constant) ((void)0)
#endif

const char *riscv_cpu_get_trap_name(target_ulong cause, bool async);
int riscv_cpu_write_elf64_note(WriteCoreDumpFunction f, CPUState *cs,
                               int cpuid, DumpState *s);
int riscv_cpu_write_elf32_note(WriteCoreDumpFunction f, CPUState *cs,
                               int cpuid, DumpState *s);
int riscv_cpu_gdb_read_register(CPUState *cpu, GByteArray *buf, int reg);
int riscv_cpu_gdb_write_register(CPUState *cpu, uint8_t *buf, int reg);
int riscv_cpu_hviprio_index2irq(int index, int *out_irq, int *out_rdzero);
uint8_t riscv_cpu_default_priority(int irq);
uint64_t riscv_cpu_all_pending(CPURISCVState *env);
int riscv_cpu_mirq_pending(CPURISCVState *env);
int riscv_cpu_sirq_pending(CPURISCVState *env);
int riscv_cpu_vsirq_pending(CPURISCVState *env);
bool riscv_cpu_fp_enabled(CPURISCVState *env);
target_ulong riscv_cpu_get_geilen(CPURISCVState *env);
void riscv_cpu_set_geilen(CPURISCVState *env, target_ulong geilen);
bool riscv_cpu_vector_enabled(CPURISCVState *env);
void riscv_cpu_set_virt_enabled(CPURISCVState *env, bool enable);
int riscv_env_mmu_index(CPURISCVState *env, bool ifetch);
#ifdef TARGET_CHERI
hwaddr cpu_riscv_translate_address_tagmem(CPURISCVState *env,
                                          target_ulong address,
                                          MMUAccessType rw, int reg, int *prot,
                                          uintptr_t retpc);
#endif
bool cpu_get_fcfien(CPURISCVState *env);
bool cpu_get_bcfien(CPURISCVState *env);
bool riscv_env_smode_dbltrp_enabled(CPURISCVState *env, bool virt);
G_NORETURN void  riscv_cpu_do_unaligned_access(CPUState *cs, vaddr addr,
                                               MMUAccessType access_type,
                                               int mmu_idx, uintptr_t retaddr);
bool riscv_cpu_tlb_fill(CPUState *cs, vaddr address, int size,
                        MMUAccessType access_type, int mmu_idx,
                        bool probe, uintptr_t retaddr);
char *riscv_isa_string(RISCVCPU *cpu);
int riscv_cpu_max_xlen(RISCVCPUClass *mcc);
bool riscv_cpu_option_set(const char *optname);

#ifdef TARGET_CHERI
static inline bool riscv_cpu_mode_cre(CPURISCVState *env);
#endif
#ifndef CONFIG_USER_ONLY
void riscv_cpu_do_interrupt(CPUState *cpu);
void riscv_isa_write_fdt(RISCVCPU *cpu, void *fdt, char *nodename);
void riscv_cpu_do_transaction_failed(CPUState *cs, hwaddr physaddr,
                                     vaddr addr, unsigned size,
                                     MMUAccessType access_type,
                                     int mmu_idx, MemTxAttrs attrs,
                                     MemTxResult response, uintptr_t retaddr);
hwaddr riscv_cpu_get_phys_page_debug(CPUState *cpu, vaddr addr);
bool riscv_cpu_exec_interrupt(CPUState *cs, int interrupt_request);
void riscv_cpu_swap_hypervisor_regs(CPURISCVState *env, bool hs_mode_trap);
int riscv_cpu_claim_interrupts(RISCVCPU *cpu, uint64_t interrupts);
uint64_t riscv_cpu_update_mip(CPURISCVState *env, uint64_t mask,
                              uint64_t value);
void riscv_cpu_set_rnmi(RISCVCPU *cpu, uint32_t irq, bool level);
void riscv_cpu_interrupt(CPURISCVState *env);
#define BOOL_TO_MASK(x) (-!!(x)) /* helper for riscv_cpu_update_mip value */
void riscv_cpu_set_rdtime_fn(CPURISCVState *env, uint64_t (*fn)(void *),
                             void *arg);
void riscv_cpu_set_aia_ireg_rmw_fn(CPURISCVState *env, uint32_t priv,
                                   int (*rmw_fn)(void *arg,
                                                 target_ulong reg,
                                                 target_ulong *val,
                                                 target_ulong new_val,
                                                 target_ulong write_mask),
                                   void *rmw_fn_arg);

RISCVException smstateen_acc_ok(CPURISCVState *env, int index, uint64_t bit);
#endif /* !CONFIG_USER_ONLY */

void riscv_cpu_set_mode(CPURISCVState *env, target_ulong newpriv, bool virt_en);

void riscv_ctr_add_entry(CPURISCVState *env, target_long src, target_long dst,
    enum CTRType type, target_ulong prev_priv, bool prev_virt);
void riscv_ctr_clear(CPURISCVState *env);

void riscv_translate_init(void);
void riscv_translate_code(CPUState *cs, TranslationBlock *tb,
                          int *max_insns, vaddr pc, void *host_pc);

G_NORETURN void riscv_raise_exception(CPURISCVState *env,
                                      RISCVException exception,
                                      uintptr_t pc);

target_ulong riscv_cpu_get_fflags(CPURISCVState *env);
void riscv_cpu_set_fflags(CPURISCVState *env, target_ulong);
bool csr_needs_asr(uint32_t csrno, bool write);

static inline const RISCVCPUConfig *riscv_cpu_cfg(CPURISCVState *env)
{
    return &env_archcpu(env)->cfg;
}

static inline bool riscv_has_cheri(CPURISCVState *env)
{
#ifdef TARGET_CHERI
    return riscv_cpu_cfg(env)->ext_cheri;
#else
    return false;
#endif
}

static inline bool riscv_has_cheri_hybrid(CPURISCVState *env)
{
#ifdef TARGET_CHERI
    if (!riscv_cpu_cfg(env)->ext_cheri) {
        return false;
    }
#ifdef TARGET_CHERI_RISCV_V9
    return true;
#else
    return riscv_cpu_cfg(env)->ext_zyhybrid;
#endif
#else
    return false;
#endif
}

static inline bool riscv_has_stid(CPURISCVState *env)
{
#ifdef TARGET_CHERI
    return true;
#else
    return false;
#endif
}

#include "cpu_cheri.h"
#include "exec/log_instr.h"

static inline bool pc_is_current(CPURISCVState *env)
{
#ifdef CONFIG_DEBUG_TCG
    return env->_pc_is_current;
#else
    return true;
#endif
}

/*
 * Note: the pc does not have to be up-to-date, tb start is fine.
 * We may miss a few dumps or print too many if -dfilter is on but
 * that shouldn't really matter.
 */
static inline target_ulong cpu_get_recent_pc(CPURISCVState *env)
{
#ifdef TARGET_CHERI
    return env->pcc._cr_cursor;
#else
    return env->pc;
#endif
}
FIELD(TB_FLAGS, MEM_IDX, 0, 3)
FIELD(TB_FLAGS, FS, 3, 2)
/* Vector flags */
FIELD(TB_FLAGS, VS, 5, 2)
FIELD(TB_FLAGS, LMUL, 7, 3)
FIELD(TB_FLAGS, SEW, 10, 3)
FIELD(TB_FLAGS, VL_EQ_VLMAX, 13, 1)
FIELD(TB_FLAGS, VILL, 14, 1)
FIELD(TB_FLAGS, VSTART_EQ_ZERO, 15, 1)
/* The combination of MXL/SXL/UXL that applies to the current cpu mode. */
FIELD(TB_FLAGS, XL, 16, 2)
/* If PointerMasking should be applied */
FIELD(TB_FLAGS, PM_MASK_ENABLED, 18, 1)
FIELD(TB_FLAGS, PM_BASE_ENABLED, 19, 1)
FIELD(TB_FLAGS, VTA, 18, 1)
FIELD(TB_FLAGS, VMA, 19, 1)
/* Native debug itrigger */
FIELD(TB_FLAGS, ITRIGGER, 20, 1)
/* Virtual mode enabled */
FIELD(TB_FLAGS, VIRT_ENABLED, 21, 1)
FIELD(TB_FLAGS, PRIV, 22, 2)
FIELD(TB_FLAGS, AXL, 24, 2)
/* zicfilp needs a TB flag to track indirect branches */
FIELD(TB_FLAGS, FCFI_ENABLED, 26, 1)
FIELD(TB_FLAGS, FCFI_LP_EXPECTED, 27, 1)
/* zicfiss needs a TB flag so that correct TB is located based on tb flags */
FIELD(TB_FLAGS, BCFI_ENABLED, 28, 1)
/* If pointer masking should be applied and address sign extended */
FIELD(TB_FLAGS, PM_PMM, 29, 2)
FIELD(TB_FLAGS, PM_SIGNEXTEND, 31, 1)

#ifdef TARGET_RISCV32
#define riscv_cpu_mxl(env)  ((void)(env), MXL_RV32)
#else
static inline RISCVMXL riscv_cpu_mxl(CPURISCVState *env)
{
    return env->misa_mxl;
}
#endif
#define riscv_cpu_mxl_bits(env) (1UL << (4 + riscv_cpu_mxl(env)))


#if !defined(CONFIG_USER_ONLY)
static inline int cpu_address_mode(CPURISCVState *env)
{
    int mode = env->priv;

    if (mode == PRV_M && get_field(env->mstatus, MSTATUS_MPRV)) {
        mode = get_field(env->mstatus, MSTATUS_MPP);
    }
    return mode;
}

static inline RISCVMXL cpu_get_xl(CPURISCVState *env, target_ulong mode)
{
    RISCVMXL xl = env->misa_mxl;
    /*
     * When emulating a 32-bit-only cpu, use RV32.
     * When emulating a 64-bit cpu, and MXL has been reduced to RV32,
     * MSTATUSH doesn't have UXL/SXL, therefore XLEN cannot be widened
     * back to RV64 for lower privs.
     */
    if (xl != MXL_RV32) {
        switch (mode) {
        case PRV_M:
            break;
        case PRV_U:
            xl = get_field(env->mstatus, MSTATUS64_UXL);
            break;
        default: /* PRV_S */
            xl = get_field(env->mstatus, MSTATUS64_SXL);
            break;
        }
    }
    return xl;
}
#endif

#if defined(TARGET_RISCV32)
#define cpu_recompute_xl(env)  ((void)(env), MXL_RV32)
#else
static inline RISCVMXL cpu_recompute_xl(CPURISCVState *env)
{
#if !defined(CONFIG_USER_ONLY)
    return cpu_get_xl(env, env->priv);
#else
    return env->misa_mxl;
#endif
}
#endif

#if defined(TARGET_RISCV32)
#define cpu_address_xl(env)  ((void)(env), MXL_RV32)
#else
static inline RISCVMXL cpu_address_xl(CPURISCVState *env)
{
#ifdef CONFIG_USER_ONLY
    return env->xl;
#else
    int mode = cpu_address_mode(env);

    return cpu_get_xl(env, mode);
#endif
}
#endif

static inline int riscv_cpu_xlen(CPURISCVState *env)
{
    return 16 << env->xl;
}

#ifdef TARGET_RISCV32
#define riscv_cpu_sxl(env)  ((void)(env), MXL_RV32)
#else
static inline RISCVMXL riscv_cpu_sxl(CPURISCVState *env)
{
#ifdef CONFIG_USER_ONLY
    return env->misa_mxl;
#else
    if (env->misa_mxl != MXL_RV32) {
        return get_field(env->mstatus, MSTATUS64_SXL);
    }
#endif
    return MXL_RV32;
}
#endif

static inline bool riscv_cpu_allow_16bit_insn(const RISCVCPUConfig *cfg,
                                              target_long priv_ver,
                                              uint32_t misa_ext)
{
    /* In priv spec version 1.12 or newer, C always implies Zca */
    if (priv_ver >= PRIV_VERSION_1_12_0) {
        return cfg->ext_zca;
    } else {
        return misa_ext & RVC;
    }
}

/*
 * Encode LMUL to lmul as follows:
 *     LMUL    vlmul    lmul
 *      1       000       0
 *      2       001       1
 *      4       010       2
 *      8       011       3
 *      -       100       -
 *     1/8      101      -3
 *     1/4      110      -2
 *     1/2      111      -1
 *
 * then, we can calculate VLMAX = vlen >> (vsew + 3 - lmul)
 * e.g. vlen = 256 bits, SEW = 16, LMUL = 1/8
 *      => VLMAX = vlen >> (1 + 3 - (-3))
 *               = 256 >> 7
 *               = 2
 */
static inline uint32_t vext_get_vlmax(uint32_t vlenb, uint32_t vsew,
                                      int8_t lmul)
{
    uint32_t vlen = vlenb << 3;

    /*
     * We need to use 'vlen' instead of 'vlenb' to
     * preserve the '+ 3' in the formula. Otherwise
     * we risk a negative shift if vsew < lmul.
     */
    return vlen >> (vsew + 3 - lmul);
}

#ifdef CONFIG_TCG_LOG_INSTR
#define RISCV_LOG_INSTR_CPU_U QEMU_LOG_INSTR_CPU_USER
#define RISCV_LOG_INSTR_CPU_S QEMU_LOG_INSTR_CPU_SUPERVISOR
#define RISCV_LOG_INSTR_CPU_H QEMU_LOG_INSTR_CPU_HYPERVISOR
#define RISCV_LOG_INSTR_CPU_M QEMU_LOG_INSTR_CPU_TARGET1
extern const char * const riscv_cpu_mode_names[];

static inline bool cpu_in_user_mode(CPURISCVState *env)
{
    return env->priv == PRV_U;
}

static inline unsigned cpu_get_asid(CPURISCVState *env, target_ulong pc)
{
    return get_field(env->satp, SATP_ASID);
}

static inline const char *cpu_get_mode_name(qemu_log_instr_cpu_mode_t mode)
{
    if (riscv_cpu_mode_names[mode])
        return riscv_cpu_mode_names[mode];
    return "<invalid>";
}
#endif

bool riscv_cpu_is_32bit(RISCVCPU *cpu);

bool riscv_cpu_virt_mem_enabled(CPURISCVState *env);
RISCVPmPmm riscv_pm_get_pmm(CPURISCVState *env);
RISCVPmPmm riscv_pm_get_virt_pmm(CPURISCVState *env);
uint32_t riscv_pm_get_pmlen(RISCVPmPmm pmm);

RISCVException riscv_csrrw_check(CPURISCVState *env, int csrno,
                                 bool write);
RISCVException riscv_csrr(CPURISCVState *env, int csrno,
                          target_ulong *ret_value, uintptr_t retpc);
RISCVException riscv_csrrw(CPURISCVState *env, int csrno,
                           target_ulong *ret_value,
                           target_ulong new_value, target_ulong write_mask,
                           uintptr_t retpc);
RISCVException riscv_csrrw_debug(CPURISCVState *env, int csrno,
                                 target_ulong *ret_value,
                                 target_ulong new_value,
                                 target_ulong write_mask);

static inline void riscv_csr_write(CPURISCVState *env, int csrno,
                                   target_ulong val, uintptr_t retpc)
{
    riscv_csrrw(env, csrno, NULL, val, MAKE_64BIT_MASK(0, TARGET_LONG_BITS), retpc);
}

static inline target_ulong riscv_csr_read(CPURISCVState *env, int csrno, uintptr_t retpc)
{
    target_ulong val = 0;
    riscv_csrr(env, csrno, &val, retpc);
    return val;
}

typedef RISCVException (*riscv_csr_predicate_fn)(CPURISCVState *env,
                                                 int csrno);
typedef RISCVException (*riscv_csr_read_fn)(CPURISCVState *env, int csrno,
                                            target_ulong *ret_value);
typedef RISCVException (*riscv_csr_write_fn)(CPURISCVState *env, int csrno,
                                             target_ulong new_value,
                                             uintptr_t ra);
typedef RISCVException (*riscv_csr_op_fn)(CPURISCVState *env, int csrno,
                                          target_ulong *ret_value,
                                          target_ulong new_value,
                                          target_ulong write_mask);
typedef void (*riscv_csr_log_update_fn)(CPURISCVState *env, int csrno,
                                        target_ulong new_value);

RISCVException riscv_csrr_i128(CPURISCVState *env, int csrno,
                               Int128 *ret_value, uintptr_t retpc);
RISCVException riscv_csrrw_i128(CPURISCVState *env, int csrno,
                                Int128 *ret_value,
                                Int128 new_value, Int128 write_mask,
                                uintptr_t retpc);

typedef RISCVException (*riscv_csr_read128_fn)(CPURISCVState *env, int csrno,
                                               Int128 *ret_value);
typedef RISCVException (*riscv_csr_write128_fn)(CPURISCVState *env, int csrno,
                                             Int128 new_value);

typedef struct {
    const char *name;
    riscv_csr_predicate_fn predicate;
    riscv_csr_read_fn read;
    riscv_csr_write_fn write;
    riscv_csr_op_fn op;
    riscv_csr_read128_fn read128;
    riscv_csr_write128_fn write128;
    /* The default priv spec version should be PRIV_VERSION_1_10_0 (i.e 0) */
    uint32_t min_priv_ver;
} riscv_csr_operations;

struct RISCVCSR {
    int csrno;
    bool (*insertion_test)(RISCVCPU *cpu);
    riscv_csr_operations csr_ops;
};

/* CSR function table constants */
enum {
    CSR_TABLE_SIZE = 0x1000
};

/*
 * The event id are encoded based on the encoding specified in the
 * SBI specification v0.3
 */

enum riscv_pmu_event_idx {
    RISCV_PMU_EVENT_HW_CPU_CYCLES = 0x01,
    RISCV_PMU_EVENT_HW_INSTRUCTIONS = 0x02,
    RISCV_PMU_EVENT_CACHE_DTLB_READ_MISS = 0x10019,
    RISCV_PMU_EVENT_CACHE_DTLB_WRITE_MISS = 0x1001B,
    RISCV_PMU_EVENT_CACHE_ITLB_PREFETCH_MISS = 0x10021,
};

/* used by tcg/tcg-cpu.c*/
void isa_ext_update_enabled(RISCVCPU *cpu, uint32_t ext_offset, bool en);
bool isa_ext_is_enabled(RISCVCPU *cpu, uint32_t ext_offset);
void riscv_cpu_set_misa_ext(CPURISCVState *env, uint32_t ext);
bool riscv_cpu_is_vendor(Object *cpu_obj);

typedef struct RISCVCPUMultiExtConfig {
    const char *name;
    uint32_t offset;
    bool enabled;
} RISCVCPUMultiExtConfig;

extern const RISCVCPUMultiExtConfig riscv_cpu_extensions[];
extern const RISCVCPUMultiExtConfig riscv_cpu_vendor_exts[];
extern const RISCVCPUMultiExtConfig riscv_cpu_experimental_exts[];
extern const RISCVCPUMultiExtConfig riscv_cpu_named_features[];

typedef struct isa_ext_data {
    const char *name;
    int min_version;
    int ext_enable_offset;
} RISCVIsaExtData;
extern const RISCVIsaExtData isa_edata_arr[];
char *riscv_cpu_get_name(RISCVCPU *cpu);

void riscv_cpu_finalize_features(RISCVCPU *cpu, Error **errp);
void riscv_add_satp_mode_properties(Object *obj);
bool riscv_cpu_accelerator_compatible(RISCVCPU *cpu);

/* CSR function table */
extern riscv_csr_operations csr_ops[CSR_TABLE_SIZE];

extern const bool valid_vm_1_10_32[], valid_vm_1_10_64[];

void riscv_get_csr_ops(int csrno, riscv_csr_operations *ops);
void riscv_set_csr_ops(int csrno, const riscv_csr_operations *ops);

void riscv_cpu_register_gdb_regs_for_features(CPUState *cs);

#ifdef TARGET_CHERI
typedef struct _csr_cap_ops riscv_csr_cap_ops;
typedef cap_register_t (*riscv_csr_cap_read_fn)(CPURISCVState *env,
                                                riscv_csr_cap_ops *cap);
typedef void (*riscv_csr_cap_write_fn)(CPURISCVState *env,
                                       riscv_csr_cap_ops *cap,
                                       cap_register_t src, target_ulong newval,
                                       bool clen);

#define CSR_OP_REQUIRE_CRE   (1 << 0)
#define CSR_OP_IA_CONVERSION (1 << 1)
#define CSR_OP_UPDATE_SCADDR (1 << 2)
#define CSR_OP_EXTENDED_REG  (1 << 3)
#define CSR_OP_IS_CODE_PTR   (1 << 4)
#define CSR_OP_DIRECT_WRITE  (0)

struct _csr_cap_ops {
    const char *name;
    uint32_t reg_num;
    riscv_csr_cap_read_fn read;
    riscv_csr_cap_write_fn write;
    uint8_t flags;
};
riscv_csr_cap_ops *get_csr_cap_info(uint32_t csrnum);
cap_register_t *get_cap_csr(CPUArchState *env, uint32_t index);

/* Do the CRE bits allow cheri access in the current CPU mode? */
static inline bool riscv_cpu_mode_cre(CPURISCVState *env)
{
#ifdef TARGET_CHERI_RISCV_V9
    return env_archcpu(env)->cfg.ext_cheri;
#else
    /*
     * CRE bits are defined only if Zcherihybrid is supported.
     * For Zcheripurecap, cheri register access is always allowed.
     */
    if (!riscv_has_cheri_hybrid(env)) {
        return true;
    }

    if (env->mseccfg & MSECCFG_CRE) {
        /* CRE bits allow cheri in M mode */
        if (env->priv == PRV_M)
            return true;

        if (env->menvcfg & MENVCFG_CRE) {
            /* CRE bits allow cheri in S mode (and in M mode) */
            if (env->priv == PRV_S)
                return true;

            if (env->senvcfg & SENVCFG_CRE) {
                /* CRE bits allow cheri in U mode (and in M, S modes) */
                if (env->priv == PRV_U)
                    return true;
            }
        }
    }

    /*
     * For now, we do not support the hypervisor extension. It'll probably
     * have another CRE bit for H mode.
     */

    return false;
#endif
}
#endif

target_ulong riscv_new_csr_seed(target_ulong new_value,
                                target_ulong write_mask);

const char *satp_mode_str(uint8_t satp_mode, bool is_32_bit);

/* In th_csr.c */
extern const RISCVCSR th_csr_list[];

const char *priv_spec_to_str(int priv_version);
#endif /* RISCV_CPU_H */

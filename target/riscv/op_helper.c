/*
 * RISC-V Emulation Helpers for QEMU.
 *
 * Copyright (c) 2016-2017 Sagar Karandikar, sagark@eecs.berkeley.edu
 * Copyright (c) 2017-2018 SiFive, Inc.
 * Copyright (c) 2022      VRULL GmbH
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

#include "qemu/osdep.h"
#include "cpu.h"
#include "internals.h"
#include "qemu/error-report.h"
#include "exec/cputlb.h"
#include "accel/tcg/cpu-ldst.h"
#include "accel/tcg/probe.h"
#include "exec/helper-proto.h"
#include "exec/tlb-flags.h"
#include "exec/tswap.h"
#include "system/tcg.h"


#include "trace.h"
#ifdef TARGET_CHERI
#include "cheri-helper-utils.h"
#include "cheri_tagmem.h"
#endif

#ifndef CONFIG_USER_ONLY
static inline MemOp mo_endian_env(CPURISCVState *env)
{
    /*
     * A couple of bits in MSTATUS set the endianness:
     *  - MSTATUS_UBE (User-mode),
     *  - MSTATUS_SBE (Supervisor-mode),
     *  - MSTATUS_MBE (Machine-mode)
     * but we don't implement that yet.
     */
    return MO_TE;
}
#endif

/* Exceptions processing helpers */
G_NORETURN void riscv_raise_exception(CPURISCVState *env,
                                      RISCVException exception,
                                      uintptr_t pc)
{
    CPUState *cs = env_cpu(env);

    trace_riscv_exception(exception,
                          riscv_cpu_get_trap_name(exception, false),
                          PC_ADDR(env));

    cs->exception_index = exception;
    // Expand this call to print debug info: cpu_loop_exit_restore(cs, pc);
    if (pc) {
        cpu_restore_state(cs, pc);
    }
#ifdef CONFIG_RVFI_DII
    if (exception == RISCV_EXCP_ILLEGAL_INST &&
        env->rvfi_dii_have_injected_insn) {
        env->badaddr = env->rvfi_dii_injected_insn;
    } else
#endif
    if (exception == RISCV_EXCP_ILLEGAL_INST) {
        // Try to fetch the faulting instruction and store it in badaddr
        uint32_t opcode = 0;
        int ret = cpu_memory_rw_debug(env_cpu(env), PC_ADDR(env),
                                      (uint8_t *)&opcode, sizeof(opcode),
                                      /*is_write=*/false);
        opcode = tswap32(opcode); // FIXME is this needed?
        if (ret != 0 && PC_ADDR(env) != 0) {
            warn_report("RISCV_EXCP_ILLEGAL_INST: Could not read %zu bytes at "
                        "vaddr 0x" TARGET_FMT_lx "\r\n",
                        sizeof(opcode), PC_ADDR(env));
        } else {
            env->badaddr = opcode;
        }
    }
    if (exception == RISCV_EXCP_ILLEGAL_INST) {
        /*
         * Since QEMU 8.x riscv_cpu_do_interrupt() reports env->bins as the
         * illegal-instruction tval; helpers raising it at runtime (rather
         * than via decode_save_opc()) only filled badaddr, giving tval 0.
         */
        env->bins = env->badaddr;
    }
    cpu_loop_exit(cs);
}

void helper_raise_exception(CPURISCVState *env, uint32_t exception)
{
    riscv_raise_exception(env, exception, 0);
}

target_ulong helper_csrr(CPURISCVState *env, int csr)
{
    /*
     * The seed CSR must be accessed with a read-write instruction. A
     * read-only instruction such as CSRRS/CSRRC with rs1=x0 or CSRRSI/
     * CSRRCI with uimm=0 will raise an illegal instruction exception.
     */
    if (csr == CSR_SEED) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
    }

    target_ulong val = 0;
    RISCVException ret = riscv_csrr(env, csr, &val, GETPC());

    if (ret != RISCV_EXCP_NONE) {
        riscv_raise_exception(env, ret, GETPC());
    }
    return val;
}

void helper_csrw(CPURISCVState *env, int csr, target_ulong src)
{
    target_ulong mask = env->xl == MXL_RV32 ? UINT32_MAX : (target_ulong)-1;
    RISCVException ret = riscv_csrrw(env, csr, NULL, src, mask, GETPC());

    if (ret != RISCV_EXCP_NONE) {
        riscv_raise_exception(env, ret, GETPC());
    }
}

target_ulong helper_csrrw(CPURISCVState *env, int csr,
                          target_ulong src, target_ulong write_mask)
{
    target_ulong val = 0;
    RISCVException ret = riscv_csrrw(env, csr, &val, src, write_mask, GETPC());

    if (ret != RISCV_EXCP_NONE) {
        riscv_raise_exception(env, ret, GETPC());
    }
    return val;
}

#ifndef TARGET_CHERI
target_ulong helper_csrr_i128(CPURISCVState *env, int csr)
{
    Int128 rv = int128_zero();
    RISCVException ret = riscv_csrr_i128(env, csr, &rv, GETPC());

    if (ret != RISCV_EXCP_NONE) {
        riscv_raise_exception(env, ret, GETPC());
    }

    env->retxh = int128_gethi(rv);
    return int128_getlo(rv);
}

void helper_csrw_i128(CPURISCVState *env, int csr,
                      target_ulong srcl, target_ulong srch)
{
    RISCVException ret = riscv_csrrw_i128(env, csr, NULL,
                                          int128_make128(srcl, srch),
                                          UINT128_MAX, GETPC());

    if (ret != RISCV_EXCP_NONE) {
        riscv_raise_exception(env, ret, GETPC());
    }
}

target_ulong helper_csrrw_i128(CPURISCVState *env, int csr,
                               target_ulong srcl, target_ulong srch,
                               target_ulong maskl, target_ulong maskh)
{
    Int128 rv = int128_zero();
    RISCVException ret = riscv_csrrw_i128(env, csr, &rv,
                                          int128_make128(srcl, srch),
                                          int128_make128(maskl, maskh),
                                          GETPC());

    if (ret != RISCV_EXCP_NONE) {
        riscv_raise_exception(env, ret, GETPC());
    }

    env->retxh = int128_gethi(rv);
    return int128_getlo(rv);
}
#endif /* TARGET_CHERI */
/*
 * check_zicbo_envcfg
 *
 * Raise virtual exceptions and illegal instruction exceptions for
 * Zicbo[mz] instructions based on the settings of [mhs]envcfg as
 * specified in section 2.5.1 of the CMO specification.
 */
static void check_zicbo_envcfg(CPURISCVState *env, target_ulong envbits,
                                uintptr_t ra)
{
#ifndef CONFIG_USER_ONLY
    if ((env->priv < PRV_M) && !get_field(env->menvcfg, envbits)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, ra);
    }

    if (env->virt_enabled &&
        (((env->priv <= PRV_S) && !get_field(env->henvcfg, envbits)) ||
         ((env->priv < PRV_S) && !get_field(env->senvcfg, envbits)))) {
        riscv_raise_exception(env, RISCV_EXCP_VIRT_INSTRUCTION_FAULT, ra);
    }

    if ((env->priv < PRV_S) && !get_field(env->senvcfg, envbits)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, ra);
    }
#endif
}

static void do_cbo_zero(CPURISCVState *env, target_ulong address, uintptr_t ra)
{
    RISCVCPU *cpu = env_archcpu(env);
    uint16_t cbozlen = cpu->cfg.cboz_blocksize;
    int mmu_idx = riscv_env_mmu_index(env, false);
    void *mem;

    /* Caller must pass an address that is aligned-down to the cache-block. */
    g_assert(QEMU_IS_ALIGNED(address, cbozlen));

    /*
     * cbo.zero requires MMU_DATA_STORE access. Do a probe_write()
     * to raise any exceptions, including PMP.
     */
    mem = probe_write(env, address, cbozlen, mmu_idx, ra);

    if (likely(mem)) {
#ifdef TARGET_CHERI
        RAMBlock *r;
        ram_addr_t offs;

        /* TODO: Memory update and tag change must be atomic. */
        assert(!qemu_tcg_mttcg_enabled() ||
                cpu_in_exclusive_context(env_cpu(env)));

        rcu_read_lock(); /* protect r from changes while we use it */
        r = qemu_ram_block_from_host(mem, /* round to page? */ false, &offs);
        if (r) {
            cheri_tag_phys_invalidate(env, r, offs, cbozlen, NULL);
        }
        rcu_read_unlock();
#endif
        memset(mem, 0, cbozlen);
    } else {
        /*
         * This means that we're dealing with an I/O page. Section 4.2
         * of cmobase v1.0.1 says:
         *
         * "Cache-block zero instructions store zeros independently
         * of whether data from the underlying memory locations are
         * cacheable."
         *
         * Write zeros in address + cbozlen regardless of not being
         * a RAM page.
         */
        for (int i = 0; i < cbozlen; i++) {
            cpu_stb_mmuidx_ra(env, address + i, 0, mmu_idx, ra);
        }
    }
}

void helper_cbo_zero(CPURISCVState *env, target_ulong address)
{
    RISCVCPU *cpu = env_archcpu(env);
    uint16_t cbozlen = cpu->cfg.cboz_blocksize;
    uintptr_t ra = GETPC();

    check_zicbo_envcfg(env, MENVCFG_CBZE, ra);

    /* Mask off low-bits to align-down to the cache-block. */
    address &= ~(cbozlen - 1);

    do_cbo_zero(env, address, ra);
}

#ifdef TARGET_CHERI
void helper_cbo_zero_cap(CPURISCVState *env, uint32_t addr_reg)
{
    uintptr_t _host_return_address = GETPC();
    RISCVCPU *cpu = env_archcpu(env);
    check_zicbo_envcfg(env, MENVCFG_CBZE, _host_return_address);
    uint32_t auth_reg = cheri_in_capmode(env) ? addr_reg : CHERI_EXC_REGNUM_DDC;
    const cap_register_t *auth_cap = get_capreg_or_special(env, auth_reg);
    if (!auth_cap->cr_tag) {
        raise_cheri_exception(env, CapEx_TagViolation, auth_reg);
    } else if (!cap_is_unsealed(auth_cap)) {
        raise_cheri_exception(env, CapEx_SealViolation, auth_reg);
    }
    if (!cap_has_perms(auth_cap, CAP_PERM_STORE)) {
        raise_cheri_exception(env, CapEx_PermitStoreViolation, auth_reg);
    }
    if (cap_has_invalid_perms_encoding(env, auth_cap)) {
        raise_cheri_exception(env, CapEx_UserDefViolation, auth_reg);
    }
    target_ulong address = get_capreg_cursor(env, addr_reg);
    uint16_t cbozlen = cpu->cfg.cboz_blocksize;
    /* Mask off low-bits to align-down to the cache-block. */
    address &= ~(cbozlen - 1);

    if (!cap_is_in_bounds(auth_cap, address, cbozlen)) {
        raise_cheri_exception(env, CapEx_LengthViolation, auth_reg);
    }

    do_cbo_zero(env, address, _host_return_address);
}
#endif

/*
 * check_zicbom_access
 *
 * Check access permissions (LOAD, STORE or FETCH as specified in
 * section 2.5.2 of the CMO specification) for Zicbom, raising
 * either store page-fault (non-virtualized) or store guest-page
 * fault (virtualized).
 */
static void check_zicbom_access(CPURISCVState *env,
                                target_ulong address,
                                uintptr_t ra)
{
    RISCVCPU *cpu = env_archcpu(env);
    int mmu_idx = riscv_env_mmu_index(env, false);
    uint16_t cbomlen = cpu->cfg.cbom_blocksize;
    void *phost;
    int ret;

    /* Mask off low-bits to align-down to the cache-block. */
    address &= ~(cbomlen - 1);

    /*
     * Section 2.5.2 of cmobase v1.0.1:
     *
     * "A cache-block management instruction is permitted to
     * access the specified cache block whenever a load instruction
     * or store instruction is permitted to access the corresponding
     * physical addresses. If neither a load instruction nor store
     * instruction is permitted to access the physical addresses,
     * but an instruction fetch is permitted to access the physical
     * addresses, whether a cache-block management instruction is
     * permitted to access the cache block is UNSPECIFIED."
     */

    /*
     * Please note that in qemu 6.x, probe_access_flags does not yet have a
     * size parameter. Upstream commit 1770b2f2d3d ("accel/tcg: Add 'size'
     * param to probe_access_flags()") adds the size parameter and explains
     * the background. On risc-v systems, size is used for checking different
     * PMP permissions within a page.
     *
     * The upstream implementation of zicbom calls probe_access_flags with
     * size = cbomlen.
     */

    ret = probe_access_flags(env, address, cbomlen, MMU_DATA_LOAD,
                             mmu_idx, true, &phost, ra);
    if (ret != TLB_INVALID_MASK) {
        /* Success: readable */
        return;
    }

    /*
     * Since not readable, must be writable. On failure, store
     * fault/store guest amo fault will be raised by
     * riscv_cpu_tlb_fill(). PMP exceptions will be caught
     * there as well.
     */
    probe_write(env, address, cbomlen, mmu_idx, ra);
}

void helper_cbo_clean_flush(CPURISCVState *env, target_ulong address)
{
    uintptr_t ra = GETPC();
    check_zicbo_envcfg(env, MENVCFG_CBCFE, ra);
    check_zicbom_access(env, address, ra);

    /* We don't emulate the cache-hierarchy, so we're done. */
}
#ifdef TARGET_CHERI
void helper_cbo_clean_flush_cap(CPURISCVState *env, uint32_t addr_reg)
{
    uintptr_t _host_return_address = GETPC();
    RISCVCPU *cpu = env_archcpu(env);
    uint32_t perms_req = CAP_PERM_STORE | CAP_PERM_LOAD;
    check_zicbo_envcfg(env, MENVCFG_CBCFE, _host_return_address);

    uint32_t auth_reg = cheri_in_capmode(env) ? addr_reg : CHERI_EXC_REGNUM_DDC;
    const cap_register_t *auth_cap = get_capreg_or_special(env, auth_reg);
    if (!auth_cap->cr_tag) {
        raise_cheri_exception(env, CapEx_TagViolation, auth_reg);
    }
    if (!cap_is_unsealed(auth_cap)) {
        raise_cheri_exception(env, CapEx_SealViolation, auth_reg);
    }
    if (!cap_has_perms(auth_cap, perms_req)) {
        raise_cheri_exception(env, CapEx_PermitStoreViolation, auth_reg);
    }
    if (cap_has_invalid_perms_encoding(env, auth_cap)) {
        raise_cheri_exception(env, CapEx_UserDefViolation, auth_reg);
    }

    target_ulong address = get_capreg_cursor(env, addr_reg);
    uint16_t cbomlen = cpu->cfg.cbom_blocksize;
    /* Mask off low-bits to align-down to the cache-block. */
    address &= ~(cbomlen - 1);

    /* Check if the block is entirely outside the bounds */
    if ((address >= cap_get_top_full(auth_cap)) ||
        (cap_get_base(auth_cap) >= (cap_length_t)address + cbomlen)) {
        raise_cheri_exception(env, CapEx_LengthViolation, auth_reg);
    }
    check_zicbom_access(env, address, _host_return_address);
    /* We don't emulate the cache-hierarchy, so we're done. */
}
#endif


void helper_cbo_inval(CPURISCVState *env, target_ulong address)
{
    uintptr_t ra = GETPC();
    check_zicbo_envcfg(env, MENVCFG_CBIE, ra);
    check_zicbom_access(env, address, ra);

    /* We don't emulate the cache-hierarchy, so we're done. */
}

#ifdef TARGET_CHERI
void helper_cbo_inval_cap(CPURISCVState *env, uint32_t addr_reg)
{
    uintptr_t _host_return_address = GETPC();
    RISCVCPU *cpu = env_archcpu(env);
    uint32_t perms_req = CAP_PERM_STORE | CAP_PERM_LOAD;

    /* Note: Capability checks are performed even when treated as flush */
    check_zicbo_envcfg(env, MENVCFG_CBIE, _host_return_address);
    uint32_t auth_reg = cheri_in_capmode(env) ? addr_reg : CHERI_EXC_REGNUM_DDC;
    const cap_register_t *auth_cap = get_capreg_or_special(env, auth_reg);
    if (!cheri_have_access_sysregs(env)) {
        raise_cheri_exception(env, CapEx_AccessSystemRegsViolation,
                              CHERI_EXC_REGNUM_PCC);
    }
    if (!auth_cap->cr_tag) {
        raise_cheri_exception(env, CapEx_TagViolation, auth_reg);
    }
    if (!cap_is_unsealed(auth_cap)) {
        raise_cheri_exception(env, CapEx_SealViolation, auth_reg);
    }
    if (!cap_has_perms(auth_cap, perms_req)) {
        raise_cheri_exception(env, CapEx_PermitStoreViolation, auth_reg);
    }
    if (cap_has_invalid_perms_encoding(env, auth_cap)) {
        raise_cheri_exception(env, CapEx_UserDefViolation, auth_reg);
    }

    target_ulong address = get_capreg_cursor(env, addr_reg);
    uint16_t cbomlen = cpu->cfg.cbom_blocksize;
    /* Mask off low-bits to align-down to the cache-block. */
    address &= ~(cbomlen - 1);

    /* Check if the block is entirely outside the bounds */
    if ((address >= cap_get_top_full(auth_cap)) ||
        (cap_get_base(auth_cap) >= (cap_length_t)address + cbomlen)) {
        raise_cheri_exception(env, CapEx_LengthViolation, auth_reg);
    }
    check_zicbom_access(env, address, _host_return_address);
}
#endif

#ifndef CONFIG_USER_ONLY

target_ulong helper_sret(CPURISCVState *env)
{
    uint64_t mstatus;
    target_ulong prev_priv, prev_virt = env->virt_enabled;
    const target_ulong src_priv = env->priv;
    const bool src_virt = env->virt_enabled;

    if (!(env->priv >= PRV_S)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
    }
#ifdef TARGET_CHERI
    if (!cheri_have_access_sysregs(env)) {
        raise_cheri_exception_impl(env, CapEx_AccessSystemRegsViolation,
                                   CHERI_EXC_REGNUM_PCC, 0, true, GETPC());
    }
#endif

    target_ulong retpc = GET_SPECIAL_REG_ADDR(env, sepc, sepcc) & get_xepc_mask(env);
    // We have to clear the low bit of the address since that is defined as zero
    // in the privileged spec. The cheri_update_pcc_for_exc_return() check below
    // will de-tag pcc if this would result changing the address for sealed caps.
    // If RVC is not supported, we also mask sepc[1] as specified in the RISC-V
    // privileged spec 4.1.7 Supervisor Exception Program Counter (sepc):
    // "This masking occurs also for the implicit read by the SRET instruction."
    retpc &= ~(target_ulong)(riscv_cpu_allow_16bit_insn(&env_archcpu(env)->cfg,
                                                        env->priv_ver,
                                                        env->misa_ext) ? 1 : 3);

    if (get_field(env->mstatus, MSTATUS_TSR) && !(env->priv >= PRV_M)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
    }

    if (env->virt_enabled && get_field(env->hstatus, HSTATUS_VTSR)) {
        riscv_raise_exception(env, RISCV_EXCP_VIRT_INSTRUCTION_FAULT, GETPC());
    }

    mstatus = env->mstatus;
    prev_priv = get_field(mstatus, MSTATUS_SPP);
    mstatus = set_field(mstatus, MSTATUS_SIE,
                        get_field(mstatus, MSTATUS_SPIE));
    mstatus = set_field(mstatus, MSTATUS_SPIE, 1);
    mstatus = set_field(mstatus, MSTATUS_SPP, PRV_U);

    if (riscv_cpu_cfg(env)->ext_ssdbltrp) {
        if (riscv_has_ext(env, RVH)) {
            target_ulong prev_vu = get_field(env->hstatus, HSTATUS_SPV) &&
                                   prev_priv == PRV_U;
            /* Returning to VU from HS, vsstatus.sdt = 0 */
            if (!env->virt_enabled && prev_vu) {
                env->vsstatus = set_field(env->vsstatus, MSTATUS_SDT, 0);
            }
        }
        mstatus = set_field(mstatus, MSTATUS_SDT, 0);
    }
    if (riscv_cpu_cfg(env)->ext_smdbltrp && env->priv >= PRV_M) {
        mstatus = set_field(mstatus, MSTATUS_MDT, 0);
    }
    if (env->priv_ver >= PRIV_VERSION_1_12_0) {
        mstatus = set_field(mstatus, MSTATUS_MPRV, 0);
    }
    env->mstatus = mstatus;
    riscv_log_instr_csr_changed(env, CSR_MSTATUS);

    if (riscv_has_ext(env, RVH) && !env->virt_enabled) {
        /* We support Hypervisor extensions and virtulisation is disabled */
        target_ulong hstatus = env->hstatus;

        prev_virt = get_field(hstatus, HSTATUS_SPV);
        hstatus = set_field(hstatus, HSTATUS_SPV, 0);

        env->hstatus = hstatus;

        if (prev_virt) {
            riscv_cpu_swap_hypervisor_regs(env, /*hs_mode_trap*/true);
        }
    }

    riscv_cpu_set_mode(env, prev_priv, prev_virt);

#ifdef TARGET_CHERI
    cheri_update_pcc_for_exc_return(&env->pcc, &env->sepcc, retpc);
    /* TODO(am2419): do we log PCC as a changed register? */
    qemu_log_instr_dbg_cap(env, "PCC", &env->pcc);
#endif

    /*
     * If forward cfi enabled for new priv, restore elp status
     * and clear spelp in mstatus
     */
    if (cpu_get_fcfien(env)) {
        env->elp = get_field(env->mstatus, MSTATUS_SPELP);
    }
    env->mstatus = set_field(env->mstatus, MSTATUS_SPELP, 0);
    if (riscv_cpu_cfg(env)->ext_smctr || riscv_cpu_cfg(env)->ext_ssctr) {
        riscv_ctr_add_entry(env, GET_SPECIAL_REG_ADDR(env, pc, pcc), retpc,
                            CTRDATA_TYPE_EXCEP_INT_RET, src_priv, src_virt);
    }
    return retpc;
}

static void check_ret_from_m_mode(CPURISCVState *env, target_ulong retpc,
                                  target_ulong prev_priv,
                                  uintptr_t ra)
{
    if (!(env->priv >= PRV_M)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, ra);
    }
#ifdef TARGET_CHERI
    if (!cheri_have_access_sysregs(env)) {
        raise_cheri_exception_impl(env, CapEx_AccessSystemRegsViolation,
                                   CHERI_EXC_REGNUM_PCC, 0, true, ra);
    }
#endif

#if 0
    /* FIXME: upstream diff seems wrong, the ifetch should fail not the mret */
    if (riscv_cpu_cfg(env)->pmp &&
        !pmp_get_num_rules(env) && (prev_priv != PRV_M)) {
        riscv_raise_exception(env, RISCV_EXCP_INST_ACCESS_FAULT, ra);
    }
#endif
}

static target_ulong ssdbltrp_mxret(CPURISCVState *env, target_ulong mstatus,
                                   target_ulong prev_priv,
                                   target_ulong prev_virt)
{
    /* If returning to U, VS or VU, sstatus.sdt = 0 */
    if (prev_priv == PRV_U || (prev_virt &&
        (prev_priv == PRV_S || prev_priv == PRV_U))) {
        mstatus = set_field(mstatus, MSTATUS_SDT, 0);
        /* If returning to VU, vsstatus.sdt = 0 */
        if (prev_virt && prev_priv == PRV_U) {
            env->vsstatus = set_field(env->vsstatus, MSTATUS_SDT, 0);
        }
    }

    return mstatus;
}

target_ulong helper_mret(CPURISCVState *env)
{
    target_ulong retpc = GET_SPECIAL_REG_ADDR(env, mepc, mepcc) & get_xepc_mask(env);
    // We have to clear the low bit of the address since that is defined as zero
    // in the privileged spec. The cheri_update_pcc_for_exc_return() check below
    // will de-tag pcc if this would result changing the address for sealed caps.
    // If RVC is not supported, we also mask sepc[1] as specified in the RISC-V
    // privileged spec 3.1.15 Machine Exception Program Counter (mepc):
    // "This masking occurs also for the implicit read by the MRET instruction."
    retpc &= ~(target_ulong)(riscv_cpu_allow_16bit_insn(&env_archcpu(env)->cfg,
                                                        env->priv_ver,
                                                        env->misa_ext) ? 1 : 3);
    uint64_t mstatus = env->mstatus;
    target_ulong prev_priv = get_field(mstatus, MSTATUS_MPP);
    uintptr_t ra = GETPC();

    check_ret_from_m_mode(env, retpc, prev_priv, ra);

    target_ulong prev_virt = get_field(env->mstatus, MSTATUS_MPV) &&
                             (prev_priv != PRV_M);
    mstatus = set_field(mstatus, MSTATUS_MIE,
                        get_field(mstatus, MSTATUS_MPIE));
    mstatus = set_field(mstatus, MSTATUS_MPIE, 1);
    mstatus = set_field(mstatus, MSTATUS_MPP,
                        riscv_has_ext(env, RVU) ? PRV_U : PRV_M);
    mstatus = set_field(mstatus, MSTATUS_MPV, 0);
    if (riscv_cpu_cfg(env)->ext_ssdbltrp) {
        mstatus = ssdbltrp_mxret(env, mstatus, prev_priv, prev_virt);
    }
    if (riscv_cpu_cfg(env)->ext_smdbltrp) {
        mstatus = set_field(mstatus, MSTATUS_MDT, 0);
    }
    if ((env->priv_ver >= PRIV_VERSION_1_12_0) && (prev_priv != PRV_M)) {
        mstatus = set_field(mstatus, MSTATUS_MPRV, 0);
    }
    env->mstatus = mstatus;

    if (riscv_has_ext(env, RVH) && prev_virt) {
        riscv_cpu_swap_hypervisor_regs(env, /*hs_mode_trap*/false);
    }

    riscv_cpu_set_mode(env, prev_priv, prev_virt);
    /*
     * If forward cfi enabled for new priv, restore elp status
     * and clear mpelp in mstatus
     */
    if (cpu_get_fcfien(env)) {
        env->elp = get_field(env->mstatus, MSTATUS_MPELP);
    }
    env->mstatus = set_field(env->mstatus, MSTATUS_MPELP, 0);

    if (riscv_cpu_cfg(env)->ext_smctr || riscv_cpu_cfg(env)->ext_ssctr) {
        riscv_ctr_add_entry(env, GET_SPECIAL_REG_ADDR(env, pc, pcc), retpc,
                            CTRDATA_TYPE_EXCEP_INT_RET, PRV_M, false);
    }

    riscv_log_instr_csr_changed(env, CSR_MSTATUS);
#ifdef TARGET_RISCV32
    riscv_log_instr_csr_changed(env, CSR_MSTATUSH);
#endif

#ifdef TARGET_CHERI
    cheri_update_pcc_for_exc_return(&env->pcc, &env->mepcc, retpc);
    /* TODO(am2419): do we log PCC as a changed register? */
    qemu_log_instr_dbg_cap(env, "PCC", &env->pcc);
#endif
    return retpc;
}

target_ulong helper_mnret(CPURISCVState *env)
{
    target_ulong retpc = env->mnepc;
    retpc &= ~(target_ulong)(riscv_cpu_allow_16bit_insn(&env_archcpu(env)->cfg,
                                                        env->priv_ver,
                                                        env->misa_ext) ? 1 : 3);
    target_ulong prev_priv = get_field(env->mnstatus, MNSTATUS_MNPP);
    target_ulong prev_virt;
    uintptr_t ra = GETPC();

    check_ret_from_m_mode(env, retpc, prev_priv, ra);

    prev_virt = get_field(env->mnstatus, MNSTATUS_MNPV) &&
                (prev_priv != PRV_M);
    env->mnstatus = set_field(env->mnstatus, MNSTATUS_NMIE, true);

    /*
     * If MNRET changes the privilege mode to a mode
     * less privileged than M, it also sets mstatus.MPRV to 0.
     */
    if (prev_priv < PRV_M) {
        env->mstatus = set_field(env->mstatus, MSTATUS_MPRV, false);
    }
    if (riscv_cpu_cfg(env)->ext_ssdbltrp) {
        env->mstatus = ssdbltrp_mxret(env, env->mstatus, prev_priv, prev_virt);
    }

    if (riscv_cpu_cfg(env)->ext_smdbltrp) {
        if (prev_priv < PRV_M) {
            env->mstatus = set_field(env->mstatus, MSTATUS_MDT, 0);
        }
    }

    if (riscv_has_ext(env, RVH) && prev_virt) {
        riscv_cpu_swap_hypervisor_regs(env, false);
    }

    riscv_cpu_set_mode(env, prev_priv, prev_virt);

    /*
     * If forward cfi enabled for new priv, restore elp status
     * and clear mnpelp in mnstatus
     */
    if (cpu_get_fcfien(env)) {
        env->elp = get_field(env->mnstatus, MNSTATUS_MNPELP);
    }
    env->mnstatus = set_field(env->mnstatus, MNSTATUS_MNPELP, 0);

    return retpc;
}

void helper_ctr_add_entry(CPURISCVState *env, target_ulong src,
                          target_ulong dest, target_ulong type)
{
    riscv_ctr_add_entry(env, src, dest, (enum CTRType)type,
                        env->priv, env->virt_enabled);
}

void helper_ctr_clear(CPURISCVState *env)
{
    /*
     * It's safe to call smstateen_acc_ok() for umode access regardless of the
     * state of bit 54 (CTR bit in case of m/hstateen) of sstateen. If the bit
     * is zero, smstateen_acc_ok() will return the correct exception code and
     * if it's one, smstateen_acc_ok() will return RISCV_EXCP_NONE. In that
     * scenario the U-mode check below will handle that case.
     */
    RISCVException ret = smstateen_acc_ok(env, 0, SMSTATEEN0_CTR);
    if (ret != RISCV_EXCP_NONE) {
        riscv_raise_exception(env, ret, GETPC());
    }

    if (env->priv == PRV_U) {
        /*
         * One corner case is when sctrclr is executed from VU-mode and
         * mstateen.CTR = 0, in which case we are supposed to raise
         * RISCV_EXCP_ILLEGAL_INST. This case is already handled in
         * smstateen_acc_ok().
         */
        uint32_t excep = env->virt_enabled ? RISCV_EXCP_VIRT_INSTRUCTION_FAULT :
            RISCV_EXCP_ILLEGAL_INST;
        riscv_raise_exception(env, excep, GETPC());
    }

    riscv_ctr_clear(env);
}

void HELPER(check_alignment)(CPURISCVState *env, target_ulong addr, MemOp op,
                             uint32_t exc)
{
    if (addr & (memop_size(op) - 1)) {
        env->badaddr = addr;
        riscv_raise_exception(env, exc, GETPC());
    }
}

void helper_wfi(CPURISCVState *env)
{
    CPUState *cs = env_cpu(env);
    bool rvs = riscv_has_ext(env, RVS);
    bool prv_u = env->priv == PRV_U;
    bool prv_s = env->priv == PRV_S;

    if (((prv_s || (!rvs && prv_u)) && get_field(env->mstatus, MSTATUS_TW)) ||
        (rvs && prv_u && !env->virt_enabled)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
    } else if (env->virt_enabled &&
               (prv_u || (prv_s && get_field(env->hstatus, HSTATUS_VTW)))) {
        riscv_raise_exception(env, RISCV_EXCP_VIRT_INSTRUCTION_FAULT, GETPC());
    } else {
        cs->halted = 1;
        cs->exception_index = EXCP_HLT;
        cpu_loop_exit(cs);
    }
}

void helper_wrs_nto(CPURISCVState *env)
{
    if (env->virt_enabled && (env->priv == PRV_S || env->priv == PRV_U) &&
        get_field(env->hstatus, HSTATUS_VTW) &&
        !get_field(env->mstatus, MSTATUS_TW)) {
        riscv_raise_exception(env, RISCV_EXCP_VIRT_INSTRUCTION_FAULT, GETPC());
    } else if (env->priv != PRV_M && get_field(env->mstatus, MSTATUS_TW)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
    }
}

void helper_tlb_flush(CPURISCVState *env)
{
    CPUState *cs = env_cpu(env);
    if (!env->virt_enabled &&
        (env->priv == PRV_U ||
         (env->priv == PRV_S && get_field(env->mstatus, MSTATUS_TVM)))) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
    } else if (env->virt_enabled &&
               (env->priv == PRV_U || get_field(env->hstatus, HSTATUS_VTVM))) {
        riscv_raise_exception(env, RISCV_EXCP_VIRT_INSTRUCTION_FAULT, GETPC());
    } else {
        tlb_flush(cs);
    }
}

void helper_tlb_flush_all(CPURISCVState *env)
{
    CPUState *cs = env_cpu(env);
    tlb_flush_all_cpus_synced(cs);
}

void helper_hyp_tlb_flush(CPURISCVState *env)
{
    CPUState *cs = env_cpu(env);

    if (env->virt_enabled) {
        riscv_raise_exception(env, RISCV_EXCP_VIRT_INSTRUCTION_FAULT, GETPC());
    }

    if (env->priv == PRV_M ||
        (env->priv == PRV_S && !env->virt_enabled)) {
        tlb_flush(cs);
        return;
    }

    riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
}

void helper_hyp_gvma_tlb_flush(CPURISCVState *env)
{
    if (env->priv == PRV_S && !env->virt_enabled &&
        get_field(env->mstatus, MSTATUS_TVM)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, GETPC());
    }

    helper_hyp_tlb_flush(env);
}

static int check_access_hlsv(CPURISCVState *env, bool x, uintptr_t ra)
{
    if (env->priv == PRV_M) {
        /* always allowed */
    } else if (env->virt_enabled) {
        riscv_raise_exception(env, RISCV_EXCP_VIRT_INSTRUCTION_FAULT, ra);
    } else if (env->priv == PRV_U && !get_field(env->hstatus, HSTATUS_HU)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST, ra);
    }

    int mode = get_field(env->hstatus, HSTATUS_SPVP);
    if (!x && mode == PRV_S && get_field(env->vsstatus, MSTATUS_SUM)) {
        mode = MMUIdx_S_SUM;
    }
    return mode | MMU_2STAGE_BIT;
}

target_ulong helper_hyp_hlv_bu(CPURISCVState *env, target_ulong addr)
{
    uintptr_t ra = GETPC();
    int mmu_idx = check_access_hlsv(env, false, ra);
    MemOpIdx oi = make_memop_idx(MO_UB, mmu_idx);

    return cpu_ldb_mmu(env, adjust_addr_virt(env, addr), oi, ra);
}

target_ulong helper_hyp_hlv_hu(CPURISCVState *env, target_ulong addr)
{
    uintptr_t ra = GETPC();
    int mmu_idx = check_access_hlsv(env, false, ra);
    MemOpIdx oi = make_memop_idx(mo_endian_env(env) | MO_UW, mmu_idx);

    return cpu_ldw_mmu(env, adjust_addr_virt(env, addr), oi, ra);
}

target_ulong helper_hyp_hlv_wu(CPURISCVState *env, target_ulong addr)
{
    uintptr_t ra = GETPC();
    int mmu_idx = check_access_hlsv(env, false, ra);
    MemOpIdx oi = make_memop_idx(mo_endian_env(env) | MO_UL, mmu_idx);

    return cpu_ldl_mmu(env, adjust_addr_virt(env, addr), oi, ra);
}

target_ulong helper_hyp_hlv_d(CPURISCVState *env, target_ulong addr)
{
    uintptr_t ra = GETPC();
    int mmu_idx = check_access_hlsv(env, false, ra);
    MemOpIdx oi = make_memop_idx(mo_endian_env(env) | MO_UQ, mmu_idx);

    return cpu_ldq_mmu(env, adjust_addr_virt(env, addr), oi, ra);
}

void helper_hyp_hsv_b(CPURISCVState *env, target_ulong addr, target_ulong val)
{
    uintptr_t ra = GETPC();
    int mmu_idx = check_access_hlsv(env, false, ra);
    MemOpIdx oi = make_memop_idx(MO_UB, mmu_idx);

    cpu_stb_mmu(env, adjust_addr_virt(env, addr), val, oi, ra);
}

void helper_hyp_hsv_h(CPURISCVState *env, target_ulong addr, target_ulong val)
{
    uintptr_t ra = GETPC();
    int mmu_idx = check_access_hlsv(env, false, ra);
    MemOpIdx oi = make_memop_idx(mo_endian_env(env) | MO_UW, mmu_idx);

    cpu_stw_mmu(env, adjust_addr_virt(env, addr), val, oi, ra);
}

void helper_hyp_hsv_w(CPURISCVState *env, target_ulong addr, target_ulong val)
{
    uintptr_t ra = GETPC();
    int mmu_idx = check_access_hlsv(env, false, ra);
    MemOpIdx oi = make_memop_idx(mo_endian_env(env) | MO_UL, mmu_idx);

    cpu_stl_mmu(env, adjust_addr_virt(env, addr), val, oi, ra);
}

void helper_hyp_hsv_d(CPURISCVState *env, target_ulong addr, target_ulong val)
{
    uintptr_t ra = GETPC();
    int mmu_idx = check_access_hlsv(env, false, ra);
    MemOpIdx oi = make_memop_idx(mo_endian_env(env) | MO_UQ, mmu_idx);

    cpu_stq_mmu(env, adjust_addr_virt(env, addr), val, oi, ra);
}

/*
 * TODO: These implementations are not quite correct.  They perform the
 * access using execute permission just fine, but the final PMP check
 * is supposed to have read permission as well.  Without replicating
 * a fair fraction of cputlb.c, fixing this requires adding new mmu_idx
 * which would imply that exact check in tlb_fill.
 */
target_ulong helper_hyp_hlvx_hu(CPURISCVState *env, target_ulong addr)
{
    uintptr_t ra = GETPC();
    int mmu_idx = check_access_hlsv(env, true, ra);
    MemOpIdx oi = make_memop_idx(mo_endian_env(env) | MO_UW, mmu_idx);

    return cpu_ldw_code_mmu(env, addr, oi, GETPC());
}

target_ulong helper_hyp_hlvx_wu(CPURISCVState *env, target_ulong addr)
{
    uintptr_t ra = GETPC();
    int mmu_idx = check_access_hlsv(env, true, ra);
    MemOpIdx oi = make_memop_idx(mo_endian_env(env) | MO_UL, mmu_idx);

    return cpu_ldl_code_mmu(env, addr, oi, ra);
}

void helper_ssamoswap_disabled(CPURISCVState *env)
{
    int exception = RISCV_EXCP_ILLEGAL_INST;

    /*
     * Here we follow the RISC-V CFI spec [1] to implement the exception type
     * of ssamoswap* instruction.
     *
     * [1] RISC-V CFI spec v1.0, ch2.7 Atomic Swap from a Shadow Stack Location
     *
     * Note: We have already checked some conditions in trans_* functions:
     *   1. The effective priv mode is not M-mode.
     *   2. The xSSE specific to the effictive priv mode is disabled.
     */
    if (!get_field(env->menvcfg, MENVCFG_SSE)) {
        /*
         * Disabled M-mode SSE always trigger illegal instruction when
         * current priv mode is not M-mode.
         */
        exception = RISCV_EXCP_ILLEGAL_INST;
        goto done;
    }

    if (!riscv_has_ext(env, RVS)) {
        /* S-mode is not implemented */
        exception = RISCV_EXCP_ILLEGAL_INST;
        goto done;
    } else if (env->virt_enabled) {
        /*
         * VU/VS-mode with disabled xSSE will trigger the virtual instruction
         * exception.
         */
        exception = RISCV_EXCP_VIRT_INSTRUCTION_FAULT;
        goto done;
    } else {
        /*
         * U-mode with disabled S-mode SSE will trigger the illegal instruction
         * exception.
         *
         * Note: S-mode is already handled in the disabled M-mode SSE case.
         */
        exception = RISCV_EXCP_ILLEGAL_INST;
        goto done;
    }

done:
    riscv_raise_exception(env, exception, GETPC());
}

#endif /* !CONFIG_USER_ONLY */

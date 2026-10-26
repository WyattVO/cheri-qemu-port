/*
 * RISC-V GDB Server Stub
 *
 * Copyright (c) 2016-2017 Sagar Karandikar, sagark@eecs.berkeley.edu
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
#include "exec/gdbstub.h"
#include "exec/log_instr.h"
#include "gdbstub/helpers.h"
#include "cpu.h"
#include "helper_utils.h"
#ifdef TARGET_CHERI
#include "cheri-helper-utils.h"
#endif

struct TypeSize {
    const char *gdb_type;
    const char *id;
    int size;
    const char suffix;
};

static const struct TypeSize vec_lanes[] = {
    /* quads */
    { "uint128", "quads", 128, 'q' },
    /* 64 bit */
    { "uint64", "longs", 64, 'l' },
    /* 32 bit */
    { "uint32", "words", 32, 'w' },
    /* 16 bit */
    { "uint16", "shorts", 16, 's' },
    /*
     * TODO: currently there is no reliable way of telling
     * if the remote gdb actually understands ieee_half so
     * we don't expose it in the target description for now.
     * { "ieee_half", 16, 'h', 'f' },
     */
    /* bytes */
    { "uint8", "bytes", 8, 'b' },
};

int riscv_cpu_gdb_read_register(CPUState *cs, GByteArray *mem_buf, int n)
{
    RISCVCPUClass *mcc = RISCV_CPU_GET_CLASS(cs);
    RISCVCPU *cpu = RISCV_CPU(cs);
    CPURISCVState *env = &cpu->env;
    uint64_t tmp;

    if (n < 32) {
        tmp = gpr_int_value(env, n);
    } else if (n == 32) {
        tmp = GET_SPECIAL_REG_ARCH(env, pc, pcc);
    } else {
        return 0;
    }

    switch (mcc->def->misa_mxl_max) {
    case MXL_RV32:
        return gdb_get_reg32(mem_buf, tmp);
    case MXL_RV64:
    case MXL_RV128:
        return gdb_get_reg64(mem_buf, tmp);
    default:
        g_assert_not_reached();
    }
    return 0;
}

int riscv_cpu_gdb_write_register(CPUState *cs, uint8_t *mem_buf, int n)
{
    RISCVCPUClass *mcc = RISCV_CPU_GET_CLASS(cs);
    RISCVCPU *cpu = RISCV_CPU(cs);
    CPURISCVState *env = &cpu->env;
    int length = 0;
    uint64_t tmp;

    switch (mcc->def->misa_mxl_max) {
    case MXL_RV32:
        tmp = (int32_t)ldl_p(mem_buf);
        length = 4;
        break;
    case MXL_RV64:
    case MXL_RV128:
        if (env->xl < MXL_RV64) {
            tmp = (int32_t)ldq_p(mem_buf);
        } else {
            tmp = ldq_p(mem_buf);
        }
        length = 8;
        break;
    default:
        g_assert_not_reached();
    }
    if (n > 0 && n < 32) {
        gpr_set_int_value(env, n, tmp);
    } else if (n == 32) {
        /* TODO(am2419): arguably we don't want to log changes from gdb */
        SET_SPECIAL_REG(env, pc, pcc, tmp);
    }

    return length;
}

static int riscv_gdb_get_fpu(CPUState *cs, GByteArray *buf, int n)
{
    RISCVCPU *cpu = RISCV_CPU(cs);
    CPURISCVState *env = &cpu->env;

    if (n < 32) {
        if (env->misa_ext & RVD) {
            return gdb_get_reg64(buf, env->fpr[n]);
        }
        if (env->misa_ext & RVF) {
            return gdb_get_reg32(buf, env->fpr[n]);
        }
    }
    return 0;
}

static int riscv_gdb_set_fpu(CPUState *cs, uint8_t *mem_buf, int n)
{
    RISCVCPU *cpu = RISCV_CPU(cs);
    CPURISCVState *env = &cpu->env;

    if (n < 32) {
        env->fpr[n] = ldq_p(mem_buf); /* always 64-bit */
        return sizeof(uint64_t);
    }
    return 0;
}

static int riscv_gdb_get_vector(CPUState *cs, GByteArray *buf, int n)
{
    RISCVCPU *cpu = RISCV_CPU(cs);
    CPURISCVState *env = &cpu->env;
    uint16_t vlenb = cpu->cfg.vlenb;
    if (n < 32) {
        int i;
        int cnt = 0;
        for (i = 0; i < vlenb; i += 8) {
            cnt += gdb_get_reg64(buf,
                                 env->vreg[(n * vlenb + i) / 8]);
        }
        return cnt;
    }

    return 0;
}

static int riscv_gdb_set_vector(CPUState *cs, uint8_t *mem_buf, int n)
{
    RISCVCPU *cpu = RISCV_CPU(cs);
    CPURISCVState *env = &cpu->env;
    uint16_t vlenb = cpu->cfg.vlenb;
    if (n < 32) {
        int i;
        for (i = 0; i < vlenb; i += 8) {
            env->vreg[(n * vlenb + i) / 8] = ldq_p(mem_buf + i);
        }
        return vlenb;
    }

    return 0;
}

static int riscv_gdb_get_csr(CPUState *cs, GByteArray *buf, int n)
{
    RISCVCPU *cpu = RISCV_CPU(cs);
    CPURISCVState *env = &cpu->env;

    if (n < CSR_TABLE_SIZE) {
        target_ulong val = 0;
        int result;

        result = riscv_csrrw_debug(env, n, &val, 0, 0);
        if (result == RISCV_EXCP_NONE) {
            return gdb_get_regl(buf, val);
        }
    }
    return 0;
}

static int riscv_gdb_set_csr(CPUState *cs, uint8_t *mem_buf, int n)
{
    RISCVCPU *cpu = RISCV_CPU(cs);
    CPURISCVState *env = &cpu->env;
    const unsigned regsz = riscv_cpu_is_32bit(cpu) ? 4 : 8;

    if (n < CSR_TABLE_SIZE) {
        uint64_t val = ldn_p(mem_buf, regsz);
        int result;

        result = riscv_csrrw_debug(env, n, NULL, val, -1);
        if (result == RISCV_EXCP_NONE) {
            return regsz;
        }
    }
    return 0;
}

static int riscv_gdb_get_virtual(CPUState *cs, GByteArray *buf, int n)
{
    if (n == 0) {
#ifdef CONFIG_USER_ONLY
        return gdb_get_regl(buf, 0);
#else
        RISCVCPU *cpu = RISCV_CPU(cs);
        CPURISCVState *env = &cpu->env;

        /* Per RiscV debug spec v1.0.0 rc4 */
        uint32_t vbit = (env->virt_enabled) ? BIT(2) : 0;

        return gdb_get_regl(buf, env->priv | vbit);
#endif
    }
    return 0;
}

static int riscv_gdb_set_virtual(CPUState *cs, uint8_t *mem_buf, int n)
{
    if (n == 0) {
        RISCVCPU *cpu = RISCV_CPU(cs);
        const unsigned regsz = riscv_cpu_is_32bit(cpu) ? 4 : 8;
#ifndef CONFIG_USER_ONLY
        CPURISCVState *env = &cpu->env;

        target_ulong new_priv = ldn_p(mem_buf, regsz) & 0x3;
        bool new_virt = 0;

        if (new_priv == PRV_RESERVED) {
            new_priv = PRV_S;
        }

        if (new_priv != PRV_M) {
            new_virt = (ldn_p(mem_buf, regsz) & BIT(2)) >> 2;
        }

        if (riscv_has_ext(env, RVH) && new_virt != env->virt_enabled) {
            riscv_cpu_swap_hypervisor_regs(env, false);
        }

        riscv_cpu_set_mode(env, new_priv, new_virt);
#endif
        return regsz;
    }
    return 0;
}


#if defined(TARGET_CHERI)
#define CHERI_GDB_NUM_GP_CAPREGS 32
#define CHERI_GDB_NUM_SPECIAL_CAPREGS 2
#define CHERI_GDB_NUM_CAPREGS (CHERI_GDB_NUM_GP_CAPREGS + CHERI_GDB_NUM_SPECIAL_CAPREGS)
#define CHERI_GDB_NUM_REGS (CHERI_GDB_NUM_CAPREGS)

static int riscv_gdb_get_cheri_reg(CPUState *cs, GByteArray *buf, int n)
{
    RISCVCPU *cpu = RISCV_CPU(cs);
    CPURISCVState *env = &cpu->env;
    if (n < 0)
        return 0;

    if (n < CHERI_GDB_NUM_GP_CAPREGS) {
        return gdb_get_general_purpose_capreg(buf, env, n);
    }
    switch (n) {
    case CHERI_GDB_NUM_GP_CAPREGS:
        return gdb_get_capreg(buf, cheri_get_current_pcc(env));
    case CHERI_GDB_NUM_GP_CAPREGS + 1:
        return gdb_get_capreg(buf, cheri_get_ddc(env));
    }
    return 0;
}

static int riscv_gdb_cheri_reg_no_write(CPUState *cs, uint8_t *mem_buf,
                                        int n)
{
    /* All CHERI registers are read-only currently.  */
    if (n <= CHERI_GDB_NUM_CAPREGS) {
        return CHERI_CAP_SIZE + 1;
    }
    return 0;
}
#endif

static GDBFeature *riscv_gen_dynamic_csr_feature(CPUState *cs, int base_reg)
{
    RISCVCPUClass *mcc = RISCV_CPU_GET_CLASS(cs);
    RISCVCPU *cpu = RISCV_CPU(cs);
    CPURISCVState *env = &cpu->env;
    GDBFeatureBuilder builder;
    riscv_csr_predicate_fn predicate;
    int bitsize = riscv_cpu_max_xlen(mcc);
    const char *name;
    int i;

#if !defined(CONFIG_USER_ONLY)
    env->debugger = true;
#endif

    /* Until gdb knows about 128-bit registers */
    if (bitsize > 64) {
        bitsize = 64;
    }

    gdb_feature_builder_init(&builder, &cpu->dyn_csr_feature,
                             "org.gnu.gdb.riscv.csr", "riscv-csr.xml",
                             base_reg);

    for (i = 0; i < CSR_TABLE_SIZE; i++) {
        if (env->priv_ver < csr_ops[i].min_priv_ver) {
            continue;
        }
        predicate = csr_ops[i].predicate;
        if (predicate && (predicate(env, i) == RISCV_EXCP_NONE)) {
            name = csr_ops[i].name;
            if (!name) {
                name = g_strdup_printf("csr%03x", i);
            }

            gdb_feature_builder_append_reg(&builder, name, bitsize, i,
                                           "int", NULL);
        }
    }

    gdb_feature_builder_end(&builder);

#if !defined(CONFIG_USER_ONLY)
    env->debugger = false;
#endif

    return &cpu->dyn_csr_feature;
}

static GDBFeature *ricsv_gen_dynamic_vector_feature(CPUState *cs, int base_reg)
{
    RISCVCPU *cpu = RISCV_CPU(cs);
    int bitsize = cpu->cfg.vlenb << 3;
    GDBFeatureBuilder builder;
    int i;

    gdb_feature_builder_init(&builder, &cpu->dyn_vreg_feature,
                             "org.gnu.gdb.riscv.vector", "riscv-vector.xml",
                             base_reg);

    /* First define types and totals in a whole VL */
    for (i = 0; i < ARRAY_SIZE(vec_lanes); i++) {
        int count = bitsize / vec_lanes[i].size;
        gdb_feature_builder_append_tag(
            &builder, "<vector id=\"%s\" type=\"%s\" count=\"%d\"/>",
            vec_lanes[i].id, vec_lanes[i].gdb_type, count);
    }

    /* Define unions */
    gdb_feature_builder_append_tag(&builder, "<union id=\"riscv_vector\">");
    for (i = 0; i < ARRAY_SIZE(vec_lanes); i++) {
        gdb_feature_builder_append_tag(&builder,
                                       "<field name=\"%c\" type=\"%s\"/>",
                                       vec_lanes[i].suffix, vec_lanes[i].id);
    }
    gdb_feature_builder_append_tag(&builder, "</union>");

    /* Define vector registers */
    for (i = 0; i < 32; i++) {
        gdb_feature_builder_append_reg(&builder, g_strdup_printf("v%d", i),
                                       bitsize, i, "riscv_vector", "vector");
    }

    gdb_feature_builder_end(&builder);

    return &cpu->dyn_vreg_feature;
}

#if defined(TARGET_CHERI_RISCV_V9)
static struct SCR {
    uint32_t csrno;
    const char *name;
    bool code;
} scrs[] = {
    { .csrno = CSR_STVECC, .name = "stcc", .code = true },
    { .csrno = CSR_STDC, .name = "stdc" },
    { .csrno = CSR_SSCRATCHC, .name = "sscratchc" },
    { .csrno = CSR_SEPCC, .name = "sepcc", .code = true },
    { .csrno = CSR_MTVECC, .name = "mtcc", .code = true },
    { .csrno = CSR_MTDC, .name = "mtdc" },
    { .csrno = CSR_MSCRATCHC, .name = "mscratchc" },
    { .csrno = CSR_MEPCC, .name = "mepcc", .code = true },
};

static int riscv_gdb_get_scr(CPUState *cs, GByteArray *buf, int n)
{
    RISCVCPU *cpu = RISCV_CPU(cs);
    CPURISCVState *env = &cpu->env;
    if (n < ARRAY_SIZE(scrs)) {
        cap_register_t *scr = get_cap_csr(env, scrs[n].csrno);
        return gdb_get_capreg(buf, scr);
    }
    return 0;
}

static GDBFeature *riscv_gen_dynamic_scr_feature(CPUState *cs, int base_reg)
{
    RISCVCPU *cpu = RISCV_CPU(cs);
    CPURISCVState *env = &cpu->env;
    GDBFeatureBuilder builder;
    int bitsize = riscv_cpu_mxl(env) == MXL_RV32 ? 64 : 128;
    int i;

    gdb_feature_builder_init(&builder, &cpu->dyn_scr_feature,
                             "org.gnu.gdb.riscv.scr", "riscv-scr.xml",
                             base_reg);

    for (i = 0; i < ARRAY_SIZE(scrs); i++) {
        gdb_feature_builder_append_reg(&builder, scrs[i].name,
                                       bitsize, i,
                                       scrs[i].code ? "code_capability" : "data_capability",
                                       "system");
    }

    gdb_feature_builder_end(&builder);
    return &cpu->dyn_scr_feature;
}
#endif

#if defined(TARGET_CHERI_RISCV_STD)
static GDBFeature *riscv_gen_dynamic_ycsr_feature(CPUState *cs, int base_reg)
{
    RISCVCPU *cpu = RISCV_CPU(cs);
    CPURISCVState *env = &cpu->env;
    GDBFeatureBuilder builder;
    int bitsize = riscv_cpu_mxl(env) == MXL_RV32 ? 64 : 128;

    gdb_feature_builder_init(&builder, &cpu->dyn_ycsr_feature,
                             "org.gnu.gdb.riscv.ycsr", "riscv-ycsr.xml",
                             base_reg);

    for (int i = 0; i < CSR_TABLE_SIZE; i++) {
        riscv_csr_cap_ops *cap_ops = get_csr_cap_info(i);
        if (!cap_ops) {
            continue;
        }
        riscv_csr_predicate_fn predicate = csr_ops[i].predicate;
        if (predicate && (predicate(env, i) == RISCV_EXCP_NONE)) {
            const char *name = csr_ops[i].name;
            if (!name) {
                name = g_strdup_printf("csr%03x", i);
            }
            gdb_feature_builder_append_reg(&builder, name, bitsize, i,
                                           "int", "system");

            gdb_feature_builder_append_reg(&builder, cap_ops->name, bitsize, i,
                                           (cap_ops->flags & CSR_OP_IS_CODE_PTR) != 0 ?
                                           "code_capability" : "data_capability",
                                           "system");
        }
    }

    gdb_feature_builder_end(&builder);
    return &cpu->dyn_ycsr_feature;
}

static int riscv_gdb_get_ycsr(CPUState *cs, GByteArray *buf, int n)
{
    RISCVCPU *cpu = RISCV_CPU(cs);
    CPURISCVState *env = &cpu->env;
    if (n < CSR_TABLE_SIZE) {
        const cap_register_t *value = get_cap_csr(env, n);
        return gdb_get_capreg(buf, value);
    }
    return 0;
}
#endif

void riscv_cpu_register_gdb_regs_for_features(CPUState *cs)
{
    RISCVCPUClass *mcc = RISCV_CPU_GET_CLASS(cs);
    RISCVCPU *cpu = RISCV_CPU(cs);
    CPURISCVState *env = &cpu->env;
    if (env->misa_ext & RVD) {
        gdb_register_coprocessor(cs, riscv_gdb_get_fpu, riscv_gdb_set_fpu,
                                 gdb_find_static_feature("riscv-64bit-fpu.xml"));
    } else if (env->misa_ext & RVF) {
        gdb_register_coprocessor(cs, riscv_gdb_get_fpu, riscv_gdb_set_fpu,
                                 gdb_find_static_feature("riscv-32bit-fpu.xml"));
    }
    if (cpu->cfg.ext_zve32x) {
        gdb_register_coprocessor(cs, riscv_gdb_get_vector,
                                 riscv_gdb_set_vector,
                                 ricsv_gen_dynamic_vector_feature(cs, cs->gdb_num_regs));
    }
    switch (mcc->def->misa_mxl_max) {
    case MXL_RV32:
        gdb_register_coprocessor(cs, riscv_gdb_get_virtual,
                                 riscv_gdb_set_virtual,
                                 gdb_find_static_feature("riscv-32bit-virtual.xml"));
        break;
    case MXL_RV64:
    case MXL_RV128:
        gdb_register_coprocessor(cs, riscv_gdb_get_virtual,
                                 riscv_gdb_set_virtual,
                                 gdb_find_static_feature("riscv-64bit-virtual.xml"));
        break;
    default:
        g_assert_not_reached();
    }
#if defined(TARGET_CHERI)
    gdb_register_coprocessor(cs, riscv_gdb_get_cheri_reg,
                             riscv_gdb_cheri_reg_no_write,
#if defined(TARGET_RISCV32)
                             gdb_find_static_feature("riscv-32bit-cheri.xml"));
#elif defined(TARGET_RISCV64)
                             gdb_find_static_feature("riscv-64bit-cheri.xml"));
#else
#error INVALID TARGET
#endif
#endif

    if (cpu->cfg.ext_zicsr) {
        gdb_register_coprocessor(cs, riscv_gdb_get_csr, riscv_gdb_set_csr,
                                 riscv_gen_dynamic_csr_feature(cs, cs->gdb_num_regs));
#if defined(TARGET_CHERI_RISCV_V9)
        gdb_register_coprocessor(
            cs, riscv_gdb_get_scr, riscv_gdb_cheri_reg_no_write,
            riscv_gen_dynamic_scr_feature(cs, cs->gdb_num_regs));
#elif defined(TARGET_CHERI_RISCV_STD)
        gdb_register_coprocessor(
            cs, riscv_gdb_get_ycsr, riscv_gdb_cheri_reg_no_write,
            riscv_gen_dynamic_ycsr_feature(cs, cs->gdb_num_regs));
#endif
    }
}

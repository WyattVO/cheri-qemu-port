/*
 * RISC-V translation routines for the MIPS extensions (xmips*).
 *
 * Copyright (c) 2025 MIPS
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Reference: MIPS P8700 instructions
 *            (https://mips.com/products/hardware/p8700/)
 */

#define REQUIRE_XMIPSCBOP(ctx) do {              \
    if (!ctx->cfg_ptr->ext_xmipscbop) {          \
        return false;                            \
    }                                            \
} while (0)

#define REQUIRE_XMIPSCMOV(ctx) do {              \
    if (!ctx->cfg_ptr->ext_xmipscmov) {          \
        return false;                            \
    }                                            \
} while (0)

/*
 * CHERI: the pair loads/stores go through DDC in integer mode, but there is
 * no capability-mode definition for them, so they are not available on
 * CHERI CPUs.
 */
#define REQUIRE_XMIPSLSP(ctx) do {               \
    if (!ctx->cfg_ptr->ext_xmipslsp ||           \
        ctx->cfg_ptr->ext_cheri) {               \
        return false;                            \
    }                                            \
} while (0)

/* Conditional move by MIPS. */
static bool trans_ccmov(DisasContext *ctx, arg_ccmov *a)
{
    REQUIRE_XMIPSCMOV(ctx);

    TCGv zero, source1, source2, source3, dest;
    zero = tcg_constant_tl(0);
    source1 = get_gpr(ctx, a->rs1, EXT_NONE);
    source2 = get_gpr(ctx, a->rs2, EXT_NONE);
    source3 = get_gpr(ctx, a->rs3, EXT_NONE);
    dest = dest_gpr(ctx, a->rd);

    /* CHERI: write through gen_set_gpr, cpu_gpr is not available */
    tcg_gen_movcond_tl(TCG_COND_NE, dest, source2, zero, source1, source3);
    gen_set_gpr(ctx, a->rd, dest);

    return true;
}

/* Load Doubleword Pair. */
static bool trans_ldp(DisasContext *ctx, arg_ldp *a)
{
    REQUIRE_XMIPSLSP(ctx);
    REQUIRE_64_OR_128BIT(ctx);

    TCGv src = get_gpr(ctx, a->rs1, EXT_NONE);
    TCGv dest0 = dest_gpr(ctx, a->rd);
    TCGv dest1 = dest_gpr(ctx, a->rs3);
    TCGv addr = tcg_temp_new();

    tcg_gen_addi_tl(addr, src, a->imm_y);
    gen_ddc_interposed_ld_tl(ctx, dest0, NULL, addr, ctx->mem_idx, MO_TESQ);
    gen_set_gpr(ctx, a->rd, dest0);

    tcg_gen_addi_tl(addr, addr, 8);
    gen_ddc_interposed_ld_tl(ctx, dest1, NULL, addr, ctx->mem_idx, MO_TESQ);
    gen_set_gpr(ctx, a->rs3, dest1);

    return true;
}

/* Load Word Pair. */
static bool trans_lwp(DisasContext *ctx, arg_lwp *a)
{
    REQUIRE_XMIPSLSP(ctx);

    TCGv src = get_gpr(ctx, a->rs1, EXT_NONE);
    TCGv dest0 = dest_gpr(ctx, a->rd);
    TCGv dest1 = dest_gpr(ctx, a->rs3);
    TCGv addr = tcg_temp_new();

    tcg_gen_addi_tl(addr, src, a->imm_x);
    gen_ddc_interposed_ld_tl(ctx, dest0, NULL, addr, ctx->mem_idx, MO_TESL);
    gen_set_gpr(ctx, a->rd, dest0);

    tcg_gen_addi_tl(addr, addr, 4);
    gen_ddc_interposed_ld_tl(ctx, dest1, NULL, addr, ctx->mem_idx, MO_TESL);
    gen_set_gpr(ctx, a->rs3, dest1);

    return true;
}

/* Store Doubleword Pair. */
static bool trans_sdp(DisasContext *ctx, arg_sdp *a)
{
    REQUIRE_XMIPSLSP(ctx);
    REQUIRE_64_OR_128BIT(ctx);

    TCGv src = get_gpr(ctx, a->rs1, EXT_NONE);
    TCGv data0 = get_gpr(ctx, a->rs2, EXT_NONE);
    TCGv data1 = get_gpr(ctx, a->rs3, EXT_NONE);
    TCGv addr = tcg_temp_new();

    tcg_gen_addi_tl(addr, src, a->imm_w);
    gen_ddc_interposed_st_tl(ctx, data0, NULL, addr, ctx->mem_idx, MO_TEUQ);

    tcg_gen_addi_tl(addr, addr, 8);
    gen_ddc_interposed_st_tl(ctx, data1, NULL, addr, ctx->mem_idx, MO_TEUQ);

    return true;
}

/* Store Word Pair. */
static bool trans_swp(DisasContext *ctx, arg_swp *a)
{
    REQUIRE_XMIPSLSP(ctx);

    TCGv src = get_gpr(ctx, a->rs1, EXT_NONE);
    TCGv data0 = get_gpr(ctx, a->rs2, EXT_NONE);
    TCGv data1 = get_gpr(ctx, a->rs3, EXT_NONE);
    TCGv addr = tcg_temp_new();

    tcg_gen_addi_tl(addr, src, a->imm_v);
    gen_ddc_interposed_st_tl(ctx, data0, NULL, addr, ctx->mem_idx, MO_TESL);

    tcg_gen_addi_tl(addr, addr, 4);
    gen_ddc_interposed_st_tl(ctx, data1, NULL, addr, ctx->mem_idx, MO_TESL);

    return true;
}

/* Move data from memory into cache. */
static bool trans_pref(DisasContext *ctx, arg_pref *a)
{
    REQUIRE_XMIPSCBOP(ctx);

    /* Nop */
    return true;
}

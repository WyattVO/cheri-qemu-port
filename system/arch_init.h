#ifndef QEMU_ARCH_INIT_H
#define QEMU_ARCH_INIT_H

#include "qapi/qapi-types-machine.h"

enum {
    QEMU_ARCH_ALPHA =       (1ULL << SYS_EMU_TARGET_ALPHA),
    QEMU_ARCH_ARM =         (1ULL << SYS_EMU_TARGET_ARM) |
                            (1ULL << SYS_EMU_TARGET_AARCH64) |
                            (1ULL << SYS_EMU_TARGET_MORELLO),
    QEMU_ARCH_I386 =        (1ULL << SYS_EMU_TARGET_I386) |
                            (1ULL << SYS_EMU_TARGET_X86_64),
    QEMU_ARCH_M68K =        (1ULL << SYS_EMU_TARGET_M68K),
    QEMU_ARCH_MICROBLAZE =  (1ULL << SYS_EMU_TARGET_MICROBLAZE),
    QEMU_ARCH_MIPS =        (1ULL << SYS_EMU_TARGET_MIPS) |
                            (1ULL << SYS_EMU_TARGET_MIPSEL) |
                            (1ULL << SYS_EMU_TARGET_MIPS64) |
                            (1ULL << SYS_EMU_TARGET_MIPS64EL) |
                            (1ULL << SYS_EMU_TARGET_MIPS64CHERI128),
    QEMU_ARCH_PPC =         (1ULL << SYS_EMU_TARGET_PPC) |
                            (1ULL << SYS_EMU_TARGET_PPC64),
    QEMU_ARCH_S390X =       (1ULL << SYS_EMU_TARGET_S390X),
    QEMU_ARCH_SH4 =         (1ULL << SYS_EMU_TARGET_SH4) |
                            (1ULL << SYS_EMU_TARGET_SH4EB),
    QEMU_ARCH_SPARC =       (1ULL << SYS_EMU_TARGET_SPARC) |
                            (1ULL << SYS_EMU_TARGET_SPARC64),
    QEMU_ARCH_XTENSA =      (1ULL << SYS_EMU_TARGET_XTENSA) |
                            (1ULL << SYS_EMU_TARGET_XTENSAEB),
    QEMU_ARCH_OR1K =        (1ULL << SYS_EMU_TARGET_OR1K),
    QEMU_ARCH_TRICORE =     (1ULL << SYS_EMU_TARGET_TRICORE),
    QEMU_ARCH_HPPA =        (1ULL << SYS_EMU_TARGET_HPPA),
    QEMU_ARCH_RISCV =       (1ULL << SYS_EMU_TARGET_RISCV32) |
                            (1ULL << SYS_EMU_TARGET_RISCV64) |
                            (1ULL << SYS_EMU_TARGET_RISCV32XCHERI) |
                            (1ULL << SYS_EMU_TARGET_RISCV64XCHERI) |
                            (1ULL << SYS_EMU_TARGET_RISCV32CHERISTD) |
                            (1ULL << SYS_EMU_TARGET_RISCV64CHERISTD),
    QEMU_ARCH_RX =          (1ULL << SYS_EMU_TARGET_RX),
    QEMU_ARCH_AVR =         (1ULL << SYS_EMU_TARGET_AVR),
    QEMU_ARCH_HEXAGON =     (1ULL << SYS_EMU_TARGET_HEXAGON),
    QEMU_ARCH_LOONGARCH =   (1ULL << SYS_EMU_TARGET_LOONGARCH64),
    QEMU_ARCH_ALL =         UINT64_MAX,
};

/* CHERI adds six targets, so upstream's 32-bit mask is not wide enough. */
QEMU_BUILD_BUG_ON(SYS_EMU_TARGET__MAX > 64);

/**
 * qemu_arch_available:
 * @arch_bitmask: bitmask of QEMU_ARCH_* constants
 *
 * Return whether the current target architecture is contained in @arch_bitmask
 */
bool qemu_arch_available(uint64_t arch_bitmask);

#endif

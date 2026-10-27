# CHERI QEMU 11.0 (WyattVO/cheri-qemu-port, branch `cheri-11.0`)

QEMU 11.0 with CHERI support for RISC-V and Arm Morello. This branch is the
CHERI QEMU 10.2 port (branch `cheri-10.2`) merged with upstream QEMU v11.0.0.

> **Not yet tagged.** The 11.0 port passes every stage of the TestRIG gate
> and every Morello test, but it has no baseline tag yet.
> `cheri-10.2-baseline-20261023` is still the frozen reference.
> [Status of the 11.0 port](#status-of-the-110-port) has the results.

- **CHERI-RISC-V** is the main target. It is checked instruction by
  instruction with [TestRIG](https://github.com/WyattVO/TestRIG-cheri-qemu/tree/testrig-cheri-qemu-10.2),
  against the Sail CHERI-RISC-V reference model and CHERI-Alliance QEMU.
  At system level it is checked by booting CheriBSD.
- **Morello** builds and runs, but TestRIG does not test it (there is no
  RVFI-DII or Sail model for Morello in TestRIG). It is tested only with the
  bare-metal [morello-generated-tests](https://github.com/rems-project/morello-generated-tests),
  which were generated from Arm's specification.

The upstream QEMU documentation is in [README.rst](README.rst), and the
original CHERI-QEMU notes are in [README.CHERI.md](README.CHERI.md).

**AI use.** The changes in this fork were made with AI assistance (Claude).
They are verified with the TestRIG gate rather than taken on trust. The 10.2
port at tag [`cheri-10.2-baseline-20261023`](https://github.com/WyattVO/cheri-qemu-port/tree/cheri-10.2-baseline-20261023)
passes every stage of the gate and every Morello test, and each CHERI-RISC-V
bug fix has a regression trace or a bare-metal test that fails without it.
The 11.0 port passes the same gate (2026-10-27).

## Where this comes from

| Layer | Source |
|---|---|
| QEMU 11.0.0 | upstream QEMU, tag `v11.0.0` |
| CHERI support (CHERI-RISC-V v9, CHERI-MIPS, Morello) | [CTSRD-CHERI/qemu](https://github.com/CTSRD-CHERI/qemu), QEMU 7.x based |
| Port of that CHERI support to QEMU 10.2 | Kenny Root, [kruton/cheri-qemu](https://github.com/kruton/cheri-qemu) branch `cheri-10.2.0-stacked-commits` (up to `35798f7560`) |
| TestRIG bring-up, bug fixes and CHERI extension policy on 10.2 | Wyatt Voigt, branch `cheri-10.2` (commits listed below) |
| Port from QEMU 10.2 to 11.0 | Wyatt Voigt, this branch: merge commit `fae69bc7a8` (see below) |

Branches in this fork:

- `cheri-11.0`: this branch, the QEMU 11.0 port.
- `cheri-10.2`: the QEMU 10.2 port, which passes the full gate. Its fixes are
  merged into `cheri-11.0` as they land.
- `main`: the original QEMU 7.1-based CHERI-QEMU, used as the comparison baseline.
- `rvfi-7.1-fix`: `main` plus a fix that lets 7.1 run under TestRIG at all
  (its RVFI-DII single-step handler was overridden).

## Status of the 11.0 port

The port was done as one merge of upstream `v11.0.0` into the 10.2 port,
which had 1321 CHERI commits on top of `v10.2.0`. That brought in 2676
upstream commits and 47 conflicting files.

| Check | Result on 11.0 |
|---|---|
| Builds `riscv64xcheri` and `riscv64cheristd` | yes |
| Builds `morello` | yes |
| Builds `riscv32xcheri` and `riscv32cheristd` | yes, but nothing tests them (as on 10.2) |
| Gate stage 1, CHERI extension policy | pass: 135 extensions reviewed, 38 refused, including the 7 that are new in 11.0 (below) |
| Gate stage 2, saved regression traces vs Sail | pass (7 warnings, all CHERI-Alliance QEMU differing, as on 10.2) |
| Gate stage 3, bare-metal tests | pass. `pte-cd-update-pmp` is still an expected failure, because its upstream fix (`fa2cf74883`) is not in 11.0.0. |
| Gate stage 4, random tests and coverage | pass: 39 templates, 100 tests each, no difference from Sail |
| Gate stage 4b, three-way vote | pass: in all 286 disagreements Sail agrees with this port and CHERI-Alliance QEMU differs |
| Gate stage 5, CheriBSD | pass: `cheribsdtest-purecap` 305 pass / 10 expected failures, `-hybrid` 149 / 52, as on 10.2 |
| Gate stage 6, upstream comparison | pass, against upstream v11.0.0 |
| morello-generated-tests | 3393 pass, 2 skipped, as on 10.2 |

These are from the gate run of 2026-10-27 (`full-11.0.conf`, 100 tests per
template, seed 20261027), which ended with `TESTRIG GATE: PASS (38
warnings)`.

### What the merge had to keep

- **Per-target builds.** QEMU 11 compiles more of `target/arm` and
  `system/physmem.c` once for all targets. CHERI needs them compiled per
  target: `TARGET_CHERI` changes `CPUARMState` for Morello, and `physmem.c`
  clears capability tags on RAM writes. They stay per target.
- **CHERI edits in code that QEMU 11 moved.** These were re-applied in the
  new places: `target/arm/tcg/debug.c`, `target/riscv/monitor.c`,
  `gdbstub/gdb-xml/` and `target/arm/tcg/helper-*-defs.h`.
- **RISC-V CSR state.** QEMU 11 stores CSRs as `uint64_t`. Integer fields
  follow that, the CHERI `cap_register_t` fields are unchanged, and
  migration uses the new `VMSTATE_UINT64_OR_CAP`.
- **Target masks.** CHERI adds six system targets, which takes QEMU 11 past
  its limit of 32. The `QEMU_ARCH_*` masks are now 64-bit, and the CHERI
  targets count as RISC-V, Arm or MIPS, so architecture-specific options
  apply to them as they did in 10.2.

### New QEMU 11 instructions that bypassed CHERI

Four extensions new in QEMU 11 load or store with raw addresses, which skips
the DDC bounds and permission checks. The port handles them like this:

| Extension | In this port |
|---|---|
| `zalasr` (load-acquire/store-release) | Integer-mode accesses go through DDC like other loads and stores. Reserved in capability mode, because CHERI v9 has no capability form of these instructions. |
| `zilsd`, `zclsd` (RV32 load/store pair) | Refused on CHERI CPUs, and their instructions are illegal. They are decoded after CHERI v9's RV32 `lc`/`sc`, which use the same encodings. |
| `xmipslsp` (MIPS vendor load/store pair) | Refused on CHERI CPUs, and its instructions are illegal. |

The other new extensions, `xmipscmov`, `xmipscbop` and `smpmpmt`, don't
access memory and are allowed. The gate's extension review (stage 1) covers
all 7.

## What the 10.2 port does

Everything in this section was measured on `cheri-10.2`. The 11.0 port gives
the same results (see the status table above).

- Builds three CHERI system emulators. Two are CHERI-RISC-V:
  - `qemu-system-riscv64xcheri`: CHERI ISA v9 (Xcheri), which is what CheriBSD,
    the Sail model and TestRIG use.
  - `qemu-system-riscv64cheristd`: the draft RISC-V CHERI standard 0.9.3. It
    builds and starts, but nothing tests it yet, because TestRIG has no
    generator for 0.9.3.

  The third is Morello:
  - `qemu-system-morello`: Arm Morello. It passes 3393 of the
    morello-generated-tests; the harness skips the other 2. That is the
    same as the 7.1 baseline. It has no TestRIG or CheriBSD testing.
- Boots CheriBSD (riscv64-purecap). `cheribsdtest-purecap` passes 305 tests with
  10 expected failures, and `cheribsdtest-hybrid` passes 149 with 52, which is
  the same as the 7.1 baseline.
- Supports TestRIG (RVFI-DII) mode when built with `--enable-rvfi-dii`.
- Matches the Sail CHERI-RISC-V v9 model on all 39 QuickCheckVEngine templates
  of the gate (100 random tests each) and on 31 saved regression traces. It
  also passes 32 bare-metal tests for features Sail does not model.
- Passes upstream QEMU v10.2.0's own RISC-V check-tcg tests and qtests, and
  gives the same riscv-arch-test 3.9.1 signatures as upstream. The only
  exceptions are expected: 2 Zacas tests that hit an upstream bug, and
  `bios-tables-test`, which needs a profile CHERI v9 can't offer.
- Refuses CPU extensions that would bypass capability checks (see below).

### Extensions disabled on CHERI CPUs

QEMU 10.2 brings about 100 RISC-V extensions that 7.1 did not have. The ones
below touch memory, addresses, control flow or privileged state without going
through CHERI's checks. Asking for one explicitly (e.g. `-cpu rv64,v=true`) is
an error. A CPU model that enables one implicitly (e.g. `-cpu max`) gets it
turned off with a warning. The list is in `cheri_unsafe_exts[]` in
`target/riscv/tcg/tcg-cpu.c`.

| Extensions | Reason |
|---|---|
| `v`, `zve*`, `zvfh*`, `zvfbf*`, vector crypto (`zvbb`, `zvbc`, `zvk*`) | Vector loads/stores use raw addresses: no bounds or permission checks, no tag clearing |
| `smmpm`, `smnpm`, `ssnpm`, `sspm`, `supm` | Pointer masking rewrites addresses after the capability bounds check |
| `h` | The hypervisor extension is not defined for CHERI (and `hlvx` aborted QEMU) |
| `svrsw60t59b` | PTE bits 59-60 are CHERI permission bits |
| `zcmt` | Jump-table reads are not checked against PCC/DDC |
| `smrnmi` | `mnret` changes privilege without restoring PCC |
| `zicfiss` | Shadow-stack accesses are not checked against DDC |
| `zicfilp` | Capability jumps don't set the landing-pad state |

Also enforced: `mstatus.UXL` is fixed to RV64; 64-bit CHERI builds refuse RV32 CPUs;
and on v9 the `rva22s64`/`rva23s64` profiles are unavailable, because they need
Svpbmt/Svnapot, whose PTE bits are CHERI bits.

## Changes made on 10.2 (Wyatt Voigt)

These commits were made on `cheri-10.2` and are part of this branch through
the merge. Every CHERI-RISC-V fix below has a TestRIG regression trace or a bare-metal
test in the TestRIG fork (`regressions/`), so it can't come back unnoticed.
The upstream qtests in the gate's stage 6 cover the other RISC-V fixes, and
the morello-generated-tests cover the Morello fixes.

**TestRIG (RVFI-DII) mode on QEMU 10.2**

- `a8ad3a05de` Make RVFI-DII mode start and work on QEMU 10.2.
- `039b7598f9` Restore load/store tracing lost in the upstream `tcg-op.c` split.
- `0082dff1cc` Trace capability register writes.
- `9812279785` Report `mtval` for helper-raised illegal instructions.
- `62112cc6b1` Report only 16 bits in `mtval` for illegal compressed instructions.
- `b0f1499dfc` Don't leave a timer interrupt pending after each test reset.
- `c8ba402f56` Translate instruction fetches, so instruction page faults can happen.
- `f6e5606fd7` Report physical, not virtual, memory addresses.

**Capability security**

- `50b913c41e`, `0b5d4a1a2d`, `ff5eed3968`: LR/SC, the 128-bit CAS and Zacas/Zabha
  `amocas` did not clear the tag of an overwritten capability, which allowed
  capability forgery.
- `c8f7accead` DDC/PCC bounds checks missed accesses that wrap past the top of
  the address space.
- `2d5d3f492b` The TLB truncated the CHERI page flags to 8 bits, so CheriBSD's
  load-side revocation never trapped and revoked capabilities survived.
  Found by `cheribsdtest`.

**CHERI extension policy** (see above)

- `616c5ed75d`, `4bbcd8e756`, `62e716177a`, `383f8b4fc7`: disable the unsafe
  extensions, also on bare (`rv64i`) and profile CPUs.
- `ca972d2d15` Keep `mstatus.UXL` read-only.
- `0cb83b7c63`, `ead56e9d8e`, `a5508ecb3a`: RV32 CPUs, `codasip-a730` and the
  S-mode profiles failed at startup.

**Matching the Sail CHERI-RISC-V v9 model**

- `56f1ff379e`, `6f93f2606b`: `cspecialrw` on a reserved SCR aborted QEMU, and
  it returned only the address outside capability mode.
- `64b7c54d98`, `b940469789`: `cbuildcap` unseals the requested value; an
  untagged rederivation of a sentry asserted.
- `c3b235e8b4` Implement `cclear`.
- `11520c2da4` Implement `cgettop`, which was in Sail but never in QEMU.
- `b12c68f64a`, `a61fdf2020`: `ccseal` check order, and `ccopytype` with
  cd = cs1 kept its tag.
- `7293576ada`, `c4d80608ea`, `13aa5a5625`: CSR instructions on capability CSRs
  follow v9; correct CHERI cause without ASR; `mccsr`/`uccsr` writes no longer
  fault.
- `01b83dce17` A byte store-conditional (`sc.b`) to `0xffffffffffffffff` without
  a reservation took a load access fault instead of failing (also in CHERI-QEMU
  7.1).
- Store-conditionals (`sc.b/h/w/d` and the explicit `sc.*.ddc`/`sc.*.cap`)
  checked their authority for Load instead of Store permission, so an SC
  through a Load-only DDC or capability wrote memory (also in CHERI-QEMU 7.1).
- `cbo.inval` only rejected a cache block entirely outside its authority's
  bounds; like `cbo.zero` it now needs the whole block in bounds (riscv-cheri
  spec), since invalidation can re-expose older data and capabilities.
- Capability stores ignored Sdtrig data triggers (watchpoints): the TLB
  slow flags were looked up with the CHERI access type instead of the
  data-store one (a regression from the 10.x TLB rework).
- `mseccfg.PMM` was writable without Smmpm when Smepmp is enabled (upstream
  QEMU bug; pointer masking itself stayed off).
- `2bf17aa9cd` Implement `fpclear`, which was missing.
- `ea51e31ba3` `amocas` with rd = x0 wrote to x0 (an upstream QEMU bug).

**Other RISC-V fixes**

- `cfdb9a165f` `cheri_tag_init` aborted under non-TCG accelerators, such as
  qtest.
- `385c7df638` The hobgoblin boards could not start on 10.2.

**Morello** (tested with the morello-generated-tests, not TestRIG)

- `bdb1dd967a` `get_phys_addr_pmsav7_default` gave the wrong protection type
  for `CPUTLBEntryFull` (broken by the CHERI TLB flag fix `2d5d3f492b`).
- `da1a391963` Store-exclusive used the raw address instead of the checked,
  DDC-relocated one.

### Known differences from the Sail model

These are intentional. TestRIG excludes them, and each is documented in the
TestRIG fork's `regressions/README.md`:

- `mscratch`/`sscratch` are the address field of `mscratchc`/`sscratchc` here,
  as in all CHERI-QEMU versions. Sail keeps them separate.
- QEMU implements the thread-ID capability registers `utidc`/`stidc`/`mtidc`.
  Sail does not.
- In TestRIG mode the machine timer never fires. Sail's timer advances once
  every 100 instructions.

## Building

Tested on Ubuntu 26.04 (GCC 15, Python 3.14). Other Linux distributions need
the same packages under their own names.

```sh
sudo apt install build-essential git ninja-build pkg-config python3 python3-venv \
                 libglib2.0-dev libpixman-1-dev flex bison
git clone -b cheri-11.0 https://github.com/WyattVO/cheri-qemu-port.git
cd cheri-qemu-port && mkdir build && cd build
../configure --target-list=riscv64xcheri-softmmu,riscv64cheristd-softmmu \
    --enable-rvfi-dii --disable-werror --disable-docs
ninja
```

- `--enable-rvfi-dii` is needed only for TestRIG. Leave it out for a plain emulator.
- For `-netdev user` networking, install `libslirp-dev` before configuring.
- The binaries are `build/qemu-system-riscv64xcheri` and
  `build/qemu-system-riscv64cheristd`.
- Alternatively, cheribuild (`./cheribuild.py qemu`) builds QEMU together with
  the rest of the CHERI SDK.

For Morello, use a separate build directory:

```sh
mkdir build-morello && cd build-morello
../configure --target-list=morello-softmmu --disable-werror --disable-docs
ninja
```

## Running

### Boot CheriBSD

Build a CheriBSD disk image with [cheribuild](https://github.com/CTSRD-CHERI/cheribuild):

```sh
./cheribuild.py -d disk-image-riscv64-purecap bbl-baremetal-riscv64-purecap
```

The output goes to `~/cheri/output` by default. Then:

```sh
OUT=~/cheri/output
build/qemu-system-riscv64xcheri -M virt -m 2048 -nographic \
  -bios   $OUT/sdk/bbl/riscv64-purecap/bbl \
  -kernel $OUT/rootfs-riscv64-purecap/boot/kernel/kernel \
  -drive  if=none,file=$OUT/cheribsd-riscv64-purecap.img,id=drv,format=raw,snapshot=on \
  -device virtio-blk-device,drive=drv -device virtio-rng-pci
```

- Log in as `root` (no password).
- Run `cheribsdtest-purecap -a` (expect `passed 305, expectedly failed 10`) and
  `cheribsdtest-hybrid -a` (expect `passed 149, expectedly failed 52`).
- Quit with `shutdown -p now`, or press Ctrl-A then X.
- `snapshot=on` keeps the disk image unchanged.

If cheribuild's CheriBSD build fails on a host with glibc 2.43 or newer
(`incompatible-pointer-types-discards-qualifiers` in host tools), add
`CWARNFLAGS.clang+=-Wno-error=incompatible-pointer-types-discards-qualifiers`
to `tools/build/mk/Makefile.boot` in the CheriBSD source.

### Test it with TestRIG

The TestRIG fork runs everything with one command: the CHERI extension policy
checks, the saved regression traces, the bare-metal tests, every random
template and, optionally, CheriBSD. See
[WyattVO/TestRIG-cheri-qemu](https://github.com/WyattVO/TestRIG-cheri-qemu/tree/testrig-cheri-qemu-10.2)
for setup, then:

```sh
CHERIBSD_OUTPUT=~/cheri/output utils/scripts/testrig-gate.sh /path/to/build/qemu-system-riscv64xcheri 100
```

It must end with `TESTRIG GATE: PASS`. Run it after every change to this
branch. On 11.0 use `--config utils/gate-profiles/full-11.0.conf`, which
compares stage 6 against an upstream v11.0.0 build (`refs/build-refs.sh
upstream11`) instead of v10.2.0. The fork's `docker/` directory builds a
container with the 10.2 port and every reference the gate uses. There is no
11.0 image yet.

### Test Morello

TestRIG doesn't cover Morello. Instead, run the bare-metal
[morello-generated-tests](https://github.com/rems-project/morello-generated-tests)
with the pytest harness in `tests/morello`. It needs `pytest-xdist`.

```sh
git clone https://github.com/rems-project/morello-generated-tests.git
cd tests/morello
python3 -m pytest --numprocesses=$(nproc) \
    --morello-tests-dir=/path/to/morello-generated-tests \
    --qemu=/path/to/build-morello/qemu-system-morello
```

Expect `3393 passed, 2 skipped`. Run it after changes to shared code
(`accel/tcg`, `target/cheri-common`, `include/`) as well as `target/arm`:
a RISC-V fix once broke the Morello build.

## Not done yet

- SMP with MTTCG: CHERI tags are not thread-safe across host threads (QEMU
  warns when `-accel tcg,thread=multi` is given; a forged capability is
  observable and `cbo.zero` aborts QEMU). The default for CHERI, round-robin
  TCG, is safe; use it for `-smp`.

- Tagging an 11.0 baseline.
- Making Zilsd/Zclsd and the XMIPS load/store pair CHERI-aware (they are
  refused on CHERI CPUs for now), and merging the QEMU 11.0.x stable
  releases or 11.1, which contains the PMP fix `fa2cf74883`.
- Instruction-level testing of Morello (TestRIG has no Morello model), and
  booting CheriBSD for Morello.
- Making vector CHERI-aware, which is needed before it can be enabled.
- Testing `riscv64cheristd`: it needs CheriBSD for `riscv64zcheri093-purecap`
  and a Sail model of the 0.9.3 standard
  (CHERI-Alliance/sail-riscv, branch `cheri-0.9.3`).

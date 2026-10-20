# CHERI-RISC-V QEMU 10.2 (WyattVO/cheri-qemu-port, branch `cheri-10.2`)

QEMU 10.2 with CHERI-RISC-V support, checked instruction by instruction against
the Sail CHERI-RISC-V reference model with [TestRIG](https://github.com/WyattVO/TestRIG-cheri-qemu/tree/testrig-cheri-qemu-10.2)
and at system level by booting CheriBSD.
The port is RISC-V only. The upstream QEMU documentation is in
[README.rst](README.rst), and the original CHERI-QEMU notes are in
[README.CHERI.md](README.CHERI.md).

## Where this comes from

| Layer | Source |
|---|---|
| QEMU 10.2.0 | upstream QEMU |
| CHERI support (CHERI-RISC-V v9, CHERI-MIPS, Morello) | [CTSRD-CHERI/qemu](https://github.com/CTSRD-CHERI/qemu), QEMU 7.x based |
| Port of that CHERI support to QEMU 10.2 | Kenny Root, [kruton/cheri-qemu](https://github.com/kruton/cheri-qemu) branch `cheri-10.2.0-stacked-commits` (up to `35798f7560`) |
| TestRIG bring-up, bug fixes and CHERI extension policy | Wyatt Voigt, this branch (commits on top, listed below) |

Branches in this fork:

- `cheri-10.2`: this branch, the QEMU 10.2 port.
- `main`: the original QEMU 7.1-based CHERI-QEMU, used as the comparison baseline.
- `rvfi-7.1-fix`: `main` plus a fix that lets 7.1 run under TestRIG at all
  (its RVFI-DII single-step handler was overridden).

## What it does now

- Builds two CHERI-RISC-V system emulators:
  - `qemu-system-riscv64xcheri`: CHERI ISA v9 (Xcheri), which is what CheriBSD,
    the Sail model and TestRIG use.
  - `qemu-system-riscv64cheristd`: the draft RISC-V CHERI standard 0.9.3. It
    builds and starts, but nothing tests it yet, because TestRIG has no
    generator for 0.9.3.
- Boots CheriBSD (riscv64-purecap). `cheribsdtest-purecap` passes 305 tests with
  10 expected failures, and `cheribsdtest-hybrid` passes 149 with 52, which is
  the same as the 7.1 baseline.
- Supports TestRIG (RVFI-DII) mode when built with `--enable-rvfi-dii`.
- Matches the Sail CHERI-RISC-V v9 model on all 36 comparable QuickCheckVEngine
  templates (300 random tests each), on 28 saved regression traces and on 6
  bare-metal tests for features Sail does not model.
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

## Changes in this branch (Wyatt Voigt)

Every bug fix below has a TestRIG regression trace or a bare-metal test in the
TestRIG fork (`regressions/`), so it can't come back unnoticed.

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
git clone -b cheri-10.2 https://github.com/WyattVO/cheri-qemu-port.git
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
CHERIBSD_OUTPUT=~/cheri/output utils/scripts/testrig-gate.sh /path/to/build/qemu-system-riscv64xcheri 300
```

It must end with `TESTRIG GATE: PASS`. Run it after every change to this
branch.

## Not done yet

- SMP with MTTCG: CHERI tags are not thread-safe across host threads (QEMU
  warns when `-accel tcg,thread=multi` is given; a forged capability is
  observable and `cbo.zero` aborts QEMU). The default for CHERI, round-robin
  TCG, is safe; use it for `-smp`.

- Rebasing onto QEMU 11.
- Making vector CHERI-aware, which is needed before it can be enabled.
- Testing `riscv64cheristd`: it needs CheriBSD for `riscv64zcheri093-purecap`
  and a Sail model of the 0.9.3 standard
  (CHERI-Alliance/sail-riscv, branch `cheri-0.9.3`).

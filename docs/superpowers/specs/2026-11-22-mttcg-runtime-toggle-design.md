# MTTCG as a runtime toggle on the main port branches

Date: 2026-11-22
Repos: `WyattVO/cheri-qemu-port`, `WyattVO/TestRIG-cheri-qemu`

## Intent and why

The lock_tag series — the work that makes CHERI capability tags safe under
multi-threaded TCG — currently lives on its own branches: `cheri-10.2-mttcg`
and `cheri-11.0-mttcg` in the QEMU fork, and
`testrig-cheri-qemu-10.2-mttcg` in TestRIG. Every fix to the port has to be
landed twice, and every gate change has to be landed twice; the Docker image
builds the port *twice* for the same reason. Two concrete symptoms:

- The gate's stale-trace fix (`3e017b4`) only exists on the MTTCG branch, even
  though the bug it fixes hits any profile without `ALLIANCE_QEMU`.
- The Docker image pins `TESTRIG_REV=1030579`, the 10th of the TestRIG series'
  14 commits, so the image ships *without* that fix and without the QCVEngine
  bump above it.

Fold the MTTCG work into the main branches so there is one branch per QEMU
version, and make MTTCG something you switch on at run time instead of
something you switch branches for. Then delete the MTTCG branches.

The port already has the machinery for this. `need_concurrent_tags()` is a
single global latched once in `cheri_tag_init()` from
`tcg_enabled() && qemu_tcg_mttcg_enabled() && max_cpus > 1`. When it is false,
tag memory is a plain bitmap and no lock code runs on any path (the fast path
short-circuits through `UNSAFE_SINGLE_CORE` in `cheri_tagmem.h:81`, and
generated code keys its locking off `CF_PARALLEL` in `tcg/tcg-op-ldst.c:51`);
when it is true, tag memory is an array of `lock_tag` bytes. One binary
already serves both modes. What is missing is a deliberate choice about the
default, and the test and documentation work that follows from it.

## Goals

- One branch per QEMU version: `cheri-10.2` and `cheri-11.0`, each able to run
  with or without MTTCG.
- MTTCG reached by an explicit `-accel tcg,thread=multi`, and by nothing else.
- Every result already recorded against `cheri-10.2` and `cheri-11.0` stays
  valid: a gate run with no accel flag must execute exactly as it does today.
- The gate's MTTCG profiles genuinely run under MTTCG, *provably from the log*,
  at every stage that is supposed to.
- The Docker image builds the port once, from current pins.
- `cheri-10.2-mttcg`, `cheri-11.0-mttcg` and `testrig-cheri-qemu-10.2-mttcg`
  are gone, with the pre-merge state still recoverable.

## Non-goals

- Changing the lock_tag implementation. The 14/15 commits move across as they
  are; their design is not reopened here.
- MTTCG on Morello / CHERI-ARM. `207e1445e9` (part of the merge) reports
  `mttcg_supported = false` for CHERI-ARM and makes `cheri_tag_init()` refuse
  to start a multi-CPU machine rather than warn. That stays as it is.
- MIPS. `593c807249` touches `mips64cheri128-softmmu.mak`, but that file's
  `TARGET_SUPPORTS_MTTCG` line is inert (see Approaches), and
  `target/mips/cpu.c:729` sets `.mttcg_supported = TARGET_LONG_BITS == 32`, so
  `mips64cheri128` is already false and already hits the refusal path. No MIPS
  work, no MIPS testing.
- Extending the DMA tag locking to round-robin mode (see Accepted limitations).
- Performance work on the lock path, and any new MTTCG test coverage beyond
  what the branches already carry.

## Anti-goals

Things that would make this a failure even if MTTCG technically toggles:

- A gate run that *looks* like it tested MTTCG but did not. The specific trap
  is `cheribsd-tests.py:95`, which passes `-smp N` and no accel flag; with
  MTTCG defaulting off, `CHERIBSD_SMP=4` would boot four harts under
  round-robin TCG and report a pass. That is worse than no MTTCG coverage,
  because it reads as coverage in the log. §3 is written to make the log prove
  the mode rather than assert it.
- A silent behaviour change to `full.conf`. If merging flipped the default,
  every baseline in `reports/baseline-10.2` would quietly describe a different
  execution mode than the one a fresh run produces.
- Two binaries again, by any route — a build-time switch, a second configure,
  a second Docker build. That is the thing being removed.
- Documentation left describing the two-branch world. There is more of it than
  there looks (§5), including a `regressions/README.md` section that states the
  port does not enable MTTCG for CHERI targets.

## Constraints

- Both QEMU MTTCG branches are **strict fast-forwards** over their bases:
  `cheri-10.2..cheri-10.2-mttcg` is 14 commits, `cheri-11.0..cheri-11.0-mttcg`
  is 15, and both reverse ranges are empty. Same for
  `testrig-cheri-qemu-10.2..testrig-cheri-qemu-10.2-mttcg` (14 commits). So
  nothing needs cherry-picking — the gate fix and the QCVEngine bump ride
  along — and the merges can be `--ff-only`. Local and origin agree on all six
  refs.
- `accel/tcg/tcg-all.c` is in `tcg_specific_ss` in `accel/tcg/meson.build:18`,
  so it is compiled per target and `TARGET_CHERI` is visible there
  (`tcg/tcg-op-ldst.c:44` in the same source set already relies on this).
- The TestRIG gitlink must stay consistent: QCVEngine is pushed before TestRIG.
  `847f30d` is already on `WyattVO/QuickCheckVEngine` branch
  `cheri-qemu-gate`, so there is nothing to push there.
- Pushes go to the WyattVO forks only.
- The Docker image pins three commit hashes (`PORT_REV`, `MTTCG_REV`,
  `TESTRIG_REV`); two have to move and one has to go, in the same change.

## Approaches considered

### Where the default lives

**Reinstate `TARGET_SUPPORTS_MTTCG=n`** in the CHERI target configs — i.e.
revert `593c807249`. Rejected because **it would do nothing**.
`TARGET_SUPPORTS_MTTCG` is read in exactly one place in the tree,
`meson.build:3416-3418`, which only defaults it to `y`; it is emitted into
`<target>-config-target.h` and poisoned for common code, and no C file reads
it. (The only other mention is a stale comment at
`target/cheri-common/cheri_tagmem.h:76`. `configs/targets/morello-softmmu.mak:11`
still carries the line on the MTTCG branches and it has no effect.) What
actually set the flag false on the base branches is a `#ifdef TARGET_CHERI`
block inside `riscv_tcg_ops` at `target/riscv/tcg/tcg-cpu.c:286-292`, removed
by `f62ba5b78d` on 10.2 and `cc4c6def3d` on 11.0.

**A build-time switch** (`--enable-cheri-mttcg` or a Kconfig symbol) that
compiles `lock_tag` out. Rejected: it reintroduces two binaries to build, pin
and test — the exact split this change exists to remove — and the gate would
have to know which binary it was handed. A runtime flag would still be needed
on top of it.

**A bare `#ifdef TARGET_CHERI` in `tcg_init_machine()`'s `ON_OFF_AUTO_AUTO`
branch.** Viable, about three lines, no header change. Rejected on one
criterion, stated here because the two candidates are otherwise close: **no
CHERI identifiers in generic accelerator code.** Not "fewest hunks" — the
chosen option also touches two generic files, so on a raw conflict count the
two are a wash. The point is *what* the conflict looks like when it comes:
re-resolving a generic boolean read is mechanical, re-resolving a CHERI
`#ifdef` someone else has since rewritten is not.

**A new `TCGCPUOps` field — chosen.** Add a field beside the existing
`mttcg_supported` in `include/accel/tcg/cpu-ops.h:30` meaning "supported, but
not the default", set it for CHERI-RISC-V, and consult it only in the `AUTO`
branch of `tcg_init_machine()`. It states the actual property as a target
property, keeps `accel/tcg` free of CHERI `#ifdef`s, and leaves the Morello
refusal path untouched. It also has direct precedent: the base branches
already carried a `#ifdef TARGET_CHERI` in `riscv_tcg_ops`, so the CHERI
condition goes back exactly where one already lived, and the only generic-code
change is one boolean read.

### Which default

**MTTCG on by default**, as the branches have it — a pure fast-forward with no
new code, matching upstream convention. Rejected: it re-characterises every
recorded baseline, CheriBSD boot and `full.conf` run, and it would make
`full-mttcg.conf` largely redundant while requiring `full.conf` to start
passing `thread=single` to stay comparable with what it has already measured.

**MTTCG off by default, opt in — chosen.** It costs one small patch on top of
the merge, and in exchange every existing result stays valid with no flag, and
the QEMU default lines up with how the gate already treats MTTCG: a profile you
ask for (`BENCH_MTTCG=require`, `CHERIBSD_SMP=4`), not the ambient mode.

### How the branches move

**Fast-forward merge — chosen**, preserving all 14/15 commits and their
authorship (9 of the series are Paul Buxton's), with the override as a new
commit on top. Squashing would lose that authorship and the bisectable history
of a series that found real bugs; both matter for the thesis write-up.

## Accepted limitations

**The default binary does not take the device-write tag locks.** The DMA tag
locking added by `adb99fe38d` in `system/physmem.c:3189-3192` is conditional on
`need_concurrent_tags()`, so with MTTCG off by default it is inactive in the
shipped default mode, and `smp-dma-tag-race.S` (`QEMU_THREADS: multi` only) is
skipped by `full.conf`. This is **accepted, not fixed here** — it is the
pre-existing round-robin behaviour that `cheri_tagmem.h:65-81` documents and
that `UNSAFE_SINGLE_CORE` deliberately accepts, and `adb99fe38d`'s own commit
message says round-robin TCG is unchanged. Nothing regresses relative to
today's `cheri-10.2`.

The consequence is a reporting one: the "DMA forgery fixed" result is
**MTTCG-mode-qualified**, and must be written that way wherever it is claimed —
the QEMU README, `TESTBENCH.md`, and any thesis text. The documentation work in
§5 states explicitly that round-robin mode keeps the known device-vs-vCPU tag
race and that `smp-dma-tag-race.S` is an MTTCG-only test.

## The design

### 1. QEMU: the opt-in field

In `include/accel/tcg/cpu-ops.h`, immediately after `mttcg_supported` at
`:30`, add `mttcg_opt_in` — "MTTCG works on this target but is not the
default" — with a kernel-doc comment in the house style. Default false, so no
existing target changes behaviour.

In `accel/tcg/tcg-all.c:127`, the `ON_OFF_AUTO_AUTO` arm currently reads:

    if (mttcg_supported && !icount_enabled()) {

Gate it additionally on the new field, so `AUTO` resolves to
`ON_OFF_AUTO_OFF` for a target that opts in rather than defaults in. The
`ON_OFF_AUTO_ON` arm is untouched: an explicit `thread=multi` still turns MTTCG
on, sets `max_threads = ms->smp.max_cpus`, and emits no warning — because
`mttcg_supported` remains true. That last point is load-bearing: the bench's
MTTCG detection at `qemu-only-tests.sh:62` keys off the absence of that
warning.

Set the field in `riscv_tcg_ops` at `target/riscv/tcg/tcg-cpu.c:287`, under
`#ifdef TARGET_CHERI`, with a comment saying why: the lock_tag path is correct
but is not the mode the port's recorded results describe, so it is asked for
explicitly. This is one struct, one hunk, identical on both branches, and it
covers all four CHERI-RISC-V targets (`riscv32/64xcheri`,
`riscv32/64cheristd`). Non-CHERI RISC-V is unaffected.

ARM is not touched (it would need two structs, `target/arm/cpu.c:2475` and
`target/arm/tcg/cpu-v7m.c:244`, and CHERI-ARM is already
`mttcg_supported = false` once the merge lands, with `cheri_tag_init()`
refusing). MIPS is not touched (already false, see Non-goals).

`cheri_tag_init()` stays coherent: `tcg_init_machine()` resolves `AUTO` before
any RAM is created, so `qemu_tcg_mttcg_enabled()` is false by then, `concurrent`
at `cheri_tagmem.c:484` is false, and the `exit(1)` at `:496` is not reached.

One commit per branch, on top of the merge.

### 2. QEMU: documentation

No tracked file in the QEMU fork mentions the MTTCG branch names on any of the
four branches, and nothing on the MTTCG branches touched `README.md` or
`docs/`, so this is entirely new text.

`README.md` gains a `### MTTCG` subsection under `## Running` — last
subsection before `## Not done yet` — covering: that CHERI-RISC-V supports
MTTCG; that it is off unless `-accel tcg,thread=multi` is given; what changes
when it is on (tag memory becomes `lock_tag` bytes, stores that may meet a
capability take the tag lock, device writes take it too); that it needs
`maxcpus > 1` to engage at all; that round-robin mode keeps the known
device-vs-vCPU tag race; and that Morello refuses to start under it. Plus one
line under `## Changes in this branch` recording that the lock_tag series is
now part of the branch.

### 3. TestRIG: make the MTTCG profiles provably run MTTCG

The bench and all six multi-hart regression tests already pass
`-accel tcg,thread=$mode` explicitly from `qemu-only-tests.sh:126`, driven by
their `# QEMU_THREADS:` headers. Every test with a `QEMU_SMP` header also has a
`QEMU_THREADS` header, so none can run multi-hart without an explicit flag. No
change.

`cheribsd-tests.py` is the only gap: it builds argv with `-smp N` and no accel
flag. Three pieces:

1. **A `CHERIBSD_MTTCG` gate setting** mirroring `BENCH_MTTCG`, taking
   `off|on|require` with the same validation shape as
   `qemu-only-tests.sh:31-34`. Default `off`. Plumbed from `testrig-gate.sh`
   into a new `cheribsd-tests.py` option and added to the settings line the
   gate already prints at `testrig-gate.sh:298`. `full-mttcg.conf` sets it
   alongside `CHERIBSD_SMP=4`; `full-11.0-mttcg.conf` inherits it by sourcing
   `full-mttcg.conf`. (My call: an explicit setting rather than deriving it
   from `CHERIBSD_SMP > 1`, because four harts under round-robin is a
   legitimate thing to want to test, and the printed settings line should say
   which mode ran.)
2. **Argv in the log.** `cheribsd-tests.py` echoes its full QEMU argv into the
   console log, and the gate's cheribsd note at `testrig-gate.sh:600-601`
   reports the thread mode next to the hart count it already prints.
3. **A positive runtime check.** After boot, `cheribsd-tests.py` reads
   `/proc/<pid>/task` and confirms QEMU really has a vCPU thread per hart
   (named `CPU <n>/TCG` under MTTCG, versus a single `ALL CPUs/TCG` under
   round-robin). This requires passing `-name debug-threads=on`, without
   which QEMU does not write thread names into `/proc` at all,
   failing the stage if not. This is the part that satisfies the anti-goal:
   it is independent runtime evidence rather than a self-report, and these
   logs are thesis evidence.

The random, vote, coverage and upstream stages run single-hart with RVFI-DII;
`upstream-diff.py:114` already pins `thread=single` deliberately. No change.
One behaviour change worth recording: `upstream-tests.sh:77` runs upstream's
own qtests with `QTEST_QEMU_BINARY` set to the port binary, so whatever `-smp`
those qtests use currently gets MTTCG by default on the MTTCG branch and gets
round-robin afterwards. That is the safe direction and not a coverage gap, but
stage 6 does execute differently and the §6 comparison should expect it.

`full.conf` and `full-11.0.conf`: no change. They have always effectively run
round-robin, and after this they still do, without a flag.

### 4. TestRIG: Docker

`docker/Dockerfile`:
- drop the `MTTCG_REV` arg (`:72`) and the whole second `fetch-rev` +
  configure + ninja block (`:70-78`);
- advance `PORT_REV` (`:61`) to the merged `cheri-10.2` commit. Note this also
  pulls in `dd44855316` (the Morello/AI README commit), since the current pin
  `da1a391963` is the baseline tag, one commit behind `cheri-10.2`'s head;
- **advance `TESTRIG_REV` (`:41`)** to the merged `testrig-cheri-qemu-10.2`
  head. (My call. Leaving it would ship an image without `3e017b4` and
  `ec17e68`, which contradicts the Intent. `docker/fetch-rev.sh:8` fetches a
  bare sha, which GitHub serves only for a sha reachable from a ref, so the
  pin must point at a commit on a surviving branch — the push order in §7
  guarantees that, and this makes the dependency explicit rather than
  accidental.)
- fix the two-build description in the file header (`:1-3`).

`docker/capstone-gate` keeps `--mttcg`, but it now only sets
`profile=full-mttcg`; the `qemu=` override at `:22` goes away, since the same
binary serves both. Update the usage comment at `:4-7`.

`docker/README.md`: the MTTCG-branch row (`:9`), the TestRIG row's revision
hash and its `…/tree/testrig-cheri-qemu-10.2-mttcg` link plus the QuickCheckVEngine
hash — now `847f30d8b5` per the merged gitlink — (`:10`), the two-build
provenance sentence (`:19-20`), `MTTCG_REV` in the `--build-arg` list (`:36`),
the "test the MTTCG branch instead" paragraph reworded to "run the same QEMU
under MTTCG" (`:53-57`), and the
`cheri-qemu-AI/cheri-qemu-10.2-mttcg/build` path in the layout list (`:117`).

### 5. TestRIG: the rest of the documentation

`regressions/README.md:156-179`, "### SMP and MTTCG", currently states as fact
that CHERI-QEMU 10.2 runs harts round-robin and does not enable MTTCG for CHERI
targets, that `-accel tcg,thread=multi` warns, and that the lock_tag series
lives on branch `cheri-10.2-mttcg` (`:166`, `:175-176`, and the bench header
docs at `:151`). All of that is false on the merged branch. My call on how to
fix it: **rewrite the section to the one-branch world in the present tense,
and keep the explanation of why MTTCG is unsafe without lock_tag as an
explicit past-tense paragraph** ("before the lock_tag series…"). That
explanation exists nowhere else and is cross-referenced from
`TESTBENCH.md:156-157`, so deleting it would lose real information, but
leaving it in the present tense would be a lie about the current branch.

`TESTBENCH.md`: record that MTTCG is a run-time mode of the one branch, which
stages exercise it and how (bench via `QEMU_THREADS`, CheriBSD via
`CHERIBSD_MTTCG`), that `full.conf` deliberately does not, and that the DMA
result is MTTCG-qualified per Accepted limitations. Touches `:54`, `:59-60`,
`:81`, `:140-146`.

Stale prose to correct in the same commits:
- `utils/scripts/testrig-gate.sh:113-114` — `CHERIBSD_SMP`'s help says QEMU
  runs the harts with MTTCG when it supports MTTCG for CHERI.
- `utils/scripts/cheribsd-tests.py:77-79` — `--smp`'s help says the same.
- `utils/gate-profiles/full-mttcg.conf:3-4` — "four harts, which QEMU runs on
  four host threads", true again only once `CHERIBSD_MTTCG` is set there.

### 6. Validation before anything is deleted

Nothing is deleted until the gate passes on the merged branches. Logs to
`reports/mttcg-10.2/`:

- `full.conf` against merged `cheri-10.2` — must match the
  `reports/baseline-10.2` evidence. This is the proof the default did not move.
- `full-mttcg.conf` against the *same* binary — must match
  `gate-10.2-mttcg-final.log` / `gate-10.2-mttcg-testrig.log`, with the
  settings line showing `CHERIBSD_MTTCG`, the cheribsd note showing the mode,
  and the host-thread check passing.
- `full-11.0.conf` and `full-11.0-mttcg.conf` against merged `cheri-11.0` —
  compared against `gate-11.0-mttcg.log`.
- A deliberate negative check: `full-mttcg.conf` with `CHERIBSD_MTTCG=off`
  must *fail* the host-thread check rather than pass the stage. Without this
  the new evidence path is itself untested.
- `tests/unit` lock_tag test and the host-only locktag benchmark still build
  and pass on each merged branch.
- A Docker image build from the updated Dockerfile with both pins advanced,
  then `capstone-gate` and `capstone-gate --mttcg` inside it, confirming one
  binary serves both. Per the existing note, the image cannot be built until
  the merged branches are pushed to the forks.

The `full.conf` / `full-11.0.conf` runs also cover the Alliance branch of the
gate's stale-trace fix, which `3e017b4` changed and no vote stage has exercised
since. Expect stage 6 to differ from the MTTCG-branch logs for the qtest reason
in §3.

### 7. Retiring the branches

Only after the runs above pass. Tag each MTTCG branch head before deleting it,
so the pre-merge state stays recoverable by name: `cheri-10.2-mttcg-final` at
`adb99fe38d`, `cheri-11.0-mttcg-final` at `6dda88956f`,
`testrig-cheri-qemu-10.2-mttcg-final` at `ec17e68`. Push the tags, then delete
the three branches locally and on the WyattVO forks, and remove the
now-redundant `cheri-qemu-11.0-mttcg` worktree.

Record the three tag names in `reports/mttcg-10.2/`, because the gate-log
filenames under `reports/gate-runs/` (e.g.
`20261029-121250-cheri-10.2-mttcg-adb99fe38d-full-mttcg.log`) are the only
record of which branch produced that evidence, and the branch name in them
stops resolving once the branches are gone.

Push order, for the gitlink: QCVEngine (already at `847f30d`, nothing to do),
then TestRIG, then the QEMU branches. Both QEMU and TestRIG pushes have to land
before the Docker pins can be validated.

## Left to the builder

- The exact wording of the `mttcg_opt_in` kernel-doc comment.
- Commit splitting and messages, within this shape: QEMU — (1) the field +
  `tcg-all.c` + riscv, (2) README; TestRIG — (1) `CHERIBSD_MTTCG` plumb +
  evidence + profiles, (2) Docker re-pin, (3) docs.
- How the host-thread check reads the hart count (QEMU's own pid vs. a
  `/proc` scan) and what tolerance it allows for QEMU's non-vCPU threads.
- Exact placement of the new text within each documentation file.

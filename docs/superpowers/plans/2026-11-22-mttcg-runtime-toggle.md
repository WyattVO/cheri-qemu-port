# MTTCG Runtime Toggle Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fold the lock_tag MTTCG series into `cheri-10.2` and `cheri-11.0` with MTTCG as an explicit runtime opt-in, prove the gate's MTTCG profiles really run multi-threaded, and retire the three MTTCG branches.

**Architecture:** Both QEMU MTTCG branches and the TestRIG one are strict fast-forwards over their bases, so the series moves by `git merge --ff-only`. A new `TCGCPUOps.mttcg_opt_in` field makes `-accel tcg,thread=auto` resolve to single-threaded on CHERI-RISC-V while leaving `thread=multi` fully working, so one binary serves both modes. On the gate side a `CHERIBSD_MTTCG` setting passes the flag to the CheriBSD stage and a new mode-detection helper reads QEMU's vCPU thread names out of `/proc` to prove from the log which mode actually ran.

**Tech Stack:** C (QEMU 10.2/11.0, meson/ninja), Bash (gate scripts), Python 3 stdlib (gate helpers, `unittest`), Docker.

**Spec:** `docs/superpowers/specs/2026-11-22-mttcg-runtime-toggle-design.md`

## Global Constraints

- Merges are `git merge --ff-only`. Never squash — the series' authorship (9 of 14/15 commits are Paul Buxton's) and bisectability must survive.
- Pushes go to the **WyattVO forks only**. Push order for the gitlink: QCVEngine (already at `847f30d`, nothing to do), then TestRIG, then the QEMU branches.
- Nothing is pushed until Task 7's local validation passes. Nothing is deleted until Task 9 passes.
- Never use bare `git stash` / `git stash pop` in these worktrees — the stash stack is shared. Use a WIP commit instead.
- No commit may carry a `Co-Authored-By: Claude` or `Generated with Claude Code` line.
- `mttcg_supported` stays `true` for CHERI-RISC-V. The bench's MTTCG detection at `utils/scripts/qemu-only-tests.sh:62` keys off the *absence* of the "not yet converted to MTTCG" warning, so making it false would break stage 3.
- Tag names and targets, exactly: `cheri-10.2-mttcg-final` → `adb99fe38d`, `cheri-11.0-mttcg-final` → `6dda88956f`, `testrig-cheri-qemu-10.2-mttcg-final` → `ec17e68`.
- The DMA tag-locking limitation is **accepted, not fixed**: every place that claims the DMA result must qualify it as MTTCG-mode-only.

## Review Focus

Failure modes the spec implies that no task's happy path exercises. Each has a test added to the owning task.

1. **`/proc/<pid>/task` unreadable, or QEMU gone before the mode check runs** — must report `unknown` and fail `require` with a distinct message, never silently report `single` and never fail a passing boot under `off`. (Task 1, Task 4)
2. **`CHERIBSD_MTTCG=require` against a QEMU with no CHERI MTTCG** (an Alliance binary, or a pre-merge one that refuses and exits) — must fail with a clear reason, not hang to the 3600s login timeout. (Task 4)
3. **`CHERIBSD_MTTCG=on|require` with `CHERIBSD_SMP=1`** — `thread=multi` gives exactly one `CPU 0/TCG` thread and `need_concurrent_tags()` is still false because `max_cpus == 1`, so the count check passes while tags are *not* concurrent. Must warn that the run proves nothing about lock_tag. (Task 4)
4. **An invalid `CHERIBSD_MTTCG` value** — must exit 2 with a message naming the accepted values, never default silently to off. (Task 4)
5. **A QEMU that names vCPU threads differently** (a different version, or `comm` truncation) — the name match must be anchored so a lookalike cannot be read as a vCPU thread, and the result must be `unknown` rather than a wrong mode. (Task 1)

---

### Task 1: vCPU thread-mode helper

The measuring instrument for everything after this. Pure function over a directory, so it is testable without QEMU.

**Files:**
- Create: `utils/scripts/qemu_mode.py` (TestRIG repo)
- Create: `utils/scripts/test_qemu_mode.py` (TestRIG repo)

**Interfaces:**
- Consumes: nothing.
- Produces: `vcpu_thread_mode(task_dir: str) -> tuple[str, int]`, returning `("multi", n)` when `n` threads are named `CPU <i>/TCG` (MTTCG creates one per hart, `accel/tcg/tcg-accel-ops-mttcg.c:131`), `("single", 1)` when a thread is named `ALL CPUs/TCG` (round-robin, `accel/tcg/tcg-accel-ops-rr.c:331`), and `("unknown", 0)` when neither is present or `task_dir` cannot be read. Also `probe_argv(argv) -> list[str]`, which appends `-name debug-threads=on` (QEMU writes thread names into `/proc` only when asked; without it every thread is named after the binary and the mode is unreadable) and `-S`. And a `__main__` CLI: `python3 utils/scripts/qemu_mode.py <qemu> [args...]` launches that QEMU through `probe_argv`, prints `<mode> <count>`, and kills it.

- [ ] **Step 1: Write the failing tests**

```python
# utils/scripts/test_qemu_mode.py
import os, tempfile, unittest
from qemu_mode import vcpu_thread_mode

def task_dir(names):
    d = tempfile.mkdtemp()
    for i, name in enumerate(names):
        os.mkdir(os.path.join(d, str(100 + i)))
        with open(os.path.join(d, str(100 + i), "comm"), "w") as f:
            f.write(name + "\n")
    return d

class TestVcpuThreadMode(unittest.TestCase):
    def test_mttcg_reports_multi_and_counts_harts(self):
        d = task_dir(["qemu-system-risc", "CPU 0/TCG", "CPU 1/TCG",
                      "CPU 2/TCG", "CPU 3/TCG", "IO mon_iothread"])
        self.assertEqual(vcpu_thread_mode(d), ("multi", 4))

    def test_round_robin_reports_single(self):
        d = task_dir(["qemu-system-risc", "ALL CPUs/TCG", "IO mon_iothread"])
        self.assertEqual(vcpu_thread_mode(d), ("single", 1))

    def test_no_vcpu_thread_reports_unknown(self):
        self.assertEqual(vcpu_thread_mode(task_dir(["qemu-system-risc"])),
                         ("unknown", 0))

    def test_missing_directory_reports_unknown(self):
        self.assertEqual(vcpu_thread_mode("/nonexistent/task"), ("unknown", 0))

    def test_lookalike_name_is_not_a_vcpu_thread(self):
        d = task_dir(["notCPU 0/TCG", "CPU 0/TCGx"])
        self.assertEqual(vcpu_thread_mode(d), ("unknown", 0))

if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `cd utils/scripts && python3 test_qemu_mode.py -v`
Expected: FAIL — `ModuleNotFoundError: No module named 'qemu_mode'`

- [ ] **Step 3: Implement `vcpu_thread_mode(task_dir: str) -> tuple[str, int]` in `utils/scripts/qemu_mode.py`**

Match thread names with a fully anchored pattern (`^CPU \d+/TCG$`, `^ALL CPUs/TCG$`) against each `comm` file's stripped contents. Treat any `OSError` reading the directory or a `comm` file as that thread contributing nothing, so a thread exiting mid-scan cannot turn `multi` into `unknown` on its own. Prefer `multi` when both kinds somehow appear.

- [ ] **Step 4: Run the tests to verify they pass**

Run: `cd utils/scripts && python3 test_qemu_mode.py -v`
Expected: 5 tests, OK

- [ ] **Step 5: Add the `__main__` CLI**

Launch `probe_argv(sys.argv[1:])` with `subprocess.Popen`, poll `vcpu_thread_mode("/proc/%d/task" % p.pid)` until it is not `unknown` or a 10s deadline passes, print `"%s %d" % (mode, count)`, then terminate the child. Exit 1 if the mode is `unknown`, printing QEMU's stderr so a launch failure is visible.

- [ ] **Step 6: Commit**

```bash
git add utils/scripts/qemu_mode.py utils/scripts/test_qemu_mode.py
git commit -m "utils: Detect whether a running QEMU uses MTTCG or round-robin"
```

---

### Task 2: QEMU 10.2 — fold the series in and make MTTCG opt-in

**Files:**
- Modify: `include/accel/tcg/cpu-ops.h:30` (add field after `mttcg_supported`)
- Modify: `accel/tcg/tcg-all.c:127` (the `ON_OFF_AUTO_AUTO` condition)
- Modify: `target/riscv/tcg/tcg-cpu.c:286-287` (set the field in `riscv_tcg_ops`)
- Worktree: `/home/wyatt/Projects/CAPSTONE/cheri-qemu-AI/cheri-qemu-10.2`

**Interfaces:**
- Consumes: `vcpu_thread_mode` CLI from Task 1.
- Produces: `bool TCGCPUOps.mttcg_opt_in` — "MTTCG is supported on this target but is not the default; it must be asked for with `-accel tcg,thread=multi`". Task 3 sets the identical field on `cheri-11.0`.

- [ ] **Step 1: Fast-forward the series onto `cheri-10.2`**

```bash
cd /home/wyatt/Projects/CAPSTONE/cheri-qemu-AI/cheri-qemu-10.2
git checkout cheri-10.2
git merge --ff-only cheri-10.2-mttcg
git log --oneline -1   # expect adb99fe38d
```

- [ ] **Step 2: Commit the spec and this plan onto `cheri-10.2`**

They are currently untracked in this worktree, so they survive the checkout in Step 1.

```bash
git add docs/superpowers/specs/2026-11-22-mttcg-runtime-toggle-design.md \
        docs/superpowers/plans/2026-11-22-mttcg-runtime-toggle.md
git commit -m "docs: Add the design and plan for making MTTCG a run-time toggle"
```

- [ ] **Step 3: Build and confirm the default is currently MTTCG (the behaviour being changed)**

Run: `python3 ../../TestRig-AI/TestRIG-cheri-qemu/utils/scripts/qemu_mode.py build-mttcg/qemu-system-riscv64xcheri -M virt -smp 4 -bios none -display none -monitor none -serial none`
Expected: `multi 4` — this is the pre-patch state, and it is why the patch exists. (Rebuild `build-mttcg` with ninja first. If the machine refuses to start with no payload, add `-kernel` pointing at any ELF.)

- [ ] **Step 4: Add `mttcg_opt_in` to `struct TCGCPUOps`**

Insert the field directly after `mttcg_supported` in `include/accel/tcg/cpu-ops.h`, with a kernel-doc comment in the same style as its neighbour. Default false, so no other target changes behaviour.

- [ ] **Step 5: Gate the AUTO branch on it in `accel/tcg/tcg-all.c`**

Change the condition at `:127` to also require `!cc->tcg_ops->mttcg_opt_in`. Leave the `ON_OFF_AUTO_ON` and `ON_OFF_AUTO_OFF` arms untouched.

- [ ] **Step 6: Set the field for CHERI-RISC-V in `target/riscv/tcg/tcg-cpu.c`**

In `riscv_tcg_ops` at `:286`, keep `.mttcg_supported = true` and add, under `#ifdef TARGET_CHERI`, `.mttcg_opt_in = true` with a comment saying why: the lock_tag path is correct, but round-robin is the mode the port's recorded results describe, so MTTCG is asked for explicitly. One hunk; covers all four CHERI-RISC-V targets. Touch neither `target/arm` nor `target/mips`.

- [ ] **Step 7: Rebuild and verify both modes**

```bash
ninja -C build-mttcg
M=../../TestRig-AI/TestRIG-cheri-qemu/utils/scripts/qemu_mode.py
Q=build-mttcg/qemu-system-riscv64xcheri
A="-M virt -smp 4 -bios none -display none -monitor none -serial none"
python3 $M $Q $A                              # expect: single 1
python3 $M $Q $A -accel tcg,thread=multi      # expect: multi 4
```

- [ ] **Step 8: Verify the lock_tag tests and a non-CHERI target are unaffected**

```bash
./build-mttcg/tests/unit/test-cheri-tagmem-locktag
./build-mttcg/tests/bench/cheri-tagmem-locktag-bench
```
Expected: both PASS. The primitive itself is untouched by this task, so a failure here means the merge went wrong, not the patch. Also confirm a configured non-CHERI RISC-V target (if one is built) still reports `multi` by default.

- [ ] **Step 9: Commit**

```bash
git add include/accel/tcg/cpu-ops.h accel/tcg/tcg-all.c target/riscv/tcg/tcg-cpu.c
git commit -m "accel/tcg, target/riscv: Make MTTCG opt-in on CHERI-RISC-V"
```

---

### Task 3: QEMU 11.0 — the same fold and patch

**Files:**
- Modify: the same three files, same lines, in `/home/wyatt/Projects/CAPSTONE/cheri-qemu-AI/cheri-qemu-11.0`

**Interfaces:**
- Consumes: the `mttcg_opt_in` field name and semantics from Task 2, unchanged.
- Produces: nothing new.

- [ ] **Step 1: Fast-forward the series onto `cheri-11.0`**

```bash
cd /home/wyatt/Projects/CAPSTONE/cheri-qemu-AI/cheri-qemu-11.0
git merge --ff-only cheri-11.0-mttcg
git log --oneline -1   # expect 6dda88956f
```

- [ ] **Step 2: Apply the identical three-file change**

Same field, same condition, same `riscv_tcg_ops` hunk as Task 2 steps 4-6. The file and line numbers are identical on this branch; confirm with `grep -n mttcg_supported target/riscv/tcg/tcg-cpu.c` before editing.

- [ ] **Step 3: Build and verify both modes**

Configure a build directory with `--target-list=riscv64xcheri-softmmu --enable-rvfi-dii`, then run the two `qemu_mode.py` commands from Task 2 step 7 against it.
Expected: `single 1` with no flag, `multi 4` with `-accel tcg,thread=multi`

- [ ] **Step 4: Verify the lock_tag tests**

```bash
./<build>/tests/unit/test-cheri-tagmem-locktag
./<build>/tests/bench/cheri-tagmem-locktag-bench
```
Expected: both PASS.

- [ ] **Step 5: Commit**

Same message as Task 2 step 9.

---

### Task 4: `CHERIBSD_MTTCG` and the evidence the log needs

**Files:**
- Modify: `utils/scripts/cheribsd-tests.py:77-79` (the `--smp` help), `:95-100` (argv), `:101-127` (the run)
- Modify: `utils/scripts/testrig-gate.sh:113-114` (help), `:298` (settings line), `:592-601` (the cheribsd stage)
- Modify: `utils/gate-profiles/full-mttcg.conf`
- Create: `utils/scripts/test_cheribsd_mttcg.py`

**Interfaces:**
- Consumes: `vcpu_thread_mode(task_dir)` from Task 1.
- Produces: `cheribsd-tests.py --mttcg {off,on,require}` (default `off`); a stdout line `MTTCG: <mode>, <n> vCPU threads` that the gate's `note[cheribsd]` appends; gate setting `CHERIBSD_MTTCG`.

- [ ] **Step 1: Write the failing tests**

```python
# utils/scripts/test_cheribsd_mttcg.py
import unittest
from cheribsd_mttcg import qemu_accel_args, check_mode

class TestAccelArgs(unittest.TestCase):
    def test_off_adds_nothing(self):
        self.assertEqual(qemu_accel_args("off"), [])
    def test_on_and_require_ask_for_multi(self):
        self.assertEqual(qemu_accel_args("on"), ["-accel", "tcg,thread=multi"])
        self.assertEqual(qemu_accel_args("require"), ["-accel", "tcg,thread=multi"])

class TestCheckMode(unittest.TestCase):
    # check_mode(setting, mode, count, smp) -> (ok: bool, message: str)
    def test_require_passes_when_mttcg_ran_on_every_hart(self):
        ok, msg = check_mode("require", "multi", 4, 4)
        self.assertTrue(ok); self.assertIn("multi", msg)
    def test_require_fails_on_round_robin(self):
        ok, msg = check_mode("require", "single", 1, 4)
        self.assertFalse(ok); self.assertIn("round-robin", msg)
    def test_require_fails_distinctly_when_mode_is_unknown(self):
        ok, msg = check_mode("require", "unknown", 0, 4)
        self.assertFalse(ok); self.assertIn("could not tell", msg)
    def test_require_fails_when_threads_do_not_cover_every_hart(self):
        ok, msg = check_mode("require", "multi", 2, 4)
        self.assertFalse(ok); self.assertIn("4", msg)
    def test_on_does_not_fail_on_round_robin(self):
        ok, _ = check_mode("on", "single", 1, 4)
        self.assertTrue(ok)
    def test_off_never_fails_and_never_checks(self):
        for mode, count in [("single", 1), ("unknown", 0), ("multi", 4)]:
            self.assertTrue(check_mode("off", mode, count, 4)[0])
    def test_single_hart_warns_that_tags_are_not_concurrent(self):
        ok, msg = check_mode("require", "multi", 1, 1)
        self.assertTrue(ok); self.assertIn("not concurrent", msg)

if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `cd utils/scripts && python3 test_cheribsd_mttcg.py -v`
Expected: FAIL — `ModuleNotFoundError: No module named 'cheribsd_mttcg'`

- [ ] **Step 3: Implement `qemu_accel_args(setting)` and `check_mode(setting, mode, count, smp)` in `utils/scripts/cheribsd_mttcg.py`**

`check_mode` returns `(ok, message)`. Under `off`, always `(True, "")`. Under `on`, never fail — report what ran. Under `require`, fail unless `mode == "multi"` and `count == smp`, with distinct wording for `single` ("round-robin"), `unknown` ("could not tell"), and a short count. Whenever `smp < 2`, append that tags are **not concurrent** at one hart, because `need_concurrent_tags()` also requires `max_cpus > 1` — the run proves MTTCG started, not that lock_tag was exercised.

- [ ] **Step 4: Run the tests to verify they pass**

Run: `cd utils/scripts && python3 test_cheribsd_mttcg.py -v`
Expected: 9 tests, OK

- [ ] **Step 5: Wire it into `cheribsd-tests.py`**

Add `--mttcg` with `choices=("off", "on", "require")`, default `off` — argparse's own `choices` gives the exit-2-on-bad-value behaviour Review Focus 4 wants. Extend `argv` with `qemu_accel_args(args.mttcg)`, **and always with `-name debug-threads=on`** — without it QEMU does not write thread names into `/proc` and the mode check can only ever answer `unknown` (see the Task 1 ruling in the ledger). It only enables naming; it has no effect on the guest. Write the full argv into the console log as the first line, before `Console(argv, log)`, so the log records what was launched. After the `login:` prompt is reached (QEMU is up, and this is also where a QEMU that refused unsafe MTTCG has already exited with its message in the log), call `vcpu_thread_mode("/proc/%d/task" % con.pid)`, print `MTTCG: <mode>, <n> vCPU threads` plus the message from `check_mode`, and set `status = 1` if it is not ok. Correct the `--smp` help at `:77-79`, which currently claims QEMU picks MTTCG automatically.

- [ ] **Step 6: Wire it into the gate**

In `utils/scripts/testrig-gate.sh`: pass `--mttcg "${CHERIBSD_MTTCG:-off}"` in the `cheribsd-tests.py` invocation; append the `MTTCG: ...` line to `note[cheribsd]` next to the hart count; add `CHERIBSD_MTTCG=${CHERIBSD_MTTCG:-off}` to the settings line at `:298`; document it in the environment block next to `BENCH_MTTCG` and fix `CHERIBSD_SMP`'s stale description at `:113-114`.

- [ ] **Step 7: Set it in the MTTCG profile**

Add `CHERIBSD_MTTCG=require` to `utils/gate-profiles/full-mttcg.conf` beside `CHERIBSD_SMP=4`, and correct the comment at `:3-4` so it describes the flag rather than an automatic default. `full-11.0-mttcg.conf` inherits it by sourcing. `full.conf` and `full-11.0.conf` are not touched.

- [ ] **Step 8: Verify the bad-value and no-MTTCG paths by hand**

```bash
python3 utils/scripts/cheribsd-tests.py --mttcg bogus X Y   # expect: exit 2, lists off/on/require
```
Then run the cheribsd stage with `CHERIBSD_MTTCG=require` against a QEMU without CHERI MTTCG (`refs/alliance-qemu/build/qemu-system-riscv64xcheri`) and confirm it fails with the refusal in the log rather than hanging to the login timeout.

- [ ] **Step 9: Commit**

```bash
git add utils/scripts/cheribsd_mttcg.py utils/scripts/test_cheribsd_mttcg.py \
        utils/scripts/cheribsd-tests.py utils/scripts/testrig-gate.sh \
        utils/gate-profiles/full-mttcg.conf
git commit -m "gate: Run CheriBSD under MTTCG on request, and prove which mode ran"
```

---

### Task 5: QEMU documentation

**Files:**
- Modify: `README.md` on `cheri-10.2` and `cheri-11.0` (`## Running` section, `## Changes in this branch` section)

**Interfaces:**
- Consumes: the flag name and semantics from Task 2.
- Produces: nothing code depends on.

- [ ] **Step 1: Add a `### MTTCG` subsection under `## Running`**

Last subsection before `## Not done yet`. Cover: CHERI-RISC-V supports MTTCG; it is off unless `-accel tcg,thread=multi` is given; what changes when on (tag memory becomes `lock_tag` bytes, stores that may meet a capability take the tag lock, device writes take it too); it needs `maxcpus > 1` to engage at all; **round-robin mode keeps the known device-vs-vCPU tag race** that `UNSAFE_SINGLE_CORE` accepts, so the DMA-forgery fix applies under MTTCG only; Morello refuses to start under it.

- [ ] **Step 2: Note the series under `## Changes in this branch`**

One line recording that the lock_tag series is now part of the branch, with the MTTCG result mode-qualified.

- [ ] **Step 3: Verify no stale branch references remain**

Run: `git grep -n "cheri-10.2-mttcg\|cheri-11.0-mttcg"`
Expected: no output on either branch.

- [ ] **Step 4: Commit on both branches**

```bash
git add README.md && git commit -m "README.md: Document MTTCG as an opt-in run-time mode"
```

---

### Task 6: TestRIG documentation

**Files:**
- Modify: `regressions/README.md:151`, `:156-179`
- Modify: `TESTBENCH.md:54`, `:59-60`, `:81`, `:140-146`

**Interfaces:**
- Consumes: `CHERIBSD_MTTCG` from Task 4.
- Produces: nothing code depends on.

- [ ] **Step 1: Rewrite `regressions/README.md`'s "SMP and MTTCG" section**

Present tense for the one-branch world: MTTCG is a run-time mode of the port, off by default, reached with `-accel tcg,thread=multi`. Keep the explanation of *why* MTTCG is unsafe without lock_tag as an explicit past-tense paragraph ("before the lock_tag series…"), because it exists nowhere else and `TESTBENCH.md` cross-references it. Remove the branch name at `:166` and the "(on that branch, N host threads)" at `:175-176`, and fix the bench header docs at `:151`.

- [ ] **Step 2: Update `TESTBENCH.md`**

Record that MTTCG is a run-time mode of the one branch; which stages exercise it and how (bench via `QEMU_THREADS`, CheriBSD via `CHERIBSD_MTTCG`, with the vCPU-thread check as the evidence); that `full.conf` deliberately does not; and that `smp-dma-tag-race.S` is MTTCG-only, so the DMA result is mode-qualified.

- [ ] **Step 3: Verify no stale branch references remain outside `docker/`**

Run: `git grep -n "testrig-cheri-qemu-10.2-mttcg\|cheri-10.2-mttcg" -- . ':!docker'`
Expected: no output. (`docker/` is Task 8.)

- [ ] **Step 4: Commit**

```bash
git add regressions/README.md TESTBENCH.md
git commit -m "docs: Describe MTTCG as a run-time mode of the one branch"
```

---

### Task 7: Local gate validation

No code. This is the gate that decides whether anything gets pushed or deleted.

**Files:**
- Create: logs under `/home/wyatt/Projects/CAPSTONE/reports/mttcg-10.2/`

**Interfaces:**
- Consumes: everything from Tasks 1-6.
- Produces: the evidence Task 8 and Task 10 are conditional on.

- [ ] **Step 1: `full.conf` against merged `cheri-10.2`**

Run the gate with `--config utils/gate-profiles/full.conf` against the merged 10.2 binary.
Expected: matches the `reports/baseline-10.2` evidence. **This is the proof the default did not move.** Settings line shows `CHERIBSD_MTTCG=off`.

- [ ] **Step 2: `full-mttcg.conf` against the same binary**

Expected: matches `gate-10.2-mttcg-final.log` / `gate-10.2-mttcg-testrig.log`; settings line shows `CHERIBSD_MTTCG=require CHERIBSD_SMP=4`; `note[cheribsd]` reports `multi, 4 vCPU threads`. One binary served both profiles — record that explicitly in the log note.

- [ ] **Step 3: The negative check**

Run `full-mttcg.conf` with `CHERIBSD_MTTCG=off` overridden on the command line.
Expected: the cheribsd stage **fails** the mode check rather than passing. Without this the new evidence path is itself untested.

- [ ] **Step 4: `full-11.0.conf` and `full-11.0-mttcg.conf` against merged `cheri-11.0`**

Expected: compared against `gate-11.0-mttcg.log`. Expect stage 6 to differ from the MTTCG-branch logs, because `upstream-tests.sh:77` runs upstream's qtests against the port binary and those now run round-robin instead of MTTCG — the safe direction, but a real difference. Record it rather than treating it as a regression.

- [ ] **Step 5: Save the logs and write a short summary**

Save all five runs under `reports/mttcg-10.2/` with names saying which branch and profile produced them. Summarise pass/fail per profile, and state which comparisons matched their reference.

- [ ] **Step 6: Stop here if anything failed**

Do not proceed to Task 8. Report what failed.

---

### Task 8: Push to the forks

**Files:** none.

**Interfaces:**
- Consumes: Task 7's passing validation.
- Produces: the pushed refs the Docker pins in Task 9 need.

- [ ] **Step 1: Confirm the gitlink before pushing**

```bash
cd /home/wyatt/Projects/CAPSTONE/TestRig-AI/TestRIG-cheri-qemu
git ls-tree HEAD riscv-implementations/QuickCheckVEngine   # expect 847f30d8b5...
git -C riscv-implementations/QuickCheckVEngine ls-remote origin cheri-qemu-gate
```
Expected: the gitlink SHA matches what `cheri-qemu-gate` points at on the fork, so a fresh clone resolves. QCVEngine `847f30d` is already pushed, so there is nothing to push there — verify, do not assume.

- [ ] **Step 2: Push TestRIG, then the QEMU branches**

```bash
git push origin testrig-cheri-qemu-10.2
cd /home/wyatt/Projects/CAPSTONE/cheri-qemu-AI/cheri-qemu-10.2 && git push origin cheri-10.2
cd /home/wyatt/Projects/CAPSTONE/cheri-qemu-AI/cheri-qemu-11.0 && git push origin cheri-11.0
```
TestRIG first, so no one can fetch a TestRIG commit whose gitlink does not resolve.

- [ ] **Step 3: Verify each pushed head**

Run `git ls-remote origin <branch>` for all three and confirm the SHAs match local.

---

### Task 9: Docker — one build, current pins

**Files:**
- Modify: `docker/Dockerfile:1-3` (header), `:41` (`TESTRIG_REV`), `:61` (`PORT_REV`), `:70-78` (delete the MTTCG block)
- Modify: `docker/capstone-gate:4-7` (usage), `:21-22` (the `--mttcg` case)
- Modify: `docker/README.md:9`, `:10`, `:19-20`, `:36`, `:53-57`, `:117`

**Interfaces:**
- Consumes: the pushed SHAs from Task 8.
- Produces: an image where `capstone-gate` and `capstone-gate --mttcg` run the same binary.

- [ ] **Step 1: Re-pin and drop the second build**

Advance `PORT_REV` to the merged `cheri-10.2` head (note this also picks up `dd44855316`, the Morello/AI README commit, since the current pin `da1a391963` is the baseline tag one commit behind). Advance `TESTRIG_REV` to the merged `testrig-cheri-qemu-10.2` head — leaving it at `1030579` would ship an image without `3e017b4` and `ec17e68`. Delete the `MTTCG_REV` arg and its whole `fetch-rev`/configure/ninja block. Fix the two-build description in the file header.

- [ ] **Step 2: Make `--mttcg` select only the profile**

In `docker/capstone-gate`, drop the `qemu=` override at `:22` so `--mttcg` sets `profile=full-mttcg` alone, and update the usage comment at `:4-7`.

- [ ] **Step 3: Update `docker/README.md`**

Remove the MTTCG-branch row (`:9`); in the TestRIG row (`:10`) update the revision hash, repoint the branch link to `testrig-cheri-qemu-10.2`, and set the QuickCheckVEngine hash to `847f30d8b5`; rewrite the two-build provenance sentence (`:19-20`); drop `MTTCG_REV` from the `--build-arg` list (`:36`); reword the MTTCG paragraph (`:53-57`) from "test the MTTCG branch instead" to "run the same QEMU under MTTCG"; drop the `cheri-qemu-10.2-mttcg/build` path from the layout list (`:117`).

- [ ] **Step 4: Build the image**

Run: `docker build -t cheri-qemu-gate:10.2 docker/`
Expected: succeeds, and builds the port once.

- [ ] **Step 5: Verify one binary serves both profiles**

Run `capstone-gate` and `capstone-gate --mttcg` in the container (CheriBSD will skip unless an output directory is mounted).
Expected: both run; the MTTCG run's settings line shows `CHERIBSD_MTTCG=require`; both report the same QEMU commit in the run header.

- [ ] **Step 6: Verify no stale branch references remain anywhere**

Run: `git grep -n "cheri-10.2-mttcg\|cheri-11.0-mttcg\|testrig-cheri-qemu-10.2-mttcg"`
Expected: no output.

- [ ] **Step 7: Commit and push**

```bash
git add docker/ && git commit -m "docker: Build the port once and run MTTCG from the same binary"
git push origin testrig-cheri-qemu-10.2
```

---

### Task 10: Tag and retire the MTTCG branches

Irreversible. Only after Tasks 7 and 9 pass.

**Files:**
- Create: a note in `/home/wyatt/Projects/CAPSTONE/reports/mttcg-10.2/` recording the tags

**Interfaces:**
- Consumes: Task 7's and Task 9's passing validation.
- Produces: nothing.

- [ ] **Step 1: Confirm each branch is fully merged**

```bash
cd /home/wyatt/Projects/CAPSTONE/cheri-qemu-AI/cheri-qemu-10.2
git log --oneline cheri-10.2..cheri-10.2-mttcg          # expect empty
git log --oneline cheri-11.0..cheri-11.0-mttcg          # expect empty
cd /home/wyatt/Projects/CAPSTONE/TestRig-AI/TestRIG-cheri-qemu
git log --oneline testrig-cheri-qemu-10.2..testrig-cheri-qemu-10.2-mttcg  # expect empty
```
Expected: all three empty. If any is not, stop.

- [ ] **Step 2: Create and push the three tags**

Annotated tags at exactly `adb99fe38d`, `6dda88956f`, `ec17e68`, named `cheri-10.2-mttcg-final`, `cheri-11.0-mttcg-final`, `testrig-cheri-qemu-10.2-mttcg-final`. Push them before deleting anything.

- [ ] **Step 3: Verify the tags are on the remotes**

Run `git ls-remote --tags origin` in both repos and confirm all three resolve to the expected SHAs.

- [ ] **Step 4: Record the tags in the evidence directory**

Write a short note in `reports/mttcg-10.2/` mapping each tag to its SHA and to the gate logs that were produced from it. The filenames under `reports/gate-runs/` embed the branch names (e.g. `20261029-121250-cheri-10.2-mttcg-adb99fe38d-full-mttcg.log`), and those names stop resolving once the branches are gone.

- [ ] **Step 5: Delete the branches, local and remote**

Delete `cheri-10.2-mttcg`, `cheri-11.0-mttcg` (QEMU fork) and `testrig-cheri-qemu-10.2-mttcg` (TestRIG), locally and on the WyattVO forks. Remove the now-redundant `cheri-qemu-11.0-mttcg` worktree with `git worktree remove`.

- [ ] **Step 6: Verify the end state**

```bash
git branch -a | grep mttcg    # expect: no branches, in both repos
git tag | grep mttcg          # expect: the three -final tags
```
Expected: one branch per QEMU version, three tags preserving the pre-merge heads.

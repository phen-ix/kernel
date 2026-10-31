# Phase 2 — feature integration record

Branch: `arena/2197b6cf-kernel` (original branch `16` untouched).

This file records what was changed, why, and what is still **not** proven.
A successful build is not a runtime guarantee.

## Pins / provenance

| Component | Upstream | Pin | Notes |
|---|---|---:|---|
| LLVM/Clang | AOSP prebuilts clang-r522817 (mirror) | `6a853dd45fe8f0d1821deb899c77a7b954b6306b` | Clang/LLD 18.0.1, Android 11967740 |
| Host tools | LineageOS android_prebuilts_build-tools | `f61cfbcb609173e1040753a2b9e8fbe8517343f9` | flex/bison/bc/coreutils shims |
| uclamp reference | darkhz/uclamp `uclamp_4.14` | `bc867bf6c38f72b661bcd0f7006e17c8bfcaa3c3` | adaptation reference only; known cpuset/active-task defects were not copied |
| uclamp ABI/group reference | AOSP kernel_msm redbull 4.19 | see audit | CPU-controller `cpu.uclamp.*` model |
| xxBackslashKSU | `backslashxx/KernelSU` | `f8aeac629022114d6b366b17048965a28ff398e8` | exact requested upstream, UAPI v6 |
| BBRv3 algorithm | `google/bbr` `bbrv3-2025-03-18` | `90210de4b779d40496dee0b89081780eeddf2a60` | BBRv3 model, not BBRv1 |
| BBRv3 mainline compatibility | `stardock/tcp_bbr_modules` | `45731d211025c30702316590f1f01d7aa59811b9` | compatibility/accessor layer only; reviewed against Google source |
| KCal | this repo history before revert `f36b29f714` | `a3c762797eee…` / `e4dab6ea6b26…` | restored and reworked; global force-update removed |

## 1. Pure LLVM build plumbing

Problem: the 4.14 Kbuild tree had no `LLVM=1` support and would use GCC for
host tools / abort on Android Clang without `CLANG_TRIPLE`.

Change: root `Makefile` now handles `LLVM=1` by overriding CC/HOSTCC/HOSTCXX,
LD, AR, NM, OBJCOPY, OBJDUMP, STRIP, READELF, AS, CROSS_COMPILE and setting
`CLANG_TRIPLE=aarch64-linux-gnu-` for arm64. `LLVM_IAS=1` is forced.

CI: `.github/workflows/miatoll-kernel.yml`, `scripts/fetch-llvm.sh`,
`scripts/build-miatoll.sh`. The build script validates required configs,
builds `Image dtbs modules`, audits first-argv of `.cmd` files for GNU
fallback, and collects artifacts/logs.

## 2. Uclamp

Files: `include/linux/sched/uclamp.h`, `kernel/sched/uclamp.c`, scheduler
core/fair/rt/CASS/schedutil, cpuset, fork, sysctl, UAPI sched_attr, Kconfig,
miatoll defconfig.

Semantics:

- Per-task `uclamp_req[]` and effective `uclamp[]`, runqueue bucket
  aggregation, `UCLAMP_FLAG_IDLE` handling.
- Dual hierarchy: CPU controller (`cpu.uclamp.min/max/latency_sensitive`)
  and cpuset (`uclamp.min/max/latency_sensitive`). A cpuset CSS is **not**
  cast to `task_group`.
- Effective clamp = requested clamp bounded by both parent hierarchies and
  global sysctls; inversion resolved by min ≤ max.
- `sched_attr` extended to 56 bytes (`SCHED_ATTR_SIZE_VER1`) with
  `sched_util_min/max`; flags `KEEP_POLICY`, `KEEP_PARAMS`,
  `UTIL_CLAMP_MIN/MAX`. 48-byte VER0 callers remain accepted.
- Fork/post-fork ordering places `uclamp_post_fork()` after
  `cgroup_post_fork()` so cgroup defaults are visible.
- Schedtune is retained. Raw util gets schedtune margin first, then uclamp
  bounds are applied. CASS uses clamped task/rq util and latency-sensitive
  idle-CPU preference.

Configs: `CONFIG_UCLAMP_TASK=y`, `CONFIG_UCLAMP_TASK_GROUP=y`,
`CONFIG_UCLAMP_BUCKETS_COUNT=5`, `CONFIG_SCHED_TUNE=y`.

## 3. xxBackslashKSU

Vendored exact upstream `kernel/` tree into `drivers/kernelsu/`.

Enabled options: `KSU=y`, `KSU_TAMPER_SYSCALL_TABLE=y` (upstream-recommended
for 3.x–4.14), `KSU_LSM_SECURITY_HOOKS=y`, sulog, adb root, in-tree
heuristic. 32-bit hooks remain enabled because `CONFIG_COMPAT=y`.

No substitution with KernelSU-Next or another fork. Manager identity is the
upstream default (`me.weishu.kernelsu`, pinned expected signature).

## 4. Genuine BBRv3

`net/ipv4/tcp_bbr3.c` registers as `bbr3`; existing `bbr` (v1) and CUBIC
remain. Default congestion control stays CUBIC.

Backported/adapted dependencies:

- BBRv3 private state is heap-allocated because `ICSK_CA_PRIV_SIZE` is too
  small; `.release` frees it.
- `tcp_sock`: `fast_ack_mode`, `tcp_clock_cache`,
  `tlp_orig_data_app_limited`, `plb_rehash`.
- `rate_sample.is_ack_delayed`; TSO-segs callback in `tcp_congestion_ops`.
- Google PLB (`tcp_plb.c`) with per-netns sysctls and MIB counter. PLB is
  disabled by default (`tcp_plb_enabled=0`).
- Compatibility accessors for 4.14 TCP fields; ECN/L4S-style `ecn_low` is a
  load-time opt-in, matching upstream behavior on routes without
  `TCP_ECN_LOW`.

Selection: `sysctl -w net.ipv4.tcp_congestion_control=bbr3`.

## 5. KCal

Restored Qualcomm SDE KCal sysfs interface (`kcal`, `kcal_enable`,
`kcal_min`, `kcal_hue`, `kcal_sat`, `kcal_val`, `kcal_cont`) but removed the
unsafe global single-CRTC force-update.

Changes:

- Per-CRTC tracking of last PCC/HSIC CP properties.
- Sysfs update replays those properties through `sde_cp_crtc_set_property()`
  so the existing SDE CP dirty-list/atomic path owns hardware programming.
- RGB PCC coefficients are scaled in `reg_dmav1_setup_dspp_pccv4()`.
- HSIC is combined with userspace/SDM LiveDisplay requests rather than
  replacing them.

Known limitation: hardware update occurs on the next SDE CP commit; this is
not a guarantee of immediate visible change without a userspace commit.
Runtime validation against VoltageOS LiveDisplay/SDM is still required.

## Verification status (local, 2026-10-31)

`scripts/build-miatoll.sh` completed with `BUILD_EXIT=0` using the pinned
Android Clang 18.0.1 / LLD 18.0.1 toolchain, `ARCH=arm64 LLVM=1 LLVM_IAS=1`,
and `vendor/xiaomi/miatoll_defconfig`.

Evidence: `docs/integration/evidence/phase2/`.

- `vmlinux`: ELF64 AArch64, entry `0xFFFFFF8010080000`.
- Artifacts: `Image`, `cust-atoll-ab.dtb`, four miatoll DTBOs
  (curtana/excalibur/gram/joyeuse), `tcp_bic.ko`, `tcp_htcp.ko`, final
  `.config`, SHA-256 sums.
- Image SHA-256:
  `4b411d50d285d60fab801757777b008babd8431b7809ff7ef333c727c4dea27f`
- Final config SHA-256:
  `16b66e8e8d13c58dc692173fd06d925c8f006cff506f5460f5dd8f686b0005b3`
- Required feature configs validated; schedtune retained; BBRv1 retained;
  default TCP congestion control remains CUBIC.
- First-argv `.cmd` audit: no GNU compiler/linker/binutils fallback
  (clang 3576, ld.lld 5, llvm-nm 1, llvm-objcopy 2, plus host build helpers).
- Build log contains 5 pre-existing-style `-Wconstant-logical-operand`
  warnings from KSU unity-build include paths; no errors. Optional bpfilter
  userspace tool is skipped as in baseline (`CONFIG_BPFILTER` unset).

**No device boot, CTS/VTS, display, root, scheduler, or network runtime test
has been performed in this environment. This is a build-verified integration
branch, not a claim of stable runtime behavior.**

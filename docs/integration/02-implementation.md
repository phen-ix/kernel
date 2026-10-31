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
| xxBackslashKSU | `backslashxx/KernelSU` | `9b74de40162dea776d461c15946aae4064c85e5f` | upstream master snapshot matching the v3.3.1-34 manager generation; `KSU_VERSION=32714`, UAPI v6; only `config KSU` changed to `default n` |
| BBRv3 algorithm | `google/bbr` `bbrv3-2025-03-18` | `90210de4b779d40496dee0b89081780eeddf2a60` | BBRv3 model, not BBRv1 |
| BBRv3 mainline compatibility | `stardock/tcp_bbr_modules` | `45731d211025c30702316590f1f01d7aa59811b9` | compatibility/accessor layer only; reviewed against Google source |
| KCal | removed at the user’s request | Revert of `bd1c180c3` | MSM display sources restored to the pre-integration baseline; no KCal feature is enabled |
| NoMount | `maxsteeel/nomount` v2.2.0 | `bf1847f98fed382369c43bfd652cd93774b9b8e3` | built-in `fs/nomount`; wire protocol/base version 22; Kconfig `default n` and `depends on KEYS` |
| AW8624 haptics | `Saamitk/Kinesis` branch `17` | `3d955e5d8187ad8229f7c6932226a2aa565fe30b` | maintained driver from `arshad-jamil33/android_kernel_xiaomi_sm6250@16.2`; two trailing-whitespace lines stripped locally |

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

## 5. KCal removal

The KCal feature commit `bd1c180c3` was reverted in `21eacd34b` at the
user's request. This removes the added KCal driver files, Kconfig/Makefile
entries, SDE color-processing hooks, PCC/HSIC adjustments, and the
`CONFIG_DRM_MSM_KCAL_CTRL=y` defconfig entry.

The complete `drivers/gpu/drm/msm` tree was compared successfully against
the pre-integration baseline `4afc6f076`. Normal MSM/SDE display support and
the ROM's existing display/color paths remain in place. Uclamp,
xxBackslashKSU, BBRv3, and the Clang 18 workflow are not reverted.

The build validator now rejects an enabled KCal option and still requires
`CONFIG_DRM_MSM=y`. A fresh out-of-tree build is used for the removal check,
rather than treating an old KCal-containing output directory as current.

The earlier KCal-enabled build and its successful GitHub Actions run remain
historical evidence only. Do not use those older artifacts when a KCal-free
kernel is required. See `04-build-and-download.md` for current build/download
instructions and artifact identification.

## Verification status — KCal-free build (local, 2026-10-31)

A **fresh** `scripts/build-miatoll.sh` run completed with `BUILD_EXIT=0` at
KCal removal commit `21eacd34b`, using Android Clang/LLD 18.0.1,
`ARCH=arm64 LLVM=1 LLVM_IAS=1`, and `vendor/xiaomi/miatoll_defconfig`.
The previous output directory was archived before this build, so this was
not an incremental relink of a KCal-enabled kernel.

Current evidence: `docs/integration/evidence/phase2-no-kcal/`.
The superseded KCal-enabled evidence is retained under
`docs/integration/evidence/phase2-before-kcal-revert/`.

- Display source comparison against baseline `4afc6f076`: no differences.
- Final config: no KCal option; `CONFIG_DRM_MSM=y` retained.
- Linked `System.map`: no `sde_hw_kcal_*` or `sde_cp_kcal_*` symbols.
- `vmlinux`: ELF64 AArch64.
- Artifacts: raw `Image`, one DTB, four miatoll DTBOs, two modules, and the
  resolved config. These are build outputs, not a flashable boot image.
- Image SHA-256:
  `3abe167e9979c87c9691e883c07ba93b024e31e697edc428ef6966ac393d32c0`
- Final config SHA-256:
  `08c58a63ca1df2fc2ef9f36fe638acdc022d308f740c0b0757c33f674dfd14b6`
- Uclamp, schedtune, xxBackslashKSU, BBRv1 and BBRv3 configs remain enabled;
  the default TCP congestion control remains CUBIC.
- First-argv `.cmd` audit: clang 3575, ld.lld 5, llvm-nm 1, llvm-objcopy 2;
  no GNU compiler/linker/binutils fallback.
- Clean build: no errors; 15 compiler warning lines are preserved in
  `clean-build.log`. The optional bpfilter userspace tool is skipped as in
  the baseline (`CONFIG_BPFILTER` unset).

The earlier successful GitHub Actions run 41169050563 was **KCal-enabled**
and is not evidence for this removal. Use the new branch run identified in
`04-build-and-download.md` / the delivery message instead.

**No device boot, CTS/VTS, display, root, scheduler, or network runtime test
has been performed in this environment. Compilation does not establish
stable runtime behavior.**

# Phase 1 — miatoll / VoltageOS 6.2 integration audit

Audit date: 2026-10-31. **Status: integration gate passed, with device-validation conditions.**

This is a custom-kernel feasibility audit, not an Android compatibility certification. A successful build does not establish boot, display, scheduler, root, networking, CDD, CTS or VTS compliance. No device tests have been performed.

## 1. Repository and preservation

| Item | Finding |
|---|---|
| Repository | `phen-ix/kernel` |
| Original branch | `16`; left unchanged |
| Working branch | `arena/2197b6cf-kernel` |
| Starting commit | `4afc6f0769f0f3b2b10d50d3fa02f0ba7c89a183` |
| Kernel | Linux **4.14.357-openela**, ARM64 |
| Published history | No tags, releases or Actions workflows at the audit snapshot; only branch `16` was advertised by the remote |
| Target | Xiaomi miatoll / Qualcomm SM6250 (Atoll), Cortex-A55 |
| Device configuration | `arch/arm64/configs/vendor/xiaomi/miatoll_defconfig` |
| Display source | `drivers/gpu/drm/msm/sde/`; there is no `techpack/display` tree |

Historical inspection includes the fork ancestry, `AzyrRuthless/kernel_xiaomi_sm6250`, and the Kinesis `stable-a17` reference (`28b460ad...`). Kinesis uses Clang r383902 and KernelSU v3.2.5; its unrelated touch, USB, memory-control and optimization changes are **not** adopted wholesale. Community boot claims are not test results for this tree.

The original KCal implementation is recoverable through `f36b29f714^`. The abbreviated source commits quoted in older history are not available locally. A later KCal change forced a global PCC/HSIC update; this is not a safe model for atomic display commits and must not be restored unchanged.

## 2. Target ROM contract

The inspected VoltageOS device is `mi-atoll/device_xiaomi_miatoll`, branch `17-volt`, commit `1da1db8c...`. VoltageOS publishes a unified miatoll 6.2 image dated 2026-10-16.

Observed boot contract: ARM64 `Image`, header version 2, 4096-byte pages, separate DTBO, DTB in boot. Preserve the existing four overlay identities: curtana, excalibur, gram and joyeuse. Kernel packaging must not silently substitute another device's DTB/DTBO.

The ROM exposes two different uclamp interfaces:

1. Device task profiles and powerhint use **cpuset** `uclamp.min`, `uclamp.max`, and `uclamp.latency_sensitive`.
2. Generic VoltageOS `system_core` task profiles additionally use the **CPU controller** `cpu.uclamp.min`, `cpu.uclamp.max`, and `cpu.uclamp.latency_sensitive`.

These are not interchangeable files. In particular, passing a cpuset CSS to a CPU-controller `css_tg()` conversion is invalid. The integration must model the two hierarchies explicitly and must not copy a wrapper that merely hides this type error.

The device also retains schedtune initialization and audio-performance paths. Replacing or disabling schedtune is not an acceptable shortcut. The device uses the SDM LiveDisplay HAL, with display mixing disabled, rather than the generic sysfs LiveDisplay backend. VoltageOS Settings RGB control goes through LiveDisplayManager. KCal sysfs availability alone therefore does not establish Settings/HAL/SELinux integration.

Sources inspected:

- https://github.com/mi-atoll/device_xiaomi_miatoll/tree/17-volt
- https://github.com/VoltageOS/system_core/tree/17/libprocessgroup/profiles
- https://github.com/VoltageOS/packages_apps_Settings/blob/17/src/com/android/settings/livedisplay/DisplayColor.java
- https://github.com/LineageOS/android_hardware_lineage_interfaces/tree/lineage-23.2/livedisplay/aidl
- https://www.voltageos.com/devices/download/miatoll

## 3. Baseline configuration and scheduler

Important resolved settings are preserved in `evidence/baseline.config`:

- `CONFIG_CGROUPS=y`, `CONFIG_CGROUP_SCHED=y`, `CONFIG_SCHED_TUNE=y`.
- CASS is present; WALT is disabled. `FAIR_GROUP_SCHED` and `RT_GROUP_SCHED` are disabled.
- `UCLAMP_TASK` and `UCLAMP_TASK_GROUP` are absent, not merely disabled.
- ThinLTO, Polly, Shadow Call Stack, strict kernel/module RWX, SELinux, seccomp, always-on BPF JIT and loadable modules are enabled.
- Kprobes and compat vDSO are not enabled. Do not assume a kprobe-based root integration is available.
- BBRv1 is built in; cubic is the default. HTCP and BIC are modules. `NET_SCHED` is enabled but `NET_SCH_FQ` is not.
- The Qualcomm SDE display driver, DSPP color processing and schedutil are present.

The CASS placement path uses raw task/CFS utilization. Schedutil also uses `boosted_cpu_util()`. A genuine uclamp integration must update task requests, effective constraints, runnable aggregation, wakeup placement and frequency selection deliberately. Adding sysfs files without these consumers is not uclamp. Schedtune and uclamp boosts must not be blindly added together.

## 4. Android 17 assessment

Android 17's supported Android Common Kernel list does not include 4.14. This tree is therefore **outside the supported ACK baseline**, even if it boots a custom ROM. Android 17 CDD section 9.7 still requires the relevant seccomp/BPF, memory-protection and SELinux properties. Traffic accounting and netd depend on working eBPF, not just Kconfig symbols.

The required seccomp, SELinux, BPF and RWX options inspected in the resolved configuration are present. This is only static evidence. Netd program loading, VTS kernel tests, CTS, suspend/resume and userspace compatibility remain device-test gates. No claim of Android 17 certification is made.

References:

- https://source.android.com/docs/core/architecture/kernel/android-common
- https://source.android.com/docs/compatibility/17/android-17-cdd
- https://source.android.com/docs/core/data/ebpf-traffic-monitor
- https://source.android.com/docs/core/architecture/kernel/bpf

## 5. Feature classification and chosen direction

| Area | Classification | Audit result / integration direction |
|---|---|---|
| Base ARM64 build | Fully compatible, verified locally | Pure Clang/LLD 18 baseline builds without removing security or device features. |
| Toolchain | Requires adaptation | Legacy build configuration still names Clang r383902. Pin and verify r522817 (18.0.1), with explicit LLVM tools and no compiler fallback. |
| Uclamp | Requires backporting and adaptation | Backport task/bucket/rq aggregation and scheduler consumers; support both real ROM cgroup ABIs and schedtune coexistence. |
| KCal | Requires adaptation | Use Atoll PCC/HSIC support through the existing per-CRTC SDE property lifecycle. Do not restore global force-update code. Expose conventional sysfs controls but document the ROM HAL gap honestly. |
| xxBackslashKSU | Requires adaptation | User confirmed `backslashxx/KernelSU`. Pin v3.3.1-32, commit `f8aeac629022114d6b366b17048965a28ff398e8`; review legacy hooks, SELinux and compiler hardening. No substitute implementation. |
| BBRv3 | Requires backporting | Existing code is BBRv1. Use Google's genuine v3 source and required TCP/rate-sampling/PLB dependencies, registered as `bbr`. No renamed v1 or empty compatibility shims. |
| Android 17 userspace | Requires device validation | 4.14 is outside supported ACK. ROM interfaces are identified, but boot and framework integration are not established by compilation. |
| Android 17 certification | Not established | No CDD/CTS/VTS certification is claimed. This is not evidence of a fundamental boot blocker. |

### Scheduler sources

- `darkhz/uclamp`, `uclamp_4.14`, head `bc867bf6c38f72b661bcd0f7006e17c8bfcaa3c3`: a 4.14.209 adaptation reference, not a drop-in patch for this CASS tree. Its cpuset-to-CPU-controller wrappers require correction.
- AOSP `kernel_msm`, `android-msm-redbull-4.19-android12`: vendor task/group/bucket and latency-sensitive references. Its CPU-controller interface alone does not satisfy this device's cpuset profiles.
- Relevant vendor commits include `d96bd1d5fc86`, `ad20939c1327`, `c659be787eaf`, `e6056b2a5b47`, and `19718921c3c8`. These will be individually reviewed rather than applying an entire downstream scheduler replacement.

### Root and TCP sources

- Exact requested root upstream: https://github.com/backslashxx/KernelSU
- Confirmed release: `v3.3.1-32`, peeled commit `f8aeac629022114d6b366b17048965a28ff398e8`, UAPI version 6. The manager must match the integrated driver. Issue #43's Exynos bootloop involved an incompatible ISA/LDAPR build; it is not proof that a Cortex-A55 build fails.
- Genuine TCP source: https://github.com/google/bbr, tag `bbrv3-2025-03-18`, commit `90210de4b779...`. Its released v3 file is identical to the subsequently reviewed v3 file at `88aff899355d...`. Modern struct-ops registration must be adapted to the legacy native congestion-ops API without changing the congestion algorithm.

## 6. Unmodified baseline build evidence

The baseline was built out of tree from the unchanged source with:

- `ARCH=arm64`, `SUBARCH=arm64`, `LLVM=1`, `LLVM_IAS=1`.
- Explicit `CC=clang`, `HOSTCC=clang`, `LD=ld.lld`, and LLVM binutils.
- `vendor/xiaomi/miatoll_defconfig`.
- Android Clang/LLD **18.0.1 r522817**, Android build 11967740.
- ThinLTO, Polly and SCS retained.

| Artifact/result | SHA-256 or value |
|---|---|
| Image | `864f55ac6ec78904d59937ff6bcca353939950d5e77e046ccaac10dc8d2e1dc6` |
| vmlinux | `154740711ce93f0c1791b9bf7bbbd012a589b4a3877c60898c960410046d65a9` |
| Resolved `.config` | `1f3aea3ee2aa4759cb47df84b3884ab67d0c9dbbe634f2cf3d856284388b7103` |
| clang-18 | `5ab8ad3d455ff09c8eb8f4c7dbc70fb5d5396c7910342a18fd68e848b12f749d` |
| lld | `8cc2d6d52e3b53581616ac735549de4afe8e3194ee65ae543cd2230833c0ba91` |
| llvm-ar | `ddbf59740630a74ad2ff30c7c59189a02e01f4f8da138fd78be0afc959348e93` |
| Result | Build exit 0; 3,737 object files; Image, vmlinux, DTB, four DTBOs and two TCP modules |
| ELF | AArch64 ELF64; `.comment` identifies Clang 18.0.1 and LLD 18.0.1 |
| Command audit | 3,571 recorded clang invocations; five LLD links; no GNU compiler/linker executable |

Evidence is in `evidence/`: configuration, build/tool logs, artifact hashes and toolchain identity. Generated binaries are not committed into the source repository.

The AOSP prebuilt mirror `msft-mirror-aosp/platform.prebuilts.clang.host.linux-x86`, final r522817 import `6a853dd45fe8...`, was compared against the verified checkout. The compiler binary hashes above match. Host flex/bison/bc and their runtime libraries came from pinned Lineage build-tools; host OpenSSL development support was supplied separately. These host utilities are not GNU cross-compilers.

### Baseline diagnostics retained

The build reports ten source warnings: mixed declarations/code in `mm/vmscan.c`, unused variables in schedutil/KGSL, and pointer-to-integer casts in two power-supply drivers. They are recorded, not hidden or treated as successful fixes.

`net/Makefile:24: CC cannot link executables. Skipping bpfilter.` also occurs. `CONFIG_BPFILTER` is unset and this legacy optional userspace helper is not the Android eBPF traffic-accounting implementation. This does not establish Android eBPF runtime correctness.

## 7. Go/no-go decision and release gates

**GO for staged source integration.** No fundamental source/build incompatibility was found. The unsupported ACK age is a material risk, not a reason to pretend this is an official Android 17 kernel.

Before any build may be described as device-stable, the following remain mandatory:

1. Reproducible GitHub Actions build using only verified Clang 18, with resolved feature configuration and artifact inspection.
2. Boot/rollback tests on the target ROM and all intended miatoll variants, including DTB/DTBO, touch, Wi-Fi, cameras, audio, charging, suspend and recovery.
3. KCal visual and lifecycle tests: RGB neutrality/ranges, saturation/contrast capability, atomic commits, blank/unblank, idle power collapse and HAL ownership.
4. Scheduler tests for both cgroup interfaces, migration, task clamp updates, min/max bounds, RT defaults, schedtune coexistence, frequency response and latency.
5. Matching KernelSU manager/UAPI tests, hook operation, SELinux behavior, non-root application isolation and reboot persistence.
6. Actual BBRv3 runtime selection and behavior, packet pacing, loss/ECN response and preservation of existing congestion algorithms.
7. Android netd/eBPF, CTS/VTS and relevant CDD checks. Until these pass, call the output an **integration candidate**, not a stable or certified release.

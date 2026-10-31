# Final pre-test integration (2026-10-31)

This is the last source/build step before device testing. It is **not** a
stability claim: no boot, root, mount, haptic, thermal, Wi-Fi, or performance
runtime test has been performed on this tree.

## Upstream refresh and versions inside

| Component | Pin | Version inside | Notes |
|---|---|---|---|
| xxKSU | `backslashxx/KernelSU` master `9b74de40162dea776d461c15946aae4064c85e5f` | `KSU_VERSION=32714`, UAPI `6` | Previous package was v3.3.1-32 / 32707. The v3.3.1-34 release tag itself peels to `74d0441924…` with ksuver 32713, while the release manager APK and current master are 32714; we pinned the master commit that matches the manager generation. |
| NoMount | `maxsteeel/nomount` v2.2.0 `bf1847f98fed382369c43bfd652cd93774b9b8e3` | base/wire protocol `22` | User-selected stable implementation. Built into the kernel (`CONFIG_NOMOUNT=y`). The matching userspace metamodule `NoMount-v2.2.0-release-605.zip` must be installed separately through the KernelSU manager; driver and metamodule must be updated together. |
| Compiler | AOSP/Lineage Clang `r522817` | Clang/LLD 18.0.1 | Chosen over Clang 22 r596125 for this 4.14 tree: r522817 is already build-verified here, while the only r596125 prebuilt found was a third-party 1.09 GB mirror with no miatoll runtime evidence. Newer is not automatically safer for a vendor 4.14 kernel. |

## SM6250 kernel survey and ported improvements

Reviewed `Saamitk/Kinesis` branches 16/17, `mad1mystic/kinesis-a17`
(a17-8 boot-verified on VoltageOS 6.2/excalibur; a17-12-rc1), and
LineageOS `lineage-22.2` sm6250 references.

Ported:

1. **Android 17 cgroup-v2 controller enablement**
   - `CONFIG_MEMCG=y`
   - `CONFIG_FAIR_GROUP_SCHED=y`
   - `CONFIG_VM_EVENT_COUNTERS=y`
   - Rationale: kinesis-a17 reports Android 17 userspace expects the `memory`
     controller in `cgroup.controllers`; `VM_EVENT_COUNTERS` supports lmkd
     vmstat reclaim fallback. Kept `MEMCG_SWAP`, `CFS_BANDWIDTH`, and
     `RT_GROUP_SCHED` off to match the boot-tested Android 17 configuration.
2. **AW8624 haptic driver refresh** from Kinesis@17 (`3d955e5d`), originally
   `arshad-jamil33@16.2`: effect-id handling, ulevel gain scaling, reset-GPIO
   guard, quieter logging, and removal of the `qti-haptics` alias.

Deliberately not ported:

- SuSFS: not requested; adding it silently would expand the security surface.
- FT8756 auto-upgrade re-enable: panel-firmware risk without panel-specific proof.
- Disabling KPROBES/SCS: our xxKSU uses syscall-table + LSM hooks, not the
  kprobe `selinux_hide` path that bootlooped kinesis-a17; SCS remains enabled.
- Whole-kernel merges or scheduler replacements: would violate the
  preserve-device-functionality constraint.

## Local build verification

Command:

```sh
HOST_OPENSSL_INCLUDE=/usr/local/include/node \
HOST_OPENSSL_LIBDIR="$PWD/.work/host-lib" \
scripts/build-miatoll.sh
```

Result: `BUILD_EXIT=0`, `FINAL_PRETEST_BUILD_EXIT=0`.

- Config gates passed: KSU, NoMount protocol 22, MEMCG, FAIR_GROUP_SCHED,
  VM_EVENT_COUNTERS, KEYS, uclamp task+group, schedtune, BBR+BBRv3, DRM_MSM.
- KCal negative gate passed; MSM display retained.
- Tool audit: no GNU/non-LLVM compile/link invocations in `.cmd` files.
- `nomount_init` and xxKSU symbols are linked into `vmlinux`.
- AnyKernel3 ZIP structure verification passed; DTBO is 4 entries, page 4096.

Evidence: `docs/integration/evidence/phase3-final-pretest/`.
The local pre-commit package was built from the working tree that became the
final source commit; CI rebuilds the exact pushed commit and is the authoritative
artifact for that SHA.

## Flashing and first boot

1. Flash the AnyKernel3 ZIP in recovery (A-only miatoll layout).
2. Boot once without adding new modules.
3. Install/refresh the xxKSU manager (v3.3.1-34 generation, 32714).
4. Install `NoMount-v2.2.0-release-605.zip` as a KernelSU metamodule.
5. Verify root, then NoMount rules, then haptics/display/touch/Wi-Fi/thermal.

Do not treat compilation as runtime proof. Keep a known-good boot/DTBO backup.

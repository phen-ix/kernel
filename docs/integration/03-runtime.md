# Runtime validation checklist (miatoll / VoltageOS 6.2 / Android 17)

The kernel branch is build-verified only. Before calling it stable, test on a
recoverable device with a known-good boot image and working fastboot/EDL path.
Build and download instructions are in `04-build-and-download.md`.

## Boot / Android

1. Flash/boot the produced `Image` with the matching DTB/DTBO and Android 17
   vendor_boot/boot layout used by the ROM.
2. Verify `uname -a`, SELinux enforcing, no init first-stage mount failures,
   no bootloop, no thermal/watchdog reset.
3. Run applicable CTS/VTS kernel/security tests (seccomp, SELinux, RWX, BPF).
   Android 17 ACK does not include 4.14; custom-kernel compatibility must be
   proven on the actual ROM.

## Uclamp / schedtune

- Check both interfaces exist:
  - `/dev/cpuctl/cpu.uclamp.min`, `cpu.uclamp.max`,
    `cpu.uclamp.latency_sensitive`
  - cpuset `uclamp.min`, `uclamp.max`, `uclamp.latency_sensitive`
- Verify Android task profiles write both hierarchies without `EINVAL`.
- Check `sched_util_clamp_*` sysctls and `sched_setattr` util-clamp flags.
- Confirm schedtune boost still works and is not double-counted.
- Watch for runaway minimum frequency, idle drain, or CASS placement regressions.

## Display / KCal removed

- KCal integration has been reverted. Do not expect the added `kcal_ctrl`
  sysfs interface, and do not use KCal configuration apps with this build.
- Verify normal display boot, blank/unblank, brightness, and the ROM's
  existing LiveDisplay/color behavior. These remain device-runtime tests.
- Confirm the final config has no `CONFIG_DRM_MSM_KCAL_CTRL` setting and
  still has `CONFIG_DRM_MSM=y`.

## xxBackslashKSU

- Install the matching `backslashxx/KernelSU` manager (UAPI v6, pinned
  upstream commit). Verify manager signature/allowlist behavior.
- Check `dmesg` for KSU init, syscall-table hook, LSM hook, and SELinux policy
  reload messages.
- Test root grant/deny, module mounts, adb root, sulog, and boot completion.
- Confirm 32-bit app compatibility if used (`CONFIG_COMPAT=y`, hooks enabled).

## BBRv3

- `sysctl net.ipv4.tcp_available_congestion_control` should include `bbr3`.
- Select: `sysctl -w net.ipv4.tcp_congestion_control=bbr3`.
- Verify with `ss -ti` / inet_diag that flows use BBRv3 fields.
- PLB is present but disabled by default; enable only with a tested fabric:
  `net.ipv4.tcp_plb_enabled=1`.
- ECN/L4S-style `ecn_low` is load-time opt-in for the module; default matches
  upstream behavior on routes without `TCP_ECN_LOW`.
- Compare throughput/latency/retransmits against CUBIC and BBRv1 on the target
  network. Keep CUBIC as default until validated.

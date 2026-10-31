# KCal-free build evidence

The fresh compile captured here ran at KCal removal commit
`21eacd34b7c978dd4c12bc030b91702b0c2e6d43`. The old output directory was
archived first; `clean-build.log` records a full build, not just a relink.
Subsequent documentation/checksum-packaging changes do not alter the kernel
sources used for this compile. The delivery message identifies the GitHub
Actions run for the final branch HEAD.

Key checks:

- `clean-build.log`: `BUILD_EXIT=0`, pinned Clang/LLD 18.0.1.
- `final.config`: no `CONFIG_DRM_MSM_KCAL_CTRL`; normal MSM display and the
  other requested feature configs remain.
- `kcal-removal-check.log`: display sources match the original baseline,
  KCal is absent from config and linked symbols, and the kernel is AArch64.
- `tool-audit.log`: no GNU compiler/linker/binutils fallback.
- `symbols.txt`: selected retained-feature symbols and the KCal absence check.

The downloadable artifact additionally contains relative-path `SHA256SUMS`
and `BUILD_INFO.txt`. Raw images and device trees are not a flashable ZIP.
No on-device/runtime test is claimed by these files.

The Image hash in the implementation report belongs to this captured clean
build. A later rebuild can embed a different build timestamp/version; verify
any downloaded package against its own `SHA256SUMS`, not an older run's hash.

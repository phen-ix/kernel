#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0
# Reproducible pure-Clang 18 miatoll build. No GNU cross compiler is used.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT_DIR="${OUT_DIR:-$ROOT/out/miatoll}"
TOOLCHAIN_ROOT="${TOOLCHAIN_ROOT:-$ROOT/.work/toolchain}"
BUILDTOOLS_ROOT="${BUILDTOOLS_ROOT:-$ROOT/.work/build-tools}"
CLANG_DIR="$TOOLCHAIN_ROOT/clang-r522817"
LOG_DIR="${LOG_DIR:-$OUT_DIR/logs}"
ARTIFACT_DIR="${ARTIFACT_DIR:-$OUT_DIR/artifacts}"
DEFCONFIG="vendor/xiaomi/miatoll_defconfig"

mkdir -p "$OUT_DIR" "$LOG_DIR" "$ARTIFACT_DIR"
export PATH="$CLANG_DIR/bin:$BUILDTOOLS_ROOT/path/linux-x86:$BUILDTOOLS_ROOT/linux-x86/bin:$PATH"
export LD_LIBRARY_PATH="$BUILDTOOLS_ROOT/linux-x86/lib64${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

{
  echo "=== Toolchain ==="
  clang --version | head -1
  ld.lld --version
  llvm-ar --version | head -1
  flex --version
  bison --version | head -1
  bc --version | head -1
  echo "ARCH=arm64 SUBARCH=arm64 LLVM=1 LLVM_IAS=1"
  echo "DEFCONFIG=$DEFCONFIG"
  echo "SOURCE_COMMIT=$(git -C "$ROOT" rev-parse HEAD)"
} | tee "$LOG_DIR/toolchain.txt"

clang --version | head -1 | grep -F 'clang version 18.0.1' >/dev/null
ld.lld --version | grep -F 'LLD 18.0.1' >/dev/null

MAKE_ARGS=(
  O="$OUT_DIR"
  ARCH=arm64
  SUBARCH=arm64
  LLVM=1
  LLVM_IAS=1
)

# Host OpenSSL headers/libs may be split on CI images. Prefer the distro
# libssl-dev package; allow an explicit fallback include directory.
if [[ -n "${HOST_OPENSSL_INCLUDE:-}" ]]; then
  export HOST_EXTRACFLAGS="${HOST_EXTRACFLAGS:-} -I$HOST_OPENSSL_INCLUDE"
fi
if [[ -n "${HOST_OPENSSL_LIBDIR:-}" ]]; then
  MAKE_ARGS+=(HOSTLDFLAGS="-L$HOST_OPENSSL_LIBDIR")
fi

make -C "$ROOT" "${MAKE_ARGS[@]}" "$DEFCONFIG" 2>&1 | tee "$LOG_DIR/defconfig.log"

required_configs=(
  CONFIG_UCLAMP_TASK=y
  CONFIG_UCLAMP_TASK_GROUP=y
  CONFIG_UCLAMP_BUCKETS_COUNT=5
  CONFIG_SCHED_TUNE=y
  CONFIG_KSU=y
  CONFIG_KSU_TAMPER_SYSCALL_TABLE=y
  CONFIG_KSU_LSM_SECURITY_HOOKS=y
  CONFIG_TCP_CONG_BBR=y
  CONFIG_TCP_CONG_BBR3=y
  CONFIG_DRM_MSM_KCAL_CTRL=y
  CONFIG_DEFAULT_TCP_CONG=\"cubic\"
  CONFIG_ARM64=y
  CONFIG_MODULES=y
  CONFIG_LTO_CLANG=y
  CONFIG_SHADOW_CALL_STACK=y
)
for cfg in "${required_configs[@]}"; do
  if ! grep -qxF "$cfg" "$OUT_DIR/.config"; then
    echo "ERROR: required config missing: $cfg" >&2
    exit 1
  fi
done
if grep -qE '^(CONFIG_FAIR_GROUP_SCHED|CONFIG_RT_GROUP_SCHED)=y' "$OUT_DIR/.config"; then
  echo "ERROR: unexpected group scheduler config" >&2
  exit 1
fi
echo "Required config validation passed." | tee "$LOG_DIR/config-validation.log"

set +e
make -C "$ROOT" "${MAKE_ARGS[@]}" -j"$(nproc)" Image dtbs modules \
  2>&1 | tee "$LOG_DIR/build.log"
build_rc=${PIPESTATUS[0]}
set -e
echo "BUILD_EXIT=$build_rc" | tee -a "$LOG_DIR/build.log"
if [[ "$build_rc" -ne 0 ]]; then
  exit "$build_rc"
fi

# Audit first argv of every recorded compile/link command. This catches an
# accidental GNU fallback even when make itself exits successfully.
python3 - "$OUT_DIR" <<'PY' | tee "$LOG_DIR/tool-audit.log"
import collections, pathlib, shlex, sys
out = pathlib.Path(sys.argv[1])
counts = collections.Counter()
bad = []
for cmdfile in out.rglob('.*.cmd'):
    try:
        text = cmdfile.read_text(errors='replace')
    except OSError:
        continue
    for line in text.splitlines():
        if not line.startswith(('cmd_', 'savedcmd_')) or ':=' not in line:
            continue
        cmd = line.split(':=', 1)[1].strip()
        if not cmd:
            continue
        try:
            argv = shlex.split(cmd)
        except ValueError:
            continue
        if not argv:
            continue
        tool = pathlib.Path(argv[0]).name
        counts[tool] += 1
        if tool in {'gcc', 'g++', 'cc', 'c++', 'ld', 'ld.bfd', 'as', 'ar', 'nm', 'objcopy', 'objdump', 'strip', 'readelf'}:
            bad.append((str(cmdfile), tool, cmd[:160]))
for tool, n in sorted(counts.items()):
    print(f'{tool}: {n}')
if bad:
    print('ERROR: GNU/non-LLVM tool invocation detected:')
    for path, tool, cmd in bad[:20]:
        print(f'{path}: {tool}: {cmd}')
    sys.exit(1)
print('PASS: no GNU/non-LLVM tool invocations found in .cmd files')
PY

cp "$OUT_DIR/.config" "$ARTIFACT_DIR/miatoll.config"
cp "$OUT_DIR/arch/arm64/boot/Image" "$ARTIFACT_DIR/Image"
cp "$OUT_DIR/arch/arm64/boot/dts/qcom/"*.dtb "$ARTIFACT_DIR/" 2>/dev/null || true
cp "$OUT_DIR/arch/arm64/boot/dts/qcom/"*.dtbo "$ARTIFACT_DIR/" 2>/dev/null || true
find "$OUT_DIR" -name '*.ko' -exec cp {} "$ARTIFACT_DIR/" \;
sha256sum "$ARTIFACT_DIR"/* > "$ARTIFACT_DIR/SHA256SUMS"
echo "Artifacts written to $ARTIFACT_DIR"

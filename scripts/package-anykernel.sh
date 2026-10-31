#!/usr/bin/env bash
# Package the verified miatoll kernel artifacts into an AnyKernel3 flashable ZIP.
#
# Inputs:  out/miatoll/artifacts/{Image,cust-atoll-ab.dtb,*-overlay.dtbo,...}
# Output:  out/miatoll/anykernel/AnyKernel3-miatoll-<short-sha>.zip
#
# Device layout (mi-atoll/device_xiaomi_miatoll @ 17-volt, BoardConfig.mk):
#   A-only (AB_OTA_UPDATER := false), boot header v2, page size 4096,
#   DTB included in boot image, separated DTBO partition.
#   OTA assert: curtana,excalibur,gram,joyeuse,miatoll
#
# AnyKernel3 upstream is pinned; mkdtboimg.py comes from AOSP libufdt (Lineage mirror).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ARTIFACT_DIR="${ARTIFACT_DIR:-$ROOT/out/miatoll/artifacts}"
OUT_DIR="${OUT_DIR:-$ROOT/out/miatoll}"
AK_OUT="$OUT_DIR/anykernel"
WORK="$ROOT/.work/anykernel-package"

ANYKERNEL3_REPO="osm0sis/AnyKernel3"
ANYKERNEL3_COMMIT="41f04bdb4ee30a44e429c2c1131d5e8bb32b15f1"
LIBUFDT_REPO="LineageOS/android_system_libufdt"
LIBUFDT_COMMIT="df92216e3fc5f3fe289423a7819bd4a5ed3bd443"

DTBO_PAGE_SIZE=4096
DTBO_FILES=(
  curtana-atoll-ab-idp-overlay.dtbo
  excalibur-atoll-ab-idp-overlay.dtbo
  gram-atoll-ab-idp-overlay.dtbo
  joyeuse-atoll-ab-idp-overlay.dtbo
)

die() { echo "ERROR: $*" >&2; exit 1; }

[[ -d "$ARTIFACT_DIR" ]] || die "Artifact directory not found: $ARTIFACT_DIR (run scripts/build-miatoll.sh first)"
[[ -f "$ARTIFACT_DIR/Image" ]] || die "Missing Image"
[[ -f "$ARTIFACT_DIR/cust-atoll-ab.dtb" ]] || die "Missing cust-atoll-ab.dtb"
[[ -f "$ARTIFACT_DIR/miatoll.config" ]] || die "Missing miatoll.config"
[[ -f "$ARTIFACT_DIR/BUILD_INFO.txt" ]] || die "Missing BUILD_INFO.txt"

# KCal must be absent from the config used for this package.
if grep -q '^CONFIG_DRM_MSM_KCAL_CTRL=' "$ARTIFACT_DIR/miatoll.config"; then
  die "KCal is enabled in miatoll.config; this package is for the KCal-free kernel"
fi
grep -q '^CONFIG_DRM_MSM=y' "$ARTIFACT_DIR/miatoll.config" || die "CONFIG_DRM_MSM=y not found"
grep -q '^CONFIG_NOMOUNT=y' "$ARTIFACT_DIR/miatoll.config" || die "CONFIG_NOMOUNT=y not found"
grep -q '^CONFIG_KSU=y' "$ARTIFACT_DIR/miatoll.config" || die "CONFIG_KSU=y not found"
grep -q '^CONFIG_MEMCG=y' "$ARTIFACT_DIR/miatoll.config" || die "CONFIG_MEMCG=y not found"
grep -q '^CONFIG_FAIR_GROUP_SCHED=y' "$ARTIFACT_DIR/miatoll.config" || die "CONFIG_FAIR_GROUP_SCHED=y not found"

for f in "${DTBO_FILES[@]}"; do
  [[ -f "$ARTIFACT_DIR/$f" ]] || die "Missing DTBO: $f"
done

# Verify artifact checksums before packaging.
if [[ -f "$ARTIFACT_DIR/SHA256SUMS" ]]; then
  (cd "$ARTIFACT_DIR" && sha256sum -c SHA256SUMS) || die "Artifact checksum verification failed"
fi

SOURCE_SHA="$(sed -n 's/^SOURCE_COMMIT=//p' "$ARTIFACT_DIR/BUILD_INFO.txt" | head -1)"
[[ -n "$SOURCE_SHA" ]] || SOURCE_SHA="$(git -C "$ROOT" rev-parse HEAD)"
SHORT_SHA="${SOURCE_SHA:0:9}"

mkdir -p "$WORK" "$AK_OUT"

# Fetch pinned AnyKernel3.
AK_SRC="$WORK/AnyKernel3-$ANYKERNEL3_COMMIT"
if [[ ! -d "$AK_SRC/tools" ]]; then
  echo "Fetching AnyKernel3 @ $ANYKERNEL3_COMMIT ..."
  mkdir -p "$AK_SRC"
  curl -fsSL "https://codeload.github.com/$ANYKERNEL3_REPO/tar.gz/$ANYKERNEL3_COMMIT" \
    | tar xz -C "$AK_SRC" --strip-components=1
fi
[[ -f "$AK_SRC/tools/ak3-core.sh" ]] || die "AnyKernel3 fetch incomplete"

# Fetch pinned mkdtboimg.py.
MKDTBOIMG="$WORK/libufdt-$LIBUFDT_COMMIT/utils/src/mkdtboimg.py"
if [[ ! -f "$MKDTBOIMG" ]]; then
  echo "Fetching libufdt @ $LIBUFDT_COMMIT ..."
  mkdir -p "$WORK/libufdt-$LIBUFDT_COMMIT"
  curl -fsSL "https://codeload.github.com/$LIBUFDT_REPO/tar.gz/$LIBUFDT_COMMIT" \
    | tar xz -C "$WORK/libufdt-$LIBUFDT_COMMIT" --strip-components=1
fi
[[ -f "$MKDTBOIMG" ]] || die "mkdtboimg.py fetch incomplete"

# Build dtbo.img from the four device overlay blobs.
DTBO_ARGS=()
for f in "${DTBO_FILES[@]}"; do DTBO_ARGS+=("$ARTIFACT_DIR/$f"); done
python3 "$MKDTBOIMG" create "$WORK/dtbo.img" --page_size="$DTBO_PAGE_SIZE" "${DTBO_ARGS[@]}"
python3 "$MKDTBOIMG" dump "$WORK/dtbo.img" > "$WORK/dtbo-dump.txt"
grep -q 'dt_entry_count = 4' "$WORK/dtbo-dump.txt" || die "dtbo.img does not contain 4 entries"
grep -q "page_size = $DTBO_PAGE_SIZE" "$WORK/dtbo-dump.txt" || die "dtbo.img page size mismatch"

# Stage the AnyKernel3 tree.
STAGE="$WORK/stage"
rm -rf "$STAGE"
mkdir -p "$STAGE"
cp -a "$AK_SRC/tools" "$STAGE/"
cp -a "$AK_SRC/META-INF" "$STAGE/"
cp -a "$AK_SRC/LICENSE" "$STAGE/"
# Remove upstream example placeholders / README; keep only what the installer needs.
rm -f "$STAGE/README.md" "$STAGE/anykernel.sh"
rm -rf "$STAGE/.github" "$STAGE/patch" "$STAGE/ramdisk" "$STAGE/modules"

cp "$ARTIFACT_DIR/Image" "$STAGE/Image"
cp "$ARTIFACT_DIR/cust-atoll-ab.dtb" "$STAGE/dtb"
cp "$WORK/dtbo.img" "$STAGE/dtbo.img"

# Device-specific installer script.
cat > "$STAGE/anykernel.sh" <<'EOF'
### AnyKernel3 Ramdisk Mod Script — miatoll (KCal-free, Clang 18)
## Device: Xiaomi Redmi Note 9 Pro / 9S / 9 Pro Max / Poco M2 Pro (miatoll)
## Layout: A-only, boot header v2, DTB-in-boot, separate DTBO partition.

properties() { '
kernel.string=phen-ix miatoll kernel (Clang 18, KCal-free)
do.devicecheck=1
do.modules=0
do.systemless=0
do.cleanup=1
do.cleanuponabort=1
device.name1=miatoll
device.name2=curtana
device.name3=joyeuse
device.name4=excalibur
device.name5=gram
supported.versions=
supported.patchlevels=
supported.vendorpatchlevels=
'; }

# No ramdisk overlay is shipped; preserve the existing boot ramdisk as-is.
boot_attributes() { :; }

BLOCK=boot;
IS_SLOT_DEVICE=0;
RAMDISK_COMPRESSION=auto;
# ROM already ships vbmeta with AVB flags=3; keep the original flags.
PATCH_VBMETA_FLAG=0;

. tools/ak3-core.sh;

dump_boot;
write_boot;
### end install
EOF

# Build metadata for the flashable package.
{
  echo "PACKAGE=AnyKernel3-miatoll-${SHORT_SHA}"
  echo "KERNEL_SOURCE_COMMIT=$SOURCE_SHA"
  echo "DEVICE=miatoll (curtana/joyeuse/excalibur/gram)"
  echo "ANDROID_TARGET=17 (VoltageOS 6.2)"
  echo "BOOT_LAYOUT=A-only, header v2, page size 4096, DTB-in-boot, separate DTBO"
  echo "ANYKERNEL3_COMMIT=$ANYKERNEL3_COMMIT"
  echo "LIBUFDT_COMMIT=$LIBUFDT_COMMIT"
  echo "DTBO_PAGE_SIZE=$DTBO_PAGE_SIZE"
  echo "KCAL=absent"
  echo "KSU_VERSION=$(sed -n 's/.*-DKSU_VERSION=\([0-9]*\).*/\1/p' "$ROOT/drivers/kernelsu/Makefile" | head -1)"
  echo "KSU_UAPI_VERSION=$(sed -n 's/^#define KERNEL_SU_UAPI_VERSION \([0-9]*\).*/\1/p' "$ROOT/drivers/kernelsu/include/uapi/supercall.h" | head -1)"
  echo "NOMOUNT_PROTOCOL=$(sed -n 's/^#define NOMOUNT_BASE_VERSION "\([0-9]*\)".*/\1/p' "$ROOT/fs/nomount/nomount.h" | head -1)"
  echo "NOMOUNT_UPSTREAM=maxsteeel/nomount v2.2.0"
  echo "RUNTIME_TESTED=no"
  echo "MODULES_INCLUDED=no (tcp_bic/tcp_htcp remain optional raw artifacts)"
} > "$STAGE/PACKAGE_INFO.txt"

cat > "$STAGE/README.txt" <<EOF
AnyKernel3 flashable kernel — miatoll (KCal-free)
=================================================
Kernel source commit: $SOURCE_SHA
Toolchain: Android Clang/LLD 18.0.1 (LLVM=1 LLVM_IAS=1)
Target: VoltageOS 6.2 / Android 17, Xiaomi miatoll
Devices: curtana, joyeuse, excalibur, gram (unified miatoll)

What this ZIP does:
  - Replaces the kernel (Image) and DTB in the existing boot image.
  - Flashes a matching dtbo.img (4 overlays, page size 4096).
  - Preserves the existing boot ramdisk. No Magisk/KernelSU module is installed.
  - Device check restricts installation to miatoll/curtana/joyeuse/excalibur/gram.

What this ZIP does NOT do:
  - It does not flash a ROM, recovery, vbmeta, or modem.
  - It does not include optional tcp_bic.ko / tcp_htcp.ko modules.
  - NoMount kernel support is built in, but the NoMount userspace metamodule
    is NOT installed by this ZIP. Install maxsteeel/nomount v2.2.0 (protocol 22)
    from the KernelSU manager after booting this kernel.
  - It has NOT been runtime-tested on a device. Compilation and packaging
    verification only. Keep a known-good boot/dtbo backup before flashing.

Flash from a custom recovery (TWRP/OrangeFox/Lineage Recovery). This is a
recovery-flashable AnyKernel3 ZIP, not a Magisk/KernelSU module. Wiping
Dalvik/ART cache after flashing is optional.

If the device fails to boot, restore your previous boot.img and dtbo.img
via fastboot or recovery. This kernel is provided for testing, not as a
stability-certified release.
EOF

# Create the ZIP (store updater-script uncompressed is not required; deflate all).
ZIP_NAME="AnyKernel3-miatoll-${SHORT_SHA}.zip"
ZIP_PATH="$AK_OUT/$ZIP_NAME"
rm -f "$ZIP_PATH"
(cd "$STAGE" && python3 - "$ZIP_PATH" <<'PY'
import os, sys, zipfile
out = sys.argv[1]
with zipfile.ZipFile(out, 'w', compression=zipfile.ZIP_DEFLATED, compresslevel=9) as z:
    for root, dirs, files in os.walk('.'):
        dirs.sort()
        for f in sorted(files):
            p = os.path.join(root, f)
            arc = os.path.relpath(p, '.')
            z.write(p, arc)
PY
)

# Verify the ZIP is readable and contains the expected payload.
python3 - "$ZIP_PATH" <<'PY'
import sys, zipfile
required = {
    'anykernel.sh', 'Image', 'dtb', 'dtbo.img',
    'META-INF/com/google/android/update-binary',
    'META-INF/com/google/android/updater-script',
    'tools/ak3-core.sh', 'tools/magiskboot', 'tools/busybox',
    'PACKAGE_INFO.txt', 'README.txt',
}
with zipfile.ZipFile(sys.argv[1]) as z:
    bad = z.testzip()
    if bad:
        raise SystemExit(f'Corrupt ZIP member: {bad}')
    names = set(z.namelist())
    missing = required - names
    if missing:
        raise SystemExit(f'Missing ZIP members: {sorted(missing)}')
print('ZIP structure verification: PASS')
PY

sha256sum "$ZIP_PATH" > "$ZIP_PATH.sha256"
echo "AnyKernel3 package: $ZIP_PATH"
echo "SHA-256: $(cut -d' ' -f1 "$ZIP_PATH.sha256")"

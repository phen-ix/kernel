# Build and download DOOM Kernel for miatoll

## Which source to use

- Repository: `phen-ix/kernel`
- Branch: **`arena/2197b6cf-kernel`**
- KCal removal commit: **`21eacd34b`** (or a later commit on this branch)
- Defconfig: `vendor/xiaomi/miatoll_defconfig`
- Toolchain: pinned Android **Clang/LLD 18.0.1**, `LLVM=1 LLVM_IAS=1`
- KCal: **removed**. Uclamp, xxBackslashKSU, BBRv3, schedtune, and MSM display
  support are retained.

Do not select the original `16` branch when you want these integration
changes. Do not download the older run **41169050563**: it passed, but it
built commit `72ccb7e1d`, which still contained KCal.

## Option A — compile with GitHub Actions

The workflow is `.github/workflows/miatoll-kernel.yml`, displayed as
**DOOM Kernel — miatoll (Clang 18)**. A push to `arena/2197b6cf-kernel` starts it
automatically. It fetches and checks the pinned toolchain, builds the device
defconfig, checks required features and KCal absence, audits compiler/linker
commands, and uploads the outputs and logs.

### Start or find a build in the browser

1. Open <https://github.com/phen-ix/kernel/actions/workflows/miatoll-kernel.yml>.
2. Select a **completed, successful** run on `arena/2197b6cf-kernel` whose
   commit includes `21eacd34b`. Check the run's commit SHA, not just its name.
3. To request another build, use **Run workflow** and select
   `arena/2197b6cf-kernel`, if the branch selector is available. Alternatively,
   push a commit to that branch; no source change is needed to follow the
   automatic run already started by the removal push.
4. Wait for the **build** job to finish with a green check. The first
   toolchain download can take several minutes; a cache hit is faster.
5. In the run's **Artifacts** section, download:

   ```text
   DOOM-Kernel-miatoll-raw-<full-commit-sha>
   ```

GitHub downloads this as a ZIP. Inside it are `artifacts/` and `logs/`.
Artifacts are retained for **14 days**. A newer push to the same branch can
cancel an older in-progress run because the workflow uses concurrency
cancellation; use the run for the commit you actually want.

### Download with GitHub CLI

Install and authenticate GitHub CLI (`gh auth login`) on your computer.
Replace `RUN_ID` with the number in the successful run's URL.

```bash
REPO=phen-ix/kernel
RUN_ID=<successful-run-id>

# Check the branch, exact source commit, and conclusion before downloading.
gh run view "$RUN_ID" --repo "$REPO" \
  --json headBranch,headSha,status,conclusion,url

gh run watch "$RUN_ID" --repo "$REPO" --exit-status

SHA="$(gh run view "$RUN_ID" --repo "$REPO" --json headSha --jq '.headSha')"
gh run download "$RUN_ID" --repo "$REPO" \
  --name "DOOM-Kernel-miatoll-raw-$SHA" \
  --dir ./miatoll-no-kcal

cd miatoll-no-kcal/artifacts
sha256sum -c SHA256SUMS

# KCal must be absent; the normal MSM display driver must remain enabled.
if grep -q '^CONFIG_DRM_MSM_KCAL_CTRL=' miatoll.config; then
  echo 'ERROR: this is not a KCal-free config' >&2
  exit 1
fi
grep '^CONFIG_DRM_MSM=y' miatoll.config
```

To request a manual run from the CLI, explicitly select the working branch:

```bash
gh workflow run miatoll-kernel.yml \
  --repo phen-ix/kernel \
  --ref arena/2197b6cf-kernel

gh run list --repo phen-ix/kernel \
  --workflow miatoll-kernel.yml \
  --branch arena/2197b6cf-kernel --limit 5
```

The repository's default branch is `16`; selecting the working branch is
important. If the manual-run control is unavailable in your GitHub UI, use
the automatic push-triggered run instead.

## Option B — compile locally on Linux x86-64

Ubuntu 22.04 x86-64 is the CI environment. The fetched compiler and host
tools are Linux prebuilts; use a Linux machine or WSL2, not a native Windows
shell. Keep several GB of disk space available for the toolchain and output.

Install the host dependencies (this does not select GCC as the compiler):

```bash
sudo apt-get update
sudo apt-get install -y \
  git curl ca-certificates xz-utils tar gzip make python3 \
  libssl-dev libelf-dev cpio bc bison flex
```

Fetch the correct branch and run the two checked build scripts:

```bash
git clone --single-branch --branch arena/2197b6cf-kernel \
  https://github.com/phen-ix/kernel.git kernel-no-kcal
cd kernel-no-kcal

./scripts/fetch-llvm.sh
./scripts/build-miatoll.sh
```

If the checkout already exists:

```bash
cd kernel-no-kcal
git fetch origin arena/2197b6cf-kernel
git switch arena/2197b6cf-kernel
git pull --ff-only origin arena/2197b6cf-kernel
./scripts/fetch-llvm.sh
./scripts/build-miatoll.sh
```

The scripts put the pinned toolchain in `.work/`, build out-of-tree in
`out/miatoll/`, and collect downloads in:

```text
out/miatoll/artifacts/
out/miatoll/logs/
```

For a completely fresh kernel output without deleting the toolchain:

```bash
OUT_DIR="$PWD/out/miatoll-fresh" ./scripts/build-miatoll.sh
```

That command's artifacts and logs are under `out/miatoll-fresh/` instead.
Do not work around a compilation error by disabling a required feature.

## Option C — AnyKernel3 flashable ZIP

A recovery-flashable AnyKernel3 ZIP is produced from the same verified
artifacts. GitHub Actions uploads it as a separate artifact:

```text
DOOM-Kernel-miatoll-<full-commit-sha>
```

To build it locally after `scripts/build-miatoll.sh`:

```bash
./scripts/package-anykernel.sh
```

Output:

```text
out/miatoll/anykernel/DOOM-Kernel-miatoll-<short-sha>.zip
out/miatoll/anykernel/DOOM-Kernel-miatoll-<short-sha>.zip.sha256
```

The ZIP is configured for the unified **miatoll** target
(`curtana`, `joyeuse`, `excalibur`, `gram`) using the device tree's boot
layout: **A-only**, boot header **v2**, page size **4096**, **DTB in boot**,
and a **separate DTBO partition**. It replaces the kernel and DTB inside the
existing boot image, preserves the current boot ramdisk, and flashes a
matching `dtbo.img` built from the four device overlays. It does not flash a
ROM, recovery, vbmeta, or modem, and it does not install the optional
`tcp_bic.ko` / `tcp_htcp.ko` modules. NoMount kernel support is built in
(protocol 22), but the matching NoMount v2.2.0 userspace metamodule must be
installed separately through the KernelSU manager after boot.

Verify the package before flashing:

```bash
cd out/miatoll/anykernel
sha256sum -c DOOM-Kernel-miatoll-<short-sha>.zip.sha256
```

Flash from a custom recovery (TWRP/OrangeFox/Lineage Recovery). Keep a
known-good `boot.img` and `dtbo.img` backup first. This is a testing build,
not a runtime-certified release; compilation and ZIP structure checks do not
prove that the device will boot or that Android 17 userspace will work
correctly.

## What the raw download contains

```text
artifacts/
  Image                         raw ARM64 kernel image
  cust-atoll-ab.dtb              device-tree blob
  *-atoll-ab-idp-overlay.dtbo    curtana/excalibur/gram/joyeuse overlays
  *.ko                          configured loadable modules, if any
  miatoll.config                final resolved kernel configuration
  SHA256SUMS                    relative-path checksums for verification
  BUILD_INFO.txt                source/toolchain/feature identification
logs/
  toolchain.txt
  defconfig.log
  config-validation.log
  build.log
  tool-audit.log
```

**The raw artifact above is not a flashable AnyKernel ZIP, `boot.img`, or
recovery ZIP.** Do not flash the raw `Image` directly to a boot partition.
Use the AnyKernel3 package from Option C for recovery flashing, or your
known-good ROM/kernel packaging flow. Keep a recovery image and boot/dtbo
backups before testing.

A successful compile/config/tool audit does not prove boot stability,
Android 17 compatibility, root behavior, scheduler behavior, or BBRv3 runtime
performance. Those still require on-device validation.

#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0
# Fetch the exact AOSP Clang 18 prebuilt used by the audited baseline.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEST="${TOOLCHAIN_ROOT:-$ROOT/.work/toolchain}"
LLVM_COMMIT="6a853dd45fe8f0d1821deb899c77a7b954b6306b"
BUILDTOOLS_COMMIT="f61cfbcb609173e1040753a2b9e8fbe8517343f9"
CLANG_DIR="$DEST/clang-r522817"
BUILDTOOLS_DIR="${BUILDTOOLS_ROOT:-$ROOT/.work/build-tools}"

mkdir -p "$DEST" "$(dirname "$BUILDTOOLS_DIR")"

if [[ ! -x "$CLANG_DIR/bin/clang-18" ]]; then
  tmp="$(mktemp -d "$DEST/llvm-src.XXXXXX")"
  trap 'rm -rf "$tmp"' EXIT
  git init -q "$tmp"
  git -C "$tmp" remote add origin \
    https://github.com/msft-mirror-aosp/platform.prebuilts.clang.host.linux-x86.git
  git -C "$tmp" fetch -q --depth=1 --filter=blob:none origin "$LLVM_COMMIT"
  git -C "$tmp" sparse-checkout init --cone
  git -C "$tmp" sparse-checkout set clang-r522817
  git -C "$tmp" checkout -q FETCH_HEAD
  mv "$tmp/clang-r522817" "$CLANG_DIR"
  rm -rf "$tmp"
  trap - EXIT
fi

if [[ ! -x "$BUILDTOOLS_DIR/path/linux-x86/bc" ]]; then
  tmp="$(mktemp -d)"
  trap 'rm -rf "$tmp"' EXIT
  curl -fL --retry 3 \
    "https://codeload.github.com/LineageOS/android_prebuilts_build-tools/tar.gz/$BUILDTOOLS_COMMIT" \
    -o "$tmp/build-tools.tar.gz"
  mkdir -p "$BUILDTOOLS_DIR"
  tar -xzf "$tmp/build-tools.tar.gz" -C "$BUILDTOOLS_DIR" --strip-components=1
  rm -rf "$tmp"
  trap - EXIT
fi

export PATH="$CLANG_DIR/bin:$BUILDTOOLS_DIR/path/linux-x86:$BUILDTOOLS_DIR/linux-x86/bin:$PATH"
export LD_LIBRARY_PATH="$BUILDTOOLS_DIR/linux-x86/lib64${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

expect_hash() {
  local file="$1" hash="$2"
  echo "$hash  $file" | sha256sum -c -
}
expect_hash "$CLANG_DIR/bin/clang-18" \
  5ab8ad3d455ff09c8eb8f4c7dbc70fb5d5396c7910342a18fd68e848b12f749d
expect_hash "$CLANG_DIR/bin/lld" \
  8cc2d6d52e3b53581616ac735549de4afe8e3194ee65ae543cd2230833c0ba91
expect_hash "$CLANG_DIR/bin/llvm-ar" \
  ddbf59740630a74ad2ff30c7c59189a02e01f4f8da138fd78be0afc959348e93

clang --version | head -1 | grep -F 'clang version 18.0.1'
ld.lld --version | grep -F 'LLD 18.0.1'
flex --version
bison --version | head -1
bc --version | head -1
echo "LLVM toolchain verified: $CLANG_DIR"

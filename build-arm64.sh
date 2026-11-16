#!/usr/bin/env bash
set -euo pipefail

# The ARM64 Microsoft.Windows.WDK and matching Microsoft.Windows.SDK.CPP
# NuGet packages are extracted beneath this directory.
kit=${NX_WDK_ROOT:-"$HOME/.cache/wdk-28000.2526"}
wdk="$kit/wdk/c"
sdk="$kit/sdk-base/c"
version=10.0.28000.0
kmdf=1.31
cc=${NX_CLANG_CL:-clang-cl-21}
link=${NX_LLD_LINK:-lld-link-21}

cd -- "$(dirname -- "$0")"
mkdir -p build-arm64
wdf_headers="$wdk/Include/wdf/kmdf/$kmdf"
while IFS= read -r header; do
    if [[ ! -f "$wdf_headers/$header" &&
          -f "$wdf_headers/${header,,}" ]]; then
        ln -sfn "$wdf_headers/${header,,}" "build-arm64/$header"
    fi
done < <(sed -n 's/^[[:space:]]*#include "\([^"]*\.h\)".*/\1/p' "$wdf_headers/wdf.h")
objects=()
for source in driver/{driver,controller,transport,geni,hid}.c protocol/{syna_tcm,syna_hid}.c; do
    object="build-arm64/$(basename "${source%.c}").obj"
    "$cc" --target=aarch64-pc-windows-msvc /nologo /c /kernel /O2 /GS /W3 /WX \
        /D_ARM64_ /DWINVER=0x0A00 /D_WIN32_WINNT=0x0A00 /DNTDDI_VERSION=0x0A000008 \
        "/imsvc$wdk/Include/$version/km" "/imsvc$wdk/Include/$version/km/crt" \
        "/imsvc$wdk/Include/$version/shared" \
        "/imsvc$wdf_headers" /imsvcbuild-arm64 "/imsvc$sdk/Include/$version/shared" \
        "/imsvc$sdk/Include/$version/ucrt" "/imsvc$sdk/Include/$version/um" \
        "/Fo$object" "$source"
    objects+=("$object")
done

km_lib="$wdk/Lib/$version/km/ARM64"
wdf_lib="$wdk/Lib/wdf/kmdf/ARM64/$kmdf"
"$link" /nologo /machine:arm64 /driver /subsystem:native /nodefaultlib /release \
    /entry:FxDriverEntry /out:build-arm64/NxSynaTouch.sys "${objects[@]}" \
    "$wdf_lib/wdfdriverentry.lib" "$wdf_lib/wdfldr.lib" "$km_lib/vhfkm.lib" \
    "$km_lib/ntoskrnl.lib" "$km_lib/hal.lib" "$km_lib/bufferoverflowfastfailk.lib" \
    "$km_lib/libcntpr.lib"
cp NxSynaTouch.inf build-arm64/

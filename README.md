# NX809J touch driver

An experimental Windows ARM64 touchscreen driver for the **RedMagic 11 Pro (NX809J / `qwjujube`)**, targeting the Synaptics S3910V.

This is the touch part of my Windows-on-NX809J port. Windows PE now boots on the phone. Touch does **not** work yet, so this is a place to develop the driver, compare notes, and keep the hardware findings together.

## Where things stand

The ARM64 driver builds and the protocol tests pass. On the phone, WinPE accepts the package, but the latest captured touch-device error is **Code 52 (`CM_PROB_UNSIGNED_DRIVER`, status `0xC0000428`)**. The PE boot configuration was missing test-signing; a corrected BCD has been prepared. A successful hardware start after that correction has not been confirmed.

The current version adds startup checkpoints so the next failure should be easier to locate. Successful INF installation alone does not show that the driver started or that the controller answered.

Still unverified: controller initialization, actual touch input, orientation, multitouch, cold boots, and suspend/resume.

## What is here

- `driver/`: KMDF driver, GENI SPI transport, controller setup, and Windows Virtual HID Framework integration.
- `protocol/`: TouchComm framing, controller information parsing, the recovered report layout, and HID report helpers.
- `tests/`: host-side protocol and report tests. These do not test the phone's hardware.
- [Hardware notes](docs/hardware.md): board assumptions and ACPI resources.
- [Testing notes](docs/testing.md): signing, deployment, and startup checkpoints.

This is a source repository. It does not contain Windows images, vendor firmware, signing certificates or keys, or a complete UEFI port.

## Build

The current build uses Linux, Clang/LLD 21, and Microsoft's ARM64 WDK and SDK **10.0.28000.0**. KMDF is targeted at **1.31**.

Extract the matching official packages into this layout:

```text
$NX_WDK_ROOT/
  wdk/c/Include/...
  wdk/c/Lib/...
  sdk-base/c/Include/...
```

Then run:

```sh
export NX_WDK_ROOT=/path/to/extracted-kits
bash build-arm64.sh
```

Output: `build-arm64/NxSynaTouch.sys` and the INF. This step does not generate a catalog or sign the package. See the [testing notes](docs/testing.md) before loading it.

You can override the compiler and linker with `NX_CLANG_CL` and `NX_LLD_LINK`. The script defaults to `~/.cache/wdk-28000.2526` when `NX_WDK_ROOT` is unset.

## Protocol tests

These need a C compiler and Make, not the Windows SDK:

```sh
make -C tests test
```

## Before putting it on a phone

Use an NX809J with a recovery procedure you have already tested. Pair the driver with firmware exposing the resources in the hardware notes. The INF supports `ACPI\ZTE0001` and the GPIO-polling variant `ACPI\ZTE0002`.

The boot chain must leave the touch power rails, SPI clocks and GENI firmware usable. This driver cannot initialize those missing dependencies by itself. Do not load it on another model just because the touchscreen or SoC looks similar.

## Help wanted

Useful reports include the firmware revision, Windows build, ACPI device ID, device problem code/status, and the `NX*` startup values. If you change the transport, include a protocol test where practical and say what was actually tested on hardware.

GENI SPI, Synaptics TouchComm, Windows VHF, and board power sequencing are the areas where another pair of eyes would help most.

## References and license

Board research uses [Coding-BR's NX809J kernel tree](https://github.com/Coding-BR/android_kernel_nubia_sm8850_qwjujube), including the vendor touchscreen code and Qualcomm GENI SPI implementation. The original bring-up notes refer to revision `efee6f411184886b0fbdaf63fc9db36b3ed96036`.

Microsoft documents [Virtual HID Framework](https://learn.microsoft.com/en-us/windows-hardware/drivers/hid/virtual-hid-framework--vhf-), [KMDF](https://learn.microsoft.com/en-us/windows-hardware/drivers/wdf/using-the-framework-to-develop-a-driver), and [test-signed driver loading](https://learn.microsoft.com/en-us/windows-hardware/drivers/install/the-testsigning-boot-configuration-option).

The driver and protocol sources retain their **GPL-2.0-only** SPDX notices. See [LICENSE](LICENSE). This is an independent port, with no affiliation to Nubia, Synaptics, Qualcomm, or Microsoft.

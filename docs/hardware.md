# Hardware notes

These are the contracts implemented in the source, not a claim that every register has been validated on the phone.

| Item | Current assumption |
| --- | --- |
| Phone | NX809J, `qwjujube` |
| Touch controller | Synaptics S3910V |
| Serial engine | QUPv3 wrapper 4, SE19; SPI mode 0, CS0, MSB first, 8-bit words |
| SE register range | Base `0x01A80000`, minimum length `0x4000` |
| Wrapper register range | Base `0x01AC0000`, minimum length `0x2000` |
| Reset | GPIO66, polling page `0x0F142000` |
| IRQ | Active-low GPIO158, polling page `0x0F19E000` |
| Pin page size | `0x1000` |
| Power | Handoff must preserve the 1.8 V L4B and 3.2 V L14B rails |
| HID bounds | X `0..1215`, Y `0..2687`; orientation needs testing |

## Firmware interface

`ACPI\ZTE0001` uses the GPIO provider/reset-connection path. `ACPI\ZTE0002` sets `PollingMode=1` through the INF. It expects the reset and IRQ pages as assigned memory resources, alongside the SPI engine resources. It checks IRQ level using a passive WDF timer, without a GPIO controller driver.

The MMIO backend checks for GENI SPI firmware and an enabled FIFO interface. It does not load GENI microcode or program the PMIC. An SPB backend is also present, but has not been validated on this board.

`ProgramTlmm`, `GeniClockSelect`, and `GeniClockDivider` are optional per-device registry settings. Defaults are false, 0, and 2. Clocks and pin ownership must agree with firmware; these are not settings to change by trial and error.

Firmware-side resource declarations and handoff code are outside this standalone repository. TouchComm v1 and the recovered report configuration are implemented here. Optional CRC/vendor extensions and physical contact geometry remain unverified.

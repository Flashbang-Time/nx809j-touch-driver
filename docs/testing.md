# Testing on the phone

## Signing comes first

The build script outputs an unsigned driver. Generate a catalog with the Windows WDK's Inf2Cat, then test-sign the driver and catalog with your own code-signing certificate. Do not publish the private key.

The active Windows loader entry must have `TESTSIGNING` enabled before boot. Trust the matching public certificate in the test environment's Local Machine Root and Trusted Publishers stores. Adding a certificate alone does not enable test mode. Secure Boot policy can prevent enabling test mode.

Keep changes on the phone's PE media or development installation; do not accidentally edit your PC's default BCD. Keep a known-working PE image and boot configuration. RAM-booted PE settings do not necessarily persist across reboots.

The existing NX809J deployment setup imports its package certificate at startup. That helper and its certificate are not shipped here: a fresh build needs its own matching signing/deployment setup.

## Read the actual failure

- Code 52 (`CM_PROB_UNSIGNED_DRIVER`): check the signature, certificate and boot policy first.
- Code 10 (`CM_PROB_FAILED_START`): one component in the device stack failed startup. Read the underlying problem status and checkpoints.
- A successful `drvload` result means the package was accepted. It does not prove working input.

## Startup values

The diagnostic build writes best-effort DWORDs below:

```text
HKLM\SYSTEM\CurrentControlSet\Services\NxSynaTouch\Parameters
```

`NXDiagnosticVersion` is 3. `NXStage` and `NXStatus` track startup and NTSTATUS. Registry output displays DWORDs in hexadecimal.

| NXStage (hex) | Meaning |
| --- | --- |
| `1` | Device added and settings read |
| `2` | WDF object creation returned |
| `3` | Resource validation |
| `4` | Resources mapped |
| `5` | VHF creation returned |
| `A` | Entering GENI initialization |
| `B` | GENI initialization returned |
| `14` | Reset sequence |
| `1E` | Identification |
| `1F` | Identification parsed |
| `20` | Application information |
| `28` | Report configuration |
| `32` | Enabling reports |
| `64` | Controller start completed |

Other values include `NXPollingMode`, `NXSeLength`, `NXWrapperLength`, `NXGeniProtocol`, `NXGeniIfDisable`, FIFO depths/width, `NXStartupReadStatus`, `NXStartupMessage`, `NXControllerMode`, `NXLastCommand`, and `NXLastWriteStatus`.

`NXFirstTouchBytes` records the first touch report received by the ready controller path. `NXFirstHidStatus` records the first parsed frame's VHF submission result. Neither proves correct on-screen input. Missing checkpoints can mean that code was not reached or that the best-effort registry write failed.

These are startup/first-report diagnostics, not a continuous trace. A stage may contain a success/pending value while its operation is still in progress. Later starts or resume attempts may leave earlier first-report values in place.

For a report, include the Windows build, firmware version, ACPI ID, device problem code/status, and available `NX*` values. Say whether the panel was touched, whether a HID touchscreen appeared, and whether any contact response occurred.

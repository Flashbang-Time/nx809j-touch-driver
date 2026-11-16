/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef NX809J_TOUCH_DRIVER_H
#define NX809J_TOUCH_DRIVER_H

#include <ntddk.h>
#include <wdf.h>

#define RESHUB_USE_HELPER_ROUTINES
#include <reshub.h>
#include <spb.h>
#include <gpio.h>
#include <vhf.h>

#include "../protocol/syna_tcm.h"
#include "../protocol/syna_hid.h"

#define NX_TOUCH_POOL_TAG 'hTxN'
#define NX_TOUCH_COMMAND_TIMEOUT_MS 2000u
#define NX_TOUCH_IO_TIMEOUT_MS 250u

#define NX809J_SE19_PHYSICAL_BASE 0x01A80000ull
#define NX809J_SE19_MINIMUM_SIZE  0x00004000u
#define NX809J_QUP4_PHYSICAL_BASE 0x01AC0000ull
#define NX809J_QUP4_MINIMUM_SIZE  0x00002000u
#define NX809J_RESET_PIN_BASE     0x0F142000ull
#define NX809J_IRQ_PIN_BASE       0x0F19E000ull
#define NX809J_PIN_PAGE_SIZE      0x1000u
#define NX_TOUCH_POLL_MS         8u

typedef enum NxBusBackend {
    NxBusBackendNone = 0,
    NxBusBackendGeniMmio,
    NxBusBackendSpb
} NxBusBackend;

typedef struct NxDeviceContext {
    WDFDEVICE Device;
    WDFIOTARGET SpbTarget;
    WDFIOTARGET ResetTarget;
    WDFINTERRUPT Interrupt;
    WDFTIMER PollTimer;
    WDFWAITLOCK IoLock;

    LARGE_INTEGER SpbConnectionId;
    LARGE_INTEGER ResetConnectionId;
    BOOLEAN HasSpbConnection;
    BOOLEAN HasResetConnection;
    BOOLEAN SpbTargetOpen;
    BOOLEAN ResetTargetOpen;
    BOOLEAN Powered;
    BOOLEAN ControllerReady;
    BOOLEAN DiagFirstReport;
    BOOLEAN DiagFirstSubmit;
    BOOLEAN PollingMode;
    PUCHAR ResetPinBase;
    PUCHAR IrqPinBase;

    NxBusBackend BusBackend;
    PHYSICAL_ADDRESS SePhysicalAddress;
    ULONG SeLength;
    PUCHAR SeBase;
    PHYSICAL_ADDRESS WrapperPhysicalAddress;
    ULONG WrapperLength;
    PUCHAR WrapperBase;
    ULONG GeniTxFifoDepth;
    ULONG GeniRxFifoDepth;
    ULONG GeniFifoWidthBits;
    ULONG GeniClockSelect;
    ULONG GeniClockDivider;
    BOOLEAN ProgramTlmm;
    BOOLEAN TlmmStateSaved;
    ULONG TlmmSavedControl[4];
    ULONG TlmmSavedIo[4];
    BOOLEAN GeniReady;

    VHFHANDLE VhfHandle;

    uint8_t TxWire[SYNA_TCM_MAX_PAYLOAD_SIZE + 3u];
    uint8_t RxWire[SYNA_TCM_MAX_PAYLOAD_SIZE + SYNA_TCM_MESSAGE_HEADER_SIZE + 1u];
    uint8_t IoScratch[SYNA_TCM_MAX_PAYLOAD_SIZE + 3u];
    uint8_t ReportConfig[SYNA_TCM_MAX_CONFIG_SIZE];
    size_t ReportConfigLength;

    SynaIdentification Identification;
    SynaApplicationInfo ApplicationInfo;
    NxHidTouchReport LastTouchReport;
} NxDeviceContext;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(NxDeviceContext, NxGetDeviceContext)

void NxDiagValue(_In_ NxDeviceContext *Context, _In_ PCWSTR Name, _In_ ULONG Value);
void NxDiagStage(_In_ NxDeviceContext *Context, _In_ ULONG Stage, _In_ NTSTATUS Status);

DRIVER_INITIALIZE DriverEntry;
EVT_WDF_DRIVER_DEVICE_ADD NxEvtDeviceAdd;
EVT_WDF_DEVICE_PREPARE_HARDWARE NxEvtDevicePrepareHardware;
EVT_WDF_DEVICE_RELEASE_HARDWARE NxEvtDeviceReleaseHardware;
EVT_WDF_DEVICE_D0_ENTRY NxEvtDeviceD0Entry;
EVT_WDF_DEVICE_D0_EXIT NxEvtDeviceD0Exit;
EVT_WDF_INTERRUPT_ISR NxEvtInterruptIsr;
EVT_WDF_TIMER NxEvtPollTimer;

EVT_VHF_ASYNC_OPERATION NxEvtVhfGetFeature;

NTSTATUS NxOpenResourceTarget(_In_ WDFIOTARGET Target,
                              _In_ LARGE_INTEGER ConnectionId);
NTSTATUS NxBusWrite(_In_ NxDeviceContext *Context,
                    _In_reads_bytes_(Length) const uint8_t *Buffer,
                    _In_ size_t Length);
NTSTATUS NxBusRead(_In_ NxDeviceContext *Context,
                   _Out_writes_bytes_(Length) uint8_t *Buffer,
                   _In_ size_t Length);
NTSTATUS NxSetResetPin(_In_ NxDeviceContext *Context, _In_ BOOLEAN High);
void NxConfigurePollingPins(_In_ NxDeviceContext *Context);
BOOLEAN NxPollIrqAsserted(_In_ NxDeviceContext *Context);
NTSTATUS NxReadWireMessage(_In_ NxDeviceContext *Context,
                           _Out_ SynaTcmMessage *Message);
void NxDelayMilliseconds(_In_ ULONG Milliseconds);

NTSTATUS NxGeniInitialize(_Inout_ NxDeviceContext *Context);
void NxGeniShutdown(_Inout_ NxDeviceContext *Context);
NTSTATUS NxGeniWrite(_In_ NxDeviceContext *Context,
                     _In_reads_bytes_(Length) const uint8_t *Buffer,
                     _In_ size_t Length);
NTSTATUS NxGeniRead(_In_ NxDeviceContext *Context,
                    _Out_writes_bytes_(Length) uint8_t *Buffer,
                    _In_ size_t Length);

NTSTATUS NxControllerStart(_In_ NxDeviceContext *Context);
void NxControllerStop(_In_ NxDeviceContext *Context);
NTSTATUS NxIssueCommand(_In_ NxDeviceContext *Context,
                        _In_ uint8_t Command,
                        _In_reads_bytes_opt_(PayloadLength) const uint8_t *Payload,
                        _In_ uint16_t PayloadLength,
                        _Out_writes_bytes_to_opt_(ResponseCapacity, *ResponseLength)
                            uint8_t *Response,
                        _In_ size_t ResponseCapacity,
                        _Out_opt_ size_t *ResponseLength);
void NxProcessMessage(_In_ NxDeviceContext *Context,
                      _In_ const SynaTcmMessage *Message);

NTSTATUS NxVhfCreate(_In_ NxDeviceContext *Context);
void NxVhfDelete(_In_ NxDeviceContext *Context);
NTSTATUS NxVhfSubmitFrame(_In_ NxDeviceContext *Context,
                          _In_ const SynaTouchFrame *Frame);
NTSTATUS NxVhfSubmitRelease(_In_ NxDeviceContext *Context);

#endif

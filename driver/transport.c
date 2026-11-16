/* SPDX-License-Identifier: GPL-2.0-only */
#include "nx_touch.h"

#define NX_GPIO_IO                 4u
#define NX_GPIO_IO_IN              1u
#define NX_GPIO_IO_OUT             2u
#define NX_GPIO_CONFIG_MASK        0x3FFu
#define NX_GPIO_CONFIG_8MA_PULLUP  0x0C3u
#define NX_GPIO_CONFIG_OUTPUT     0x200u

static void
NxSetPollingReset(_In_ NxDeviceContext *Context, _In_ BOOLEAN High)
{
    volatile ULONG *io = (volatile ULONG *)(Context->ResetPinBase + NX_GPIO_IO);
    ULONG value = READ_REGISTER_ULONG(io);
    WRITE_REGISTER_ULONG(io, High ? value | NX_GPIO_IO_OUT : value & ~NX_GPIO_IO_OUT);
}

void
NxConfigurePollingPins(_In_ NxDeviceContext *Context)
{
    volatile ULONG *reset = (volatile ULONG *)Context->ResetPinBase;
    volatile ULONG *irq = (volatile ULONG *)Context->IrqPinBase;

    NxSetPollingReset(Context, TRUE);
    WRITE_REGISTER_ULONG(reset, (READ_REGISTER_ULONG(reset) & ~NX_GPIO_CONFIG_MASK) |
                         NX_GPIO_CONFIG_8MA_PULLUP | NX_GPIO_CONFIG_OUTPUT);
    WRITE_REGISTER_ULONG(irq, (READ_REGISTER_ULONG(irq) & ~NX_GPIO_CONFIG_MASK) |
                         NX_GPIO_CONFIG_8MA_PULLUP);
}

BOOLEAN
NxPollIrqAsserted(_In_ NxDeviceContext *Context)
{
    return (READ_REGISTER_ULONG((volatile ULONG *)(Context->IrqPinBase + NX_GPIO_IO)) &
            NX_GPIO_IO_IN) == 0u;
}

void
NxDelayMilliseconds(_In_ ULONG Milliseconds)
{
    LARGE_INTEGER interval;
    interval.QuadPart = -((LONGLONG)Milliseconds * 10 * 1000);
    (void)KeDelayExecutionThread(KernelMode, FALSE, &interval);
}

NTSTATUS
NxOpenResourceTarget(_In_ WDFIOTARGET Target, _In_ LARGE_INTEGER ConnectionId)
{
    DECLARE_UNICODE_STRING_SIZE(path, RESOURCE_HUB_PATH_SIZE);
    WDF_IO_TARGET_OPEN_PARAMS open_params;
    NTSTATUS status;

    status = RESOURCE_HUB_CREATE_PATH_FROM_ID(&path,
                                               ConnectionId.LowPart,
                                               ConnectionId.HighPart);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    WDF_IO_TARGET_OPEN_PARAMS_INIT_OPEN_BY_NAME(&open_params, &path,
                                                GENERIC_READ | GENERIC_WRITE);
    open_params.ShareAccess = 0;
    open_params.CreateDisposition = FILE_OPEN;
    open_params.FileAttributes = FILE_ATTRIBUTE_NORMAL;
    return WdfIoTargetOpen(Target, &open_params);
}

static void
NxInitTimeout(_Out_ WDF_REQUEST_SEND_OPTIONS *Options)
{
    WDF_REQUEST_SEND_OPTIONS_INIT(Options, WDF_REQUEST_SEND_OPTION_TIMEOUT);
    WDF_REQUEST_SEND_OPTIONS_SET_TIMEOUT(Options,
                                         WDF_REL_TIMEOUT_IN_MS(NX_TOUCH_IO_TIMEOUT_MS));
}

static NTSTATUS
NxSpbWrite(_In_ NxDeviceContext *Context,
           _In_reads_bytes_(Length) const uint8_t *Buffer,
           _In_ size_t Length)
{
    WDF_MEMORY_DESCRIPTOR memory;
    WDF_REQUEST_SEND_OPTIONS options;
    ULONG_PTR bytes_transferred = 0;
    NTSTATUS status;

    if (Length == 0u || Length > MAXULONG) {
        return STATUS_INVALID_PARAMETER;
    }
    WDF_MEMORY_DESCRIPTOR_INIT_BUFFER(&memory, (PVOID)Buffer, (ULONG)Length);
    NxInitTimeout(&options);
    status = WdfIoTargetSendWriteSynchronously(Context->SpbTarget,
                                                NULL, &memory, NULL,
                                                &options, &bytes_transferred);
    if (NT_SUCCESS(status) && bytes_transferred != Length) {
        status = STATUS_DEVICE_DATA_ERROR;
    }
    return status;
}

static NTSTATUS
NxSpbRead(_In_ NxDeviceContext *Context,
          _Out_writes_bytes_(Length) uint8_t *Buffer,
          _In_ size_t Length)
{
    WDF_MEMORY_DESCRIPTOR memory;
    WDF_REQUEST_SEND_OPTIONS options;
    ULONG_PTR bytes_transferred = 0;
    NTSTATUS status;

    if (Length == 0u || Length > MAXULONG) {
        return STATUS_INVALID_PARAMETER;
    }
    WDF_MEMORY_DESCRIPTOR_INIT_BUFFER(&memory, Buffer, (ULONG)Length);
    NxInitTimeout(&options);
    status = WdfIoTargetSendReadSynchronously(Context->SpbTarget,
                                               NULL, &memory, NULL,
                                               &options, &bytes_transferred);
    if (NT_SUCCESS(status) && bytes_transferred != Length) {
        status = STATUS_DEVICE_DATA_ERROR;
    }
    return status;
}

NTSTATUS
NxBusWrite(_In_ NxDeviceContext *Context,
           _In_reads_bytes_(Length) const uint8_t *Buffer,
           _In_ size_t Length)
{
    if (Context->BusBackend == NxBusBackendGeniMmio) {
        return NxGeniWrite(Context, Buffer, Length);
    }
    if (Context->BusBackend == NxBusBackendSpb &&
        Context->SpbTargetOpen) {
        return NxSpbWrite(Context, Buffer, Length);
    }
    return STATUS_DEVICE_NOT_READY;
}

NTSTATUS
NxBusRead(_In_ NxDeviceContext *Context,
          _Out_writes_bytes_(Length) uint8_t *Buffer,
          _In_ size_t Length)
{
    if (Context->BusBackend == NxBusBackendGeniMmio) {
        return NxGeniRead(Context, Buffer, Length);
    }
    if (Context->BusBackend == NxBusBackendSpb &&
        Context->SpbTargetOpen) {
        return NxSpbRead(Context, Buffer, Length);
    }
    return STATUS_DEVICE_NOT_READY;
}

NTSTATUS
NxSetResetPin(_In_ NxDeviceContext *Context, _In_ BOOLEAN High)
{
    UCHAR value = High ? 1u : 0u;
    WDF_MEMORY_DESCRIPTOR input;
    WDF_REQUEST_SEND_OPTIONS options;

    if (Context->PollingMode) {
        NxSetPollingReset(Context, High);
        return STATUS_SUCCESS;
    }

    WDF_MEMORY_DESCRIPTOR_INIT_BUFFER(&input, &value, sizeof(value));
    NxInitTimeout(&options);
    return WdfIoTargetSendIoctlSynchronously(Context->ResetTarget,
                                              NULL,
                                              IOCTL_GPIO_WRITE_PINS,
                                              &input,
                                              NULL,
                                              &options,
                                              NULL);
}

NTSTATUS
NxReadWireMessage(_In_ NxDeviceContext *Context,
                  _Out_ SynaTcmMessage *Message)
{
    uint16_t payload_length;
    size_t continuation_length;
    NTSTATUS status;
    int parse_result;

    status = NxBusRead(Context, Context->RxWire,
                       SYNA_TCM_MESSAGE_HEADER_SIZE);
    if (!NT_SUCCESS(status)) {
        return status;
    }
    if (Context->RxWire[0] != SYNA_TCM_MESSAGE_MARKER) {
        return STATUS_DEVICE_PROTOCOL_ERROR;
    }

    payload_length = (uint16_t)((uint16_t)Context->RxWire[2] |
                                ((uint16_t)Context->RxWire[3] << 8));
    if (payload_length > SYNA_TCM_MAX_PAYLOAD_SIZE) {
        return STATUS_BUFFER_OVERFLOW;
    }

    if (payload_length == 0u) {
        Context->RxWire[SYNA_TCM_MESSAGE_HEADER_SIZE] = SYNA_TCM_MESSAGE_PADDING;
    } else {
        continuation_length = (size_t)payload_length + 3u;
        status = NxBusRead(Context, Context->IoScratch, continuation_length);
        if (!NT_SUCCESS(status)) {
            return status;
        }
        if (Context->IoScratch[0] != SYNA_TCM_MESSAGE_MARKER ||
            Context->IoScratch[1] != SYNA_STATUS_CONTINUED_READ) {
            return STATUS_DEVICE_PROTOCOL_ERROR;
        }
        RtlCopyMemory(&Context->RxWire[SYNA_TCM_MESSAGE_HEADER_SIZE],
                      &Context->IoScratch[2], (size_t)payload_length + 1u);
    }

    parse_result = SynaTcmDecodeMessage(
        Context->RxWire,
        SYNA_TCM_MESSAGE_HEADER_SIZE + (size_t)payload_length + 1u,
        Message);
    return parse_result == SYNA_RESULT_OK
         ? STATUS_SUCCESS : STATUS_DEVICE_PROTOCOL_ERROR;
}

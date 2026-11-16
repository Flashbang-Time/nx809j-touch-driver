/* SPDX-License-Identifier: GPL-2.0-only */
#include "nx_touch.h"

static NTSTATUS
NxWriteCommand(_In_ NxDeviceContext *Context, _In_ size_t Length)
{
    size_t limit = Context->Identification.max_write_size;
    size_t written;
    NTSTATUS status;

    if (limit == 0u || Length <= limit) {
        return NxBusWrite(Context, Context->TxWire, Length);
    }
    if (limit < 3u) {
        return STATUS_DEVICE_PROTOCOL_ERROR;
    }

    status = NxBusWrite(Context, Context->TxWire, limit);
    written = limit;
    while (NT_SUCCESS(status) && written < Length) {
        size_t count = Length - written;
        if (count >= limit) {
            count = limit - 1u;
        }
        Context->IoScratch[0] = SYNA_CMD_CONTINUE_WRITE;
        RtlCopyMemory(&Context->IoScratch[1], &Context->TxWire[written], count);
        NxDelayMilliseconds(1u);
        status = NxBusWrite(Context, Context->IoScratch, count + 1u);
        written += count;
    }
    return status;
}

static NTSTATUS
NxCopyResponse(_In_ const SynaTcmMessage *Message,
               _Out_writes_bytes_to_opt_(ResponseCapacity, *ResponseLength)
                   uint8_t *Response,
               _In_ size_t ResponseCapacity,
               _Out_opt_ size_t *ResponseLength)
{
    if (ResponseLength != NULL) {
        *ResponseLength = Message->payload_length;
    }
    if (Message->payload_length == 0u || Response == NULL) {
        return STATUS_SUCCESS;
    }
    if (ResponseCapacity < Message->payload_length) {
        return STATUS_BUFFER_TOO_SMALL;
    }
    RtlCopyMemory(Response, Message->payload, Message->payload_length);
    return STATUS_SUCCESS;
}

NTSTATUS
NxIssueCommand(_In_ NxDeviceContext *Context,
               _In_ uint8_t Command,
               _In_reads_bytes_opt_(PayloadLength) const uint8_t *Payload,
               _In_ uint16_t PayloadLength,
               _Out_writes_bytes_to_opt_(ResponseCapacity, *ResponseLength)
                   uint8_t *Response,
               _In_ size_t ResponseCapacity,
               _Out_opt_ size_t *ResponseLength)
{
    size_t wire_length = 0;
    ULONGLONG started;
    NTSTATUS status;
    int result;

    if (ResponseLength != NULL) {
        *ResponseLength = 0u;
    }
    WdfWaitLockAcquire(Context->IoLock, NULL);
    result = SynaTcmBuildCommand(Command, Payload, PayloadLength,
                                 Context->TxWire, sizeof(Context->TxWire),
                                 &wire_length);
    if (result != SYNA_RESULT_OK) {
        WdfWaitLockRelease(Context->IoLock);
        return STATUS_INVALID_PARAMETER;
    }

    NxDiagValue(Context, L"NXLastCommand", Command);
    status = NxWriteCommand(Context, wire_length);
    NxDiagValue(Context, L"NXLastWriteStatus", (ULONG)status);
    if (!NT_SUCCESS(status)) {
        WdfWaitLockRelease(Context->IoLock);
        return status;
    }

    started = KeQueryInterruptTime();
    while (KeQueryInterruptTime() - started <
           NX_TOUCH_COMMAND_TIMEOUT_MS * 10000ull) {
        SynaTcmMessage message;

        NxDelayMilliseconds(10u);
        status = NxReadWireMessage(Context, &message);
        if (!NT_SUCCESS(status)) {
            continue;
        }

        if (message.code >= SYNA_REPORT_IDENTIFY &&
            message.code != SYNA_STATUS_INVALID) {
            NxProcessMessage(Context, &message);
            continue;
        }
        if (message.code == SYNA_STATUS_IDLE ||
            message.code == SYNA_STATUS_BUSY ||
            message.code == SYNA_STATUS_PREVIOUS_COMMAND_PENDING) {
            continue;
        }
        if (message.code != SYNA_STATUS_OK) {
            status = STATUS_IO_DEVICE_ERROR;
            break;
        }

        status = NxCopyResponse(&message, Response, ResponseCapacity,
                                ResponseLength);
        WdfWaitLockRelease(Context->IoLock);
        return status;
    }

    WdfWaitLockRelease(Context->IoLock);
    KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
        "NxSynaTouch: TCM command 0x%02x failed, status %#lx\n",
        Command, NT_SUCCESS(status) ? STATUS_IO_TIMEOUT : status));
    return NT_SUCCESS(status) ? STATUS_IO_TIMEOUT : status;
}

static NTSTATUS
NxGetControllerInformation(_In_ NxDeviceContext *Context)
{
    uint8_t response[256];
    size_t response_length;
    NTSTATUS status;
    int result;
    SynaTcmMessage startup;

    status = NxReadWireMessage(Context, &startup);
    NxDiagValue(Context, L"NXStartupReadStatus", (ULONG)status);
    if (NT_SUCCESS(status)) { NxDiagValue(Context, L"NXStartupMessage", startup.code); }
    if (NT_SUCCESS(status) && startup.code == SYNA_REPORT_IDENTIFY) {
        result = SynaTcmParseIdentification(startup.payload,
                                            startup.payload_length,
                                            &Context->Identification);
    } else {
        status = NxIssueCommand(Context, SYNA_CMD_IDENTIFY, NULL, 0,
                                response, sizeof(response), &response_length);
        if (!NT_SUCCESS(status)) {
            return status;
        }
        result = SynaTcmParseIdentification(response, response_length,
                                            &Context->Identification);
    }
    if (result != SYNA_RESULT_OK) {
        return STATUS_DEVICE_PROTOCOL_ERROR;
    }
    NxDiagValue(Context, L"NXControllerMode", Context->Identification.mode);
    NxDiagStage(Context, 31u, STATUS_SUCCESS);
    if (Context->Identification.mode != 0x01u) {
        return STATUS_DEVICE_NOT_READY;
    }

    NxDiagStage(Context, 32u, STATUS_SUCCESS);
    status = NxIssueCommand(Context, SYNA_CMD_GET_APPLICATION_INFO,
                            NULL, 0, response, sizeof(response),
                            &response_length);
    if (!NT_SUCCESS(status)) {
        return status;
    }
    result = SynaTcmParseApplicationInfo(response, response_length,
                                         &Context->ApplicationInfo);
    return result == SYNA_RESULT_OK
         ? STATUS_SUCCESS : STATUS_DEVICE_PROTOCOL_ERROR;
}

static NTSTATUS
NxConfigureTouchReports(_In_ NxDeviceContext *Context)
{
    uint8_t response[SYNA_TCM_MAX_CONFIG_SIZE];
    uint8_t stock[SYNA_TCM_MAX_CONFIG_SIZE];
    size_t response_length;
    size_t desired_length;
    NTSTATUS status;

    /* Confirm or install the exact format recovered from the stock module. */
    status = NxIssueCommand(Context, SYNA_CMD_GET_TOUCH_REPORT_CONFIG,
                            NULL, 0, response, sizeof(response),
                            &response_length);
    if (!NT_SUCCESS(status) ||
        response_length < SynaNx809jStockReportConfigLength ||
        RtlCompareMemory(response, SynaNx809jStockReportConfig,
                         SynaNx809jStockReportConfigLength) !=
            SynaNx809jStockReportConfigLength) {
        desired_length = Context->ApplicationInfo.max_touch_report_config_size;
        if (desired_length < SynaNx809jStockReportConfigLength ||
            desired_length > sizeof(stock)) {
            return STATUS_DEVICE_CONFIGURATION_ERROR;
        }
        RtlZeroMemory(stock, sizeof(stock));
        RtlCopyMemory(stock, SynaNx809jStockReportConfig,
                      SynaNx809jStockReportConfigLength);
        status = NxIssueCommand(Context, SYNA_CMD_SET_TOUCH_REPORT_CONFIG,
                                stock, (uint16_t)desired_length, NULL, 0, NULL);
        if (!NT_SUCCESS(status)) {
            return status;
        }
    }
    RtlCopyMemory(Context->ReportConfig, SynaNx809jStockReportConfig,
                  SynaNx809jStockReportConfigLength);
    Context->ReportConfigLength = SynaNx809jStockReportConfigLength;
    return STATUS_SUCCESS;
}

NTSTATUS
NxControllerStart(_In_ NxDeviceContext *Context)
{
    const uint8_t touch_report_code = SYNA_REPORT_TOUCH;
    NTSTATUS status;

    Context->ControllerReady = FALSE;
    Context->ReportConfigLength = 0u;
    RtlZeroMemory(&Context->Identification, sizeof(Context->Identification));
    NxDelayMilliseconds(200u);
    NxDiagStage(Context, 20u, STATUS_SUCCESS);
    status = NxSetResetPin(Context, FALSE);
    if (!NT_SUCCESS(status)) {
        return status;
    }
    NxDelayMilliseconds(20u);
    status = NxSetResetPin(Context, TRUE);
    if (!NT_SUCCESS(status)) {
        return status;
    }
    NxDelayMilliseconds(200u);

    NxDiagStage(Context, 30u, STATUS_SUCCESS);
    status = NxGetControllerInformation(Context);
    if (!NT_SUCCESS(status)) {
        return status;
    }
    NxDiagStage(Context, 40u, STATUS_SUCCESS);
    status = NxConfigureTouchReports(Context);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    NxDiagStage(Context, 50u, STATUS_SUCCESS);
    status = NxIssueCommand(Context, SYNA_CMD_ENABLE_REPORT,
                            &touch_report_code, sizeof(touch_report_code),
                            NULL, 0, NULL);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    /* Reporting is already enabled. A rejected rezero is nonfatal. */
    (void)NxIssueCommand(Context, SYNA_CMD_REZERO, NULL, 0, NULL, 0, NULL);
    Context->ControllerReady = TRUE;
    return STATUS_SUCCESS;
}

void
NxControllerStop(_In_ NxDeviceContext *Context)
{
    Context->ControllerReady = FALSE;
}

void
NxProcessMessage(_In_ NxDeviceContext *Context,
                 _In_ const SynaTcmMessage *Message)
{
    SynaTouchFrame frame;
    int result;

    if (Message->code == SYNA_REPORT_IDENTIFY &&
        Message->payload_length >= 24u) {
        (void)SynaTcmParseIdentification(Message->payload,
                                         Message->payload_length,
                                         &Context->Identification);
        return;
    }
    if (!Context->ControllerReady || Message->code != SYNA_REPORT_TOUCH ||
        Context->ReportConfigLength == 0u) {
        return;
    }

    if (!Context->DiagFirstReport) {
        Context->DiagFirstReport = TRUE;
        NxDiagValue(Context, L"NXFirstTouchBytes", Message->payload_length);
    }
    result = SynaTcmParseTouchReport(Context->ReportConfig,
                                     Context->ReportConfigLength,
                                     Message->payload,
                                     Message->payload_length,
                                     SYNA_TCM_MAX_CONTACTS,
                                     &frame);
    if (result == SYNA_RESULT_OK) {
        NTSTATUS submit_status = NxVhfSubmitFrame(Context, &frame);
        if (!Context->DiagFirstSubmit) {
            Context->DiagFirstSubmit = TRUE;
            NxDiagValue(Context, L"NXFirstHidStatus", (ULONG)submit_status);
        }
    }
}

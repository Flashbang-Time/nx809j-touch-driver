/* SPDX-License-Identifier: GPL-2.0-only */
#include "nx_touch.h"

#define NX_FINGER_COLLECTION \
    0x05, 0x0D,                         /* Usage Page (Digitizers) */ \
    0x09, 0x22,                         /* Usage (Finger) */ \
    0xA1, 0x02,                         /* Collection (Logical) */ \
    0x09, 0x42,                         /* Usage (Tip Switch) */ \
    0x09, 0x47,                         /* Usage (Confidence) */ \
    0x15, 0x00,                         /* Logical Minimum (0) */ \
    0x25, 0x01,                         /* Logical Maximum (1) */ \
    0x75, 0x01,                         /* Report Size (1) */ \
    0x95, 0x02,                         /* Report Count (2) */ \
    0x81, 0x02,                         /* Input (Data,Var,Abs) */ \
    0x75, 0x06,                         /* Report Size (6) */ \
    0x95, 0x01,                         /* Report Count (1) */ \
    0x81, 0x03,                         /* Input (Const,Var,Abs) */ \
    0x75, 0x08,                         /* Report Size (8) */ \
    0x09, 0x51,                         /* Usage (Contact Identifier) */ \
    0x25, 0x0A,                         /* Logical Maximum (10) */ \
    0x95, 0x01,                         /* Report Count (1) */ \
    0x81, 0x02,                         /* Input (Data,Var,Abs) */ \
    0x05, 0x01,                         /* Usage Page (Generic Desktop) */ \
    0x09, 0x30,                         /* Usage (X) */ \
    0x16, 0x00, 0x00,                   /* Logical Minimum (0) */ \
    0x26, 0xBF, 0x04,                   /* Logical Maximum (1215) */ \
    0x35, 0x00,                         /* Physical Minimum (0) */ \
    0x46, 0xB0, 0x02,                   /* Approximate width (688 x 0.1 mm) */ \
    0x55, 0x0E,                         /* Unit Exponent (-2) */ \
    0x65, 0x11,                         /* Unit (cm, SI Linear) */ \
    0x75, 0x10,                         /* Report Size (16) */ \
    0x95, 0x01,                         /* Report Count (1) */ \
    0x81, 0x02,                         /* Input (Data,Var,Abs) */ \
    0x09, 0x31,                         /* Usage (Y) */ \
    0x26, 0x7F, 0x0A,                   /* Logical Maximum (2687) */ \
    0x46, 0xF7, 0x05,                   /* Approximate height (1527 x 0.1 mm) */ \
    0x81, 0x02,                         /* Input (Data,Var,Abs) */ \
    0x45, 0x00,                         /* Clear physical maximum */ \
    0x55, 0x00,                         /* Clear unit exponent */ \
    0x65, 0x00,                         /* Clear unit */ \
    0xC0                                /* End Collection */

static UCHAR NxTouchReportDescriptor[] = {
    0x05, 0x0D,                         /* Usage Page (Digitizers) */
    0x09, 0x04,                         /* Usage (Touch Screen) */
    0xA1, 0x01,                         /* Collection (Application) */
    0x85, NX809J_TOUCH_REPORT_ID,       /* Report ID (Touch) */
    NX_FINGER_COLLECTION,
    NX_FINGER_COLLECTION,
    NX_FINGER_COLLECTION,
    NX_FINGER_COLLECTION,
    NX_FINGER_COLLECTION,
    NX_FINGER_COLLECTION,
    NX_FINGER_COLLECTION,
    NX_FINGER_COLLECTION,
    NX_FINGER_COLLECTION,
    NX_FINGER_COLLECTION,
    0x05, 0x0D,                         /* Usage Page (Digitizers) */
    0x09, 0x56,                         /* Usage (Scan Time) */
    0x16, 0x00, 0x00,                   /* Logical Minimum (0) */
    0x26, 0xFF, 0xFF,                   /* Logical Maximum (65535) */
    0x75, 0x10,                         /* Report Size (16) */
    0x95, 0x01,                         /* Report Count (1) */
    0x81, 0x02,                         /* Input (Data,Var,Abs) */
    0x09, 0x54,                         /* Usage (Contact Count) */
    0x15, 0x00,                         /* Logical Minimum (0) */
    0x25, 0x0A,                         /* Logical Maximum (10) */
    0x75, 0x08,                         /* Report Size (8) */
    0x95, 0x01,                         /* Report Count (1) */
    0x81, 0x02,                         /* Input (Data,Var,Abs) */
    0x85, NX809J_MAX_COUNT_REPORT_ID,   /* Report ID (Maximum Count) */
    0x09, 0x55,                         /* Usage (Contact Count Maximum) */
    0x25, 0x0A,                         /* Logical Maximum (10) */
    0xB1, 0x02,                         /* Feature (Data,Var,Abs) */
    0xC0                                /* End Collection */
};

NTSTATUS
NxVhfCreate(_In_ NxDeviceContext *Context)
{
    VHF_CONFIG config;
    NTSTATUS status;

    VHF_CONFIG_INIT(&config,
                    WdfDeviceWdmGetDeviceObject(Context->Device),
                    (USHORT)sizeof(NxTouchReportDescriptor),
                    NxTouchReportDescriptor);
    config.VhfClientContext = Context;
    config.VendorID = 0x2C17; /* ZTE */
    config.ProductID = 0x8091;
    config.VersionNumber = 0x0001;
    config.EvtVhfAsyncOperationGetFeature = NxEvtVhfGetFeature;

    status = VhfCreate(&config, &Context->VhfHandle);
    if (!NT_SUCCESS(status)) {
        Context->VhfHandle = NULL;
        return status;
    }
    status = VhfStart(Context->VhfHandle);
    if (!NT_SUCCESS(status)) {
        VhfDelete(Context->VhfHandle, TRUE);
        Context->VhfHandle = NULL;
    }
    return status;
}

void
NxVhfDelete(_In_ NxDeviceContext *Context)
{
    if (Context->VhfHandle != NULL) {
        VhfDelete(Context->VhfHandle, TRUE);
        Context->VhfHandle = NULL;
    }
}

VOID
NxEvtVhfGetFeature(_In_ PVOID VhfClientContext,
                   _In_ VHFOPERATIONHANDLE VhfOperationHandle,
                   _In_opt_ PVOID VhfOperationContext,
                   _In_ PHID_XFER_PACKET HidTransferPacket)
{
    NTSTATUS status = STATUS_INVALID_DEVICE_REQUEST;

    UNREFERENCED_PARAMETER(VhfClientContext);
    UNREFERENCED_PARAMETER(VhfOperationContext);

    if (HidTransferPacket != NULL &&
        HidTransferPacket->reportId == NX809J_MAX_COUNT_REPORT_ID &&
        HidTransferPacket->reportBuffer != NULL &&
        HidTransferPacket->reportBufferLen >= sizeof(NxHidMaximumCountReport)) {
        NxHidMaximumCountReport *report =
            (NxHidMaximumCountReport *)HidTransferPacket->reportBuffer;
        report->report_id = NX809J_MAX_COUNT_REPORT_ID;
        report->maximum_count = SYNA_TCM_MAX_CONTACTS;
        status = STATUS_SUCCESS;
    }

    (void)VhfAsyncOperationComplete(VhfOperationHandle, status);
}

NTSTATUS
NxVhfSubmitFrame(_In_ NxDeviceContext *Context,
                 _In_ const SynaTouchFrame *Frame)
{
    HID_XFER_PACKET packet;
    NxHidTouchReport active;
    NxHidTouchReport report;
    NTSTATUS status;

    if (Context->VhfHandle == NULL) {
        return STATUS_DEVICE_NOT_READY;
    }
    SynaHidBuildTouchReport(Frame,
                            NX809J_LOGICAL_MAX_X,
                            NX809J_LOGICAL_MAX_Y,
                            &active);
    active.scan_time = (uint16_t)(KeQueryInterruptTime() / 1000u);
    report = active;
    SynaHidAppendReleases(&Context->LastTouchReport, &report);
    packet.reportBuffer = (PUCHAR)&report;
    packet.reportBufferLen = sizeof(report);
    packet.reportId = NX809J_TOUCH_REPORT_ID;
    status = VhfReadReportSubmit(Context->VhfHandle, &packet);
    if (NT_SUCCESS(status)) {
        Context->LastTouchReport = active;
    }
    return status;
}

NTSTATUS
NxVhfSubmitRelease(_In_ NxDeviceContext *Context)
{
    SynaTouchFrame empty_frame;
    RtlZeroMemory(&empty_frame, sizeof(empty_frame));
    return NxVhfSubmitFrame(Context, &empty_frame);
}

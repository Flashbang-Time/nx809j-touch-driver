/* SPDX-License-Identifier: GPL-2.0-only */
#include "nx_touch.h"

/* Best-effort diagnostics at PASSIVE_LEVEL; never change hardware outcomes.
 * PE can read these values even when PnP reports only a generic Code 10. */
void
NxDiagValue(_In_ NxDeviceContext *Context, _In_ PCWSTR Name, _In_ ULONG Value)
{
    WDFKEY key;
    UNICODE_STRING name;
    if (!NT_SUCCESS(WdfDriverOpenParametersRegistryKey(
            WdfDeviceGetDriver(Context->Device), KEY_SET_VALUE,
            WDF_NO_OBJECT_ATTRIBUTES, &key))) {
        return;
    }
    RtlInitUnicodeString(&name, Name);
    (void)WdfRegistryAssignULong(key, &name, Value);
    WdfRegistryClose(key);
}

void
NxDiagStage(_In_ NxDeviceContext *Context, _In_ ULONG Stage, _In_ NTSTATUS Status)
{
    NxDiagValue(Context, L"NXStatus", (ULONG)Status);
    NxDiagValue(Context, L"NXStage", Stage);
}

static void
NxReadDeviceSettings(_In_ WDFDEVICE Device, _Inout_ NxDeviceContext *Context)
{
    WDFKEY key;
    UNICODE_STRING name;
    ULONG value;

    Context->ProgramTlmm = FALSE;
    Context->GeniClockSelect = 0u;
    Context->GeniClockDivider = 2u;

    if (!NT_SUCCESS(WdfDeviceOpenRegistryKey(Device,
                                              PLUGPLAY_REGKEY_DEVICE,
                                              KEY_READ,
                                              WDF_NO_OBJECT_ATTRIBUTES,
                                              &key))) {
        return;
    }

    RtlInitUnicodeString(&name, L"ProgramTlmm");
    if (NT_SUCCESS(WdfRegistryQueryULong(key, &name, &value))) {
        Context->ProgramTlmm = value != 0u;
    }
    RtlInitUnicodeString(&name, L"PollingMode");
    if (NT_SUCCESS(WdfRegistryQueryULong(key, &name, &value))) {
        Context->PollingMode = value == 1u;
    }
    RtlInitUnicodeString(&name, L"GeniClockSelect");
    if (NT_SUCCESS(WdfRegistryQueryULong(key, &name, &value)) && value <= 7u) {
        Context->GeniClockSelect = value;
    }
    RtlInitUnicodeString(&name, L"GeniClockDivider");
    if (NT_SUCCESS(WdfRegistryQueryULong(key, &name, &value)) &&
        value >= 1u && value <= 0xFFFu) {
        Context->GeniClockDivider = value;
    }
    WdfRegistryClose(key);
}

static NTSTATUS
NxCreateDeviceObjects(_In_ WDFDEVICE Device, _Inout_ NxDeviceContext *Context)
{
    WDF_OBJECT_ATTRIBUTES attributes;
    WDF_INTERRUPT_CONFIG interrupt_config;
    WDF_TIMER_CONFIG timer_config;
    NTSTATUS status;

    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    attributes.ParentObject = Device;

    status = WdfWaitLockCreate(&attributes, &Context->IoLock);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    status = WdfIoTargetCreate(Device, &attributes, &Context->SpbTarget);
    if (!NT_SUCCESS(status)) {
        return status;
    }
    if (Context->PollingMode) {
        WDF_TIMER_CONFIG_INIT(&timer_config, NxEvtPollTimer);
        timer_config.AutomaticSerialization = FALSE;
        attributes.ExecutionLevel = WdfExecutionLevelPassive;
        return WdfTimerCreate(&timer_config, &attributes, &Context->PollTimer);
    }
    status = WdfIoTargetCreate(Device, &attributes, &Context->ResetTarget);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    WDF_INTERRUPT_CONFIG_INIT(&interrupt_config, NxEvtInterruptIsr, NULL);
    interrupt_config.PassiveHandling = TRUE;
    return WdfInterruptCreate(Device, &interrupt_config,
                              WDF_NO_OBJECT_ATTRIBUTES, &Context->Interrupt);
}

static void
NxUnmapRegisters(_Inout_ NxDeviceContext *Context)
{
    if (Context->IrqPinBase != NULL) {
        MmUnmapIoSpace(Context->IrqPinBase, NX809J_PIN_PAGE_SIZE);
        Context->IrqPinBase = NULL;
    }
    if (Context->ResetPinBase != NULL) {
        MmUnmapIoSpace(Context->ResetPinBase, NX809J_PIN_PAGE_SIZE);
        Context->ResetPinBase = NULL;
    }
    if (Context->WrapperBase != NULL) {
        MmUnmapIoSpace(Context->WrapperBase, Context->WrapperLength);
        Context->WrapperBase = NULL;
    }
    if (Context->SeBase != NULL) {
        MmUnmapIoSpace(Context->SeBase, Context->SeLength);
        Context->SeBase = NULL;
    }
}

static void
NxCloseHardware(_Inout_ NxDeviceContext *Context)
{
    NxControllerStop(Context);
    NxVhfDelete(Context);
    if (Context->ResetTargetOpen) {
        WdfIoTargetClose(Context->ResetTarget);
        Context->ResetTargetOpen = FALSE;
    }
    if (Context->SpbTargetOpen) {
        WdfIoTargetClose(Context->SpbTarget);
        Context->SpbTargetOpen = FALSE;
    }
    if (Context->BusBackend == NxBusBackendGeniMmio) {
        NxGeniShutdown(Context);
    }
    Context->BusBackend = NxBusBackendNone;
    NxUnmapRegisters(Context);
}

NTSTATUS
DriverEntry(_In_ PDRIVER_OBJECT DriverObject,
            _In_ PUNICODE_STRING RegistryPath)
{
    WDF_DRIVER_CONFIG config;

    WDF_DRIVER_CONFIG_INIT(&config, NxEvtDeviceAdd);
    return WdfDriverCreate(DriverObject, RegistryPath,
                           WDF_NO_OBJECT_ATTRIBUTES, &config, WDF_NO_HANDLE);
}

NTSTATUS
NxEvtDeviceAdd(_In_ WDFDRIVER Driver, _Inout_ PWDFDEVICE_INIT DeviceInit)
{
    WDF_PNPPOWER_EVENT_CALLBACKS pnp_callbacks;
    WDF_OBJECT_ATTRIBUTES attributes;
    WDF_DEVICE_POWER_POLICY_IDLE_SETTINGS idle_settings;
    NxDeviceContext *context;
    WDFDEVICE device;
    NTSTATUS status;

    UNREFERENCED_PARAMETER(Driver);

    WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&pnp_callbacks);
    pnp_callbacks.EvtDevicePrepareHardware = NxEvtDevicePrepareHardware;
    pnp_callbacks.EvtDeviceReleaseHardware = NxEvtDeviceReleaseHardware;
    pnp_callbacks.EvtDeviceD0Entry = NxEvtDeviceD0Entry;
    pnp_callbacks.EvtDeviceD0Exit = NxEvtDeviceD0Exit;
    WdfDeviceInitSetPnpPowerEventCallbacks(DeviceInit, &pnp_callbacks);
    WdfDeviceInitSetDeviceType(DeviceInit, FILE_DEVICE_UNKNOWN);
    WdfDeviceInitSetIoType(DeviceInit, WdfDeviceIoBuffered);

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, NxDeviceContext);
    status = WdfDeviceCreate(&DeviceInit, &attributes, &device);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    context = NxGetDeviceContext(device);
    RtlZeroMemory(context, sizeof(*context));
    context->Device = device;
    NxReadDeviceSettings(device, context);
    NxDiagValue(context, L"NXDiagnosticVersion", 3u);
    NxDiagValue(context, L"NXPollingMode", context->PollingMode);
    NxDiagStage(context, 1u, STATUS_SUCCESS);

    /* There is no Windows clock/rail provider for SE19 yet: stay in D0. */
    WDF_DEVICE_POWER_POLICY_IDLE_SETTINGS_INIT(&idle_settings,
                                                IdleCannotWakeFromS0);
    idle_settings.Enabled = WdfFalse;
    status = WdfDeviceAssignS0IdleSettings(device, &idle_settings);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    status = NxCreateDeviceObjects(device, context);
    NxDiagStage(context, 2u, status);
    return status;
}

NTSTATUS
NxEvtDevicePrepareHardware(_In_ WDFDEVICE Device,
                           _In_ WDFCMRESLIST ResourcesRaw,
                           _In_ WDFCMRESLIST ResourcesTranslated)
{
    NxDeviceContext *context = NxGetDeviceContext(Device);
    ULONG count;
    ULONG index;
    NTSTATUS status;
    PHYSICAL_ADDRESS reset_pin = { 0 };
    PHYSICAL_ADDRESS irq_pin = { 0 };

    UNREFERENCED_PARAMETER(ResourcesRaw);

    context->HasSpbConnection = FALSE;
    context->HasResetConnection = FALSE;
    context->SeLength = 0u;
    context->WrapperLength = 0u;
    count = WdfCmResourceListGetCount(ResourcesTranslated);
    for (index = 0; index < count; ++index) {
        PCM_PARTIAL_RESOURCE_DESCRIPTOR descriptor =
            WdfCmResourceListGetDescriptor(ResourcesTranslated, index);

        if (descriptor == NULL) {
            continue;
        }
        if (descriptor->Type == CmResourceTypeMemory) {
            ULONGLONG start =
                (ULONGLONG)descriptor->u.Memory.Start.QuadPart;
            if (start == NX809J_SE19_PHYSICAL_BASE &&
                descriptor->u.Memory.Length >= NX809J_SE19_MINIMUM_SIZE) {
                context->SePhysicalAddress = descriptor->u.Memory.Start;
                context->SeLength = descriptor->u.Memory.Length;
            } else if (start == NX809J_QUP4_PHYSICAL_BASE &&
                       descriptor->u.Memory.Length >=
                           NX809J_QUP4_MINIMUM_SIZE) {
                context->WrapperPhysicalAddress = descriptor->u.Memory.Start;
                context->WrapperLength = descriptor->u.Memory.Length;
            } else if (context->PollingMode &&
                       descriptor->u.Memory.Length == NX809J_PIN_PAGE_SIZE) {
                if (start == NX809J_RESET_PIN_BASE) {
                    reset_pin = descriptor->u.Memory.Start;
                } else if (start == NX809J_IRQ_PIN_BASE) {
                    irq_pin = descriptor->u.Memory.Start;
                }
            }
            continue;
        }
        if (descriptor->Type == CmResourceTypeConnection) {
            UCHAR connection_class = descriptor->u.Connection.Class;
            UCHAR connection_type = descriptor->u.Connection.Type;

            if (connection_class == CM_RESOURCE_CONNECTION_CLASS_SERIAL &&
                connection_type == CM_RESOURCE_CONNECTION_TYPE_SERIAL_SPI &&
                !context->HasSpbConnection) {
                context->SpbConnectionId.LowPart =
                    descriptor->u.Connection.IdLowPart;
                context->SpbConnectionId.HighPart =
                    descriptor->u.Connection.IdHighPart;
                context->HasSpbConnection = TRUE;
            } else if (connection_class == CM_RESOURCE_CONNECTION_CLASS_GPIO &&
                       connection_type == CM_RESOURCE_CONNECTION_TYPE_GPIO_IO &&
                       !context->HasResetConnection) {
                context->ResetConnectionId.LowPart =
                    descriptor->u.Connection.IdLowPart;
                context->ResetConnectionId.HighPart =
                    descriptor->u.Connection.IdHighPart;
                context->HasResetConnection = TRUE;
            }
        }
    }

    NxDiagValue(context, L"NXSeLength", context->SeLength);
    NxDiagValue(context, L"NXWrapperLength", context->WrapperLength);
    NxDiagStage(context, 3u, STATUS_DEVICE_CONFIGURATION_ERROR);
    if (context->PollingMode) {
        if (reset_pin.QuadPart == 0 || irq_pin.QuadPart == 0 || context->SeLength == 0u) {
            return STATUS_DEVICE_CONFIGURATION_ERROR;
        }
    } else if (!context->HasResetConnection ||
               (context->SeLength == 0u && !context->HasSpbConnection)) {
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    if (context->SeLength != 0u) {
        context->SeBase = (PUCHAR)MmMapIoSpaceEx(
            context->SePhysicalAddress, context->SeLength,
            PAGE_READWRITE | PAGE_NOCACHE);
        if (context->SeBase == NULL) {
            return STATUS_INSUFFICIENT_RESOURCES;
        }
        if (context->WrapperLength != 0u) {
            context->WrapperBase = (PUCHAR)MmMapIoSpaceEx(
                context->WrapperPhysicalAddress, context->WrapperLength,
                PAGE_READWRITE | PAGE_NOCACHE);
            if (context->WrapperBase == NULL) {
                NxUnmapRegisters(context);
                return STATUS_INSUFFICIENT_RESOURCES;
            }
        }
        context->BusBackend = NxBusBackendGeniMmio;
    } else {
        context->BusBackend = NxBusBackendSpb;
        status = NxOpenResourceTarget(context->SpbTarget,
                                      context->SpbConnectionId);
        if (!NT_SUCCESS(status)) {
            context->BusBackend = NxBusBackendNone;
            return status;
        }
        context->SpbTargetOpen = TRUE;
    }

    if (context->PollingMode) {
        context->ResetPinBase = (PUCHAR)MmMapIoSpaceEx(reset_pin, NX809J_PIN_PAGE_SIZE,
                                                      PAGE_READWRITE | PAGE_NOCACHE);
        context->IrqPinBase = (PUCHAR)MmMapIoSpaceEx(irq_pin, NX809J_PIN_PAGE_SIZE,
                                                    PAGE_READWRITE | PAGE_NOCACHE);
        if (context->ResetPinBase == NULL || context->IrqPinBase == NULL) {
            NxCloseHardware(context);
            return STATUS_INSUFFICIENT_RESOURCES;
        }
    } else {
        status = NxOpenResourceTarget(context->ResetTarget, context->ResetConnectionId);
        if (!NT_SUCCESS(status)) {
            NxCloseHardware(context);
            return status;
        }
        context->ResetTargetOpen = TRUE;
    }

    NxDiagStage(context, 4u, STATUS_SUCCESS);
    status = NxVhfCreate(context);
    NxDiagStage(context, 5u, status);
    if (!NT_SUCCESS(status)) {
        NxCloseHardware(context);
    }
    return status;
}

NTSTATUS
NxEvtDeviceReleaseHardware(_In_ WDFDEVICE Device,
                           _In_ WDFCMRESLIST ResourcesTranslated)
{
    NxDeviceContext *context = NxGetDeviceContext(Device);

    UNREFERENCED_PARAMETER(ResourcesTranslated);
    NxCloseHardware(context);
    return STATUS_SUCCESS;
}

NTSTATUS
NxEvtDeviceD0Entry(_In_ WDFDEVICE Device,
                   _In_ WDF_POWER_DEVICE_STATE PreviousState)
{
    NxDeviceContext *context = NxGetDeviceContext(Device);
    NTSTATUS status;

    UNREFERENCED_PARAMETER(PreviousState);
    if (context->BusBackend == NxBusBackendGeniMmio) {
        NxDiagStage(context, 10u, STATUS_SUCCESS);
        status = NxGeniInitialize(context);
        NxDiagStage(context, 11u, status);
        if (!NT_SUCCESS(status)) {
            return status;
        }
    }
    if (context->PollingMode) {
        NxConfigurePollingPins(context);
    }
    status = NxControllerStart(context);
    if (!NT_SUCCESS(status)) {
        NxDiagValue(context, L"NXStatus", (ULONG)status);
    }
    context->Powered = NT_SUCCESS(status);
    if (!NT_SUCCESS(status)) {
        KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
            "NxSynaTouch: controller start failed, status %#lx\n", status));
    }
    if (!NT_SUCCESS(status) && context->BusBackend == NxBusBackendGeniMmio) {
        NxGeniShutdown(context);
    }
    if (context->Powered && context->PollingMode) {
        WdfTimerStart(context->PollTimer, WDF_REL_TIMEOUT_IN_MS(NX_TOUCH_POLL_MS));
    }
    if (NT_SUCCESS(status)) { NxDiagStage(context, 100u, status); }
    return status;
}

NTSTATUS
NxEvtDeviceD0Exit(_In_ WDFDEVICE Device,
                  _In_ WDF_POWER_DEVICE_STATE TargetState)
{
    NxDeviceContext *context = NxGetDeviceContext(Device);

    UNREFERENCED_PARAMETER(TargetState);
    if (context->PollingMode) {
        WdfWaitLockAcquire(context->IoLock, NULL);
    }
    context->Powered = FALSE;
    if (context->PollingMode) {
        WdfWaitLockRelease(context->IoLock);
        WdfTimerStop(context->PollTimer, TRUE);
    }
    NxControllerStop(context);
    (void)NxVhfSubmitRelease(context);
    if (context->BusBackend == NxBusBackendGeniMmio) {
        NxGeniShutdown(context);
    }
    return STATUS_SUCCESS;
}

BOOLEAN
NxEvtInterruptIsr(_In_ WDFINTERRUPT Interrupt, _In_ ULONG MessageId)
{
    WDFDEVICE device = WdfInterruptGetDevice(Interrupt);
    NxDeviceContext *context = NxGetDeviceContext(device);
    SynaTcmMessage message;
    NTSTATUS status;

    UNREFERENCED_PARAMETER(MessageId);
    if (!context->Powered || !context->ControllerReady) {
        return FALSE;
    }

    /* Drain the report while the passive GPIO interrupt is masked. */
    WdfWaitLockAcquire(context->IoLock, NULL);
    status = NxReadWireMessage(context, &message);
    if (NT_SUCCESS(status)) {
        NxProcessMessage(context, &message);
    }
    WdfWaitLockRelease(context->IoLock);
    return TRUE;
}

VOID
NxEvtPollTimer(_In_ WDFTIMER Timer)
{
    WDFDEVICE device = (WDFDEVICE)WdfTimerGetParentObject(Timer);
    NxDeviceContext *context = NxGetDeviceContext(device);
    ULONG delay = NX_TOUCH_POLL_MS;
    ULONG i;

    WdfWaitLockAcquire(context->IoLock, NULL);
    if (context->Powered && context->ControllerReady) {
        for (i = 0; i < 4u && NxPollIrqAsserted(context); ++i) {
            SynaTcmMessage message;
            if (!NT_SUCCESS(NxReadWireMessage(context, &message))) {
                delay = 80u;
                break;
            }
            NxProcessMessage(context, &message);
        }
        WdfTimerStart(Timer, WDF_REL_TIMEOUT_IN_MS(delay));
    }
    WdfWaitLockRelease(context->IoLock);
}

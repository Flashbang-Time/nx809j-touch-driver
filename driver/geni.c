/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Minimal polling Qualcomm GENI SPI master for NX809J QUPv3 wrapper 4 SE19.
 *
 * This deliberately implements only the one hardware contract used by the
 * Synaptics S3910V: SPI mode 0, CS0, MSB first, 8 bits per word.  It mirrors
 * the FIFO path in Linux spi-geni-qcom.c and does not depend on a Windows SPI
 * controller driver (there is no matching inbox driver in Windows 11 ARM64).
 */
#include "nx_touch.h"

#define NX_GENI_FORCE_DEFAULT_REG       0x020u
#define NX_GENI_OUTPUT_CTRL             0x024u
#define NX_GENI_CGC_CTRL                0x028u
#define NX_GENI_STATUS                  0x040u
#define NX_GENI_SER_M_CLK_CFG           0x048u
#define NX_GENI_IF_DISABLE_RO           0x064u
#define NX_GENI_FW_REVISION_RO          0x068u
#define NX_GENI_CLK_SEL                 0x07Cu
#define NX_GENI_DMA_MODE_EN             0x258u
#define NX_GENI_BYTE_GRAN               0x254u
#define NX_GENI_TX_PACKING_CFG0         0x260u
#define NX_GENI_TX_PACKING_CFG1         0x264u
#define NX_GENI_RX_PACKING_CFG0         0x284u
#define NX_GENI_RX_PACKING_CFG1         0x288u
#define NX_GENI_M_CMD0                  0x600u
#define NX_GENI_M_CMD_CTRL              0x604u
#define NX_GENI_M_IRQ_STATUS            0x610u
#define NX_GENI_M_IRQ_EN                0x614u
#define NX_GENI_M_IRQ_CLEAR             0x618u
#define NX_GENI_S_IRQ_EN                0x644u
#define NX_GENI_S_IRQ_CLEAR             0x648u
#define NX_GENI_TX_FIFO                 0x700u
#define NX_GENI_RX_FIFO                 0x780u
#define NX_GENI_TX_FIFO_STATUS          0x800u
#define NX_GENI_RX_FIFO_STATUS          0x804u
#define NX_GENI_TX_WATERMARK            0x80Cu
#define NX_GENI_RX_WATERMARK            0x810u
#define NX_GENI_RX_RFR_WATERMARK        0x814u
#define NX_GENI_HW_PARAM_0              0xE24u
#define NX_GENI_HW_PARAM_1              0xE28u
#define NX_GENI_GSI_EVENT_EN            0xE18u
#define NX_GENI_IRQ_EN                  0xE1Cu
#define NX_GENI_DMA_GENERAL_CFG         0xE30u

#define NX_SPI_CPHA                     0x224u
#define NX_SPI_LOOPBACK                 0x22Cu
#define NX_SPI_CPOL                     0x230u
#define NX_SPI_DEMUX_OUTPUT_INV         0x24Cu
#define NX_SPI_DEMUX_SEL                0x250u
#define NX_SPI_TRANS_CFG                0x25Cu
#define NX_SPI_WORD_LEN                 0x268u
#define NX_SPI_TX_TRANS_LEN             0x26Cu
#define NX_SPI_RX_TRANS_LEN             0x270u
#define NX_SPI_PRE_POST_CMD_DELAY       0x274u
#define NX_SPI_DELAY_COUNTERS           0x278u

#define NX_GENI_PROTOCOL_SPI            1u
#define NX_GENI_FIFO_IF_DISABLE         0x1u
#define NX_GENI_DMA_ENABLE              0x1u
#define NX_GENI_M_CMD_ACTIVE            0x1u
#define NX_GENI_SER_CLK_ENABLE          0x1u
#define NX_GENI_SER_CLK_DIV_SHIFT       4u

#define NX_GENI_M_CMD_DONE              (1u << 0)
#define NX_GENI_M_CMD_OVERRUN           (1u << 1)
#define NX_GENI_M_ILLEGAL_CMD           (1u << 2)
#define NX_GENI_M_CMD_FAILURE           (1u << 3)
#define NX_GENI_M_CMD_CANCEL            (1u << 4)
#define NX_GENI_M_CMD_ABORT             (1u << 5)
#define NX_GENI_M_RX_FIFO_RD_ERR        (1u << 24)
#define NX_GENI_M_RX_FIFO_WR_ERR        (1u << 25)
#define NX_GENI_M_RX_FIFO_WATERMARK     (1u << 26)
#define NX_GENI_M_RX_FIFO_LAST          (1u << 27)
#define NX_GENI_M_TX_FIFO_RD_ERR        (1u << 28)
#define NX_GENI_M_TX_FIFO_WR_ERR        (1u << 29)
#define NX_GENI_M_TX_FIFO_WATERMARK     (1u << 30)

#define NX_GENI_M_ERROR_MASK \
    (NX_GENI_M_CMD_OVERRUN | NX_GENI_M_ILLEGAL_CMD | \
     NX_GENI_M_CMD_FAILURE | NX_GENI_M_RX_FIFO_RD_ERR | \
     NX_GENI_M_RX_FIFO_WR_ERR | NX_GENI_M_TX_FIFO_RD_ERR | \
     NX_GENI_M_TX_FIFO_WR_ERR)

#define NX_GENI_M_COMMON_IRQ_MASK \
    (0x7Eu | (0xFu << 22) | (0x3u << 28))
#define NX_GENI_M_FIFO_IRQ_MASK \
    (NX_GENI_M_CMD_DONE | NX_GENI_M_RX_FIFO_WATERMARK | \
     NX_GENI_M_RX_FIFO_LAST | NX_GENI_M_TX_FIFO_WATERMARK)
#define NX_GENI_S_COMMON_IRQ_MASK \
    (0x3Eu | (0x3Fu << 9) | (0x3u << 24))

#define NX_GENI_RX_LAST                 (1u << 31)
#define NX_GENI_RX_LAST_VALID_MASK      (7u << 28)
#define NX_GENI_RX_LAST_VALID_SHIFT     28u
#define NX_GENI_RX_FIFO_COUNT_MASK      0x01FFFFFFu
#define NX_GENI_TX_FIFO_COUNT_MASK      0x0FFFFFFFu

#define NX_GENI_CMD_CANCEL              (1u << 2)
#define NX_GENI_CMD_ABORT               (1u << 1)
#define NX_GENI_CMD_OPCODE_SHIFT        27u
#define NX_GENI_CMD_FRAGMENTATION       (1u << 2)

#define NX_SPI_OPCODE_TX_ONLY           1u
/* Linux's FIFO path forms full duplex as TX_ONLY | RX_ONLY == opcode 3. */
#define NX_SPI_OPCODE_TX_RX             3u
#define NX_SPI_OPCODE_CS_ASSERT         8u
#define NX_SPI_OPCODE_CS_DEASSERT       9u
#define NX_SPI_CS_TOGGLE                (1u << 1)

#define NX_TLMM_SPI_PHYSICAL_BASE       0x0F130000ull
#define NX_TLMM_SPI_MAPPING_SIZE        0x00004000u
#define NX_TLMM_PIN_STRIDE              0x1000u
#define NX_TLMM_CTL_OFFSET              0x000u
#define NX_TLMM_IO_OFFSET               0x004u
#define NX_TLMM_PULL_MASK               0x003u
#define NX_TLMM_FUNCTION_MASK           0x03Cu
#define NX_TLMM_DRIVE_MASK              0x1C0u
#define NX_TLMM_OUTPUT_ENABLE           0x200u
#define NX_TLMM_FUNCTION_QUP4_SE0       (1u << 2)
#define NX_TLMM_DRIVE_6MA               (2u << 6)
#define NX_TLMM_GPIO_OUTPUT_VALUE       (1u << 1)

#define NX_GENI_POLL_STALL_US           2u
#define NX_GENI_COMMAND_TIMEOUT_MS      10u

static __forceinline ULONG
NxReadRegister(_In_ NxDeviceContext *Context, _In_ ULONG Offset)
{
    return READ_REGISTER_ULONG((volatile ULONG *)(Context->SeBase + Offset));
}

static __forceinline void
NxWriteRegister(_In_ NxDeviceContext *Context,
                _In_ ULONG Offset,
                _In_ ULONG Value)
{
    WRITE_REGISTER_ULONG((volatile ULONG *)(Context->SeBase + Offset), Value);
}

static BOOLEAN
NxTimedOut(_In_ ULONGLONG Started, _In_ ULONG TimeoutMilliseconds)
{
    ULONGLONG elapsed = KeQueryInterruptTime() - Started;
    return elapsed >= ((ULONGLONG)TimeoutMilliseconds * 10000ull);
}

static void
NxGeniClearInterrupts(_In_ NxDeviceContext *Context)
{
    NxWriteRegister(Context, NX_GENI_M_IRQ_CLEAR, 0xFFFFFFFFu);
    NxWriteRegister(Context, NX_GENI_S_IRQ_CLEAR, 0xFFFFFFFFu);
}

static void
NxGeniAbortActiveCommand(_In_ NxDeviceContext *Context)
{
    ULONGLONG started;

    if ((NxReadRegister(Context, NX_GENI_STATUS) & NX_GENI_M_CMD_ACTIVE) == 0u) {
        return;
    }

    NxWriteRegister(Context, NX_GENI_M_CMD_CTRL, NX_GENI_CMD_CANCEL);
    started = KeQueryInterruptTime();
    while (!NxTimedOut(started, 20u)) {
        ULONG irq_status = NxReadRegister(Context, NX_GENI_M_IRQ_STATUS);
        if ((irq_status & (NX_GENI_M_CMD_CANCEL | NX_GENI_M_CMD_DONE)) != 0u) {
            NxWriteRegister(Context, NX_GENI_M_IRQ_CLEAR, irq_status);
            break;
        }
        KeStallExecutionProcessor(NX_GENI_POLL_STALL_US);
    }

    if ((NxReadRegister(Context, NX_GENI_STATUS) & NX_GENI_M_CMD_ACTIVE) != 0u) {
        NxWriteRegister(Context, NX_GENI_M_CMD_CTRL, NX_GENI_CMD_ABORT);
        started = KeQueryInterruptTime();
        while (!NxTimedOut(started, 20u)) {
            ULONG irq_status = NxReadRegister(Context, NX_GENI_M_IRQ_STATUS);
            if ((irq_status & (NX_GENI_M_CMD_ABORT | NX_GENI_M_CMD_DONE)) != 0u) {
                NxWriteRegister(Context, NX_GENI_M_IRQ_CLEAR, irq_status);
                break;
            }
            KeStallExecutionProcessor(NX_GENI_POLL_STALL_US);
        }
    }
    NxGeniClearInterrupts(Context);
}

static NTSTATUS
NxGeniRunSimpleCommand(_In_ NxDeviceContext *Context, _In_ ULONG Opcode)
{
    ULONGLONG started;

    NxGeniAbortActiveCommand(Context);
    NxGeniClearInterrupts(Context);
    NxWriteRegister(Context, NX_GENI_M_CMD0, Opcode << NX_GENI_CMD_OPCODE_SHIFT);

    started = KeQueryInterruptTime();
    for (;;) {
        ULONG irq_status = NxReadRegister(Context, NX_GENI_M_IRQ_STATUS);

        if ((irq_status & NX_GENI_M_ERROR_MASK) != 0u) {
            NxWriteRegister(Context, NX_GENI_M_IRQ_CLEAR, irq_status);
            NxGeniAbortActiveCommand(Context);
            return STATUS_IO_DEVICE_ERROR;
        }
        if ((irq_status & NX_GENI_M_CMD_DONE) != 0u) {
            NxWriteRegister(Context, NX_GENI_M_IRQ_CLEAR, irq_status);
            return STATUS_SUCCESS;
        }
        if (irq_status != 0u) {
            NxWriteRegister(Context, NX_GENI_M_IRQ_CLEAR, irq_status);
        }
        if (NxTimedOut(started, NX_GENI_COMMAND_TIMEOUT_MS)) {
            NxGeniAbortActiveCommand(Context);
            return STATUS_IO_TIMEOUT;
        }
        KeStallExecutionProcessor(NX_GENI_POLL_STALL_US);
    }
}

static void
NxGeniDrainStaleRx(_In_ NxDeviceContext *Context)
{
    ULONG status = NxReadRegister(Context, NX_GENI_RX_FIFO_STATUS);
    ULONG words = status & NX_GENI_RX_FIFO_COUNT_MASK;

    if (words > Context->GeniRxFifoDepth) {
        words = Context->GeniRxFifoDepth;
    }
    while (words-- != 0u) {
        (void)NxReadRegister(Context, NX_GENI_RX_FIFO);
    }
}

static void
NxGeniFillTxFifo(_In_ NxDeviceContext *Context,
                 _In_reads_bytes_opt_(Length) const uint8_t *Transmit,
                 _In_ size_t Length,
                 _Inout_ size_t *Transferred)
{
    ULONG fifo_status = NxReadRegister(Context, NX_GENI_TX_FIFO_STATUS);
    ULONG words_used = fifo_status & NX_GENI_TX_FIFO_COUNT_MASK;
    ULONG words_free;

    if (words_used >= Context->GeniTxFifoDepth) {
        return;
    }
    words_free = Context->GeniTxFifoDepth - words_used;
    while (words_free-- != 0u && *Transferred < Length) {
        ULONG word = 0u;
        ULONG byte_index;

        for (byte_index = 0u;
             byte_index < 4u && *Transferred < Length;
             ++byte_index, ++(*Transferred)) {
            ULONG value = Transmit != NULL ? Transmit[*Transferred] : 0xFFu;
            word |= value << (byte_index * 8u);
        }
        NxWriteRegister(Context, NX_GENI_TX_FIFO, word);
    }
}

static void
NxGeniDrainRxFifo(_In_ NxDeviceContext *Context,
                  _Out_writes_bytes_(Length) uint8_t *Receive,
                  _In_ size_t Length,
                  _Inout_ size_t *Transferred)
{
    ULONG fifo_status = NxReadRegister(Context, NX_GENI_RX_FIFO_STATUS);
    ULONG words = fifo_status & NX_GENI_RX_FIFO_COUNT_MASK;
    ULONG valid_bytes = words * 4u;
    ULONG word_index;

    if (words > Context->GeniRxFifoDepth) {
        words = Context->GeniRxFifoDepth;
        valid_bytes = words * 4u;
    }
    if ((fifo_status & NX_GENI_RX_LAST) != 0u && words != 0u) {
        ULONG last_valid =
            (fifo_status & NX_GENI_RX_LAST_VALID_MASK) >>
            NX_GENI_RX_LAST_VALID_SHIFT;
        if (last_valid != 0u && last_valid < 4u) {
            valid_bytes -= 4u - last_valid;
        }
    }

    for (word_index = 0u; word_index < words; ++word_index) {
        ULONG word = NxReadRegister(Context, NX_GENI_RX_FIFO);
        ULONG byte_index;

        for (byte_index = 0u; byte_index < 4u; ++byte_index) {
            ULONG absolute_byte = word_index * 4u + byte_index;
            if (absolute_byte >= valid_bytes) {
                break;
            }
            if (*Transferred < Length) {
                Receive[*Transferred] = (uint8_t)(word >> (byte_index * 8u));
                ++(*Transferred);
            }
        }
    }
}

static NTSTATUS
NxGeniTransfer(_In_ NxDeviceContext *Context,
               _In_reads_bytes_opt_(Length) const uint8_t *Transmit,
               _Out_writes_bytes_opt_(Length) uint8_t *Receive,
               _In_ size_t Length)
{
    size_t tx_transferred = 0u;
    size_t rx_transferred = 0u;
    ULONG opcode = Receive != NULL ? NX_SPI_OPCODE_TX_RX : NX_SPI_OPCODE_TX_ONLY;
    ULONGLONG started;
    NTSTATUS status;
    NTSTATUS cs_status;

    if (!Context->GeniReady || Length == 0u || Length > 0x00FFFFFFu ||
        (Transmit == NULL && Receive == NULL)) {
        return STATUS_INVALID_PARAMETER;
    }

    status = NxGeniRunSimpleCommand(Context, NX_SPI_OPCODE_CS_ASSERT);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    NxGeniDrainStaleRx(Context);
    NxGeniClearInterrupts(Context);
    NxWriteRegister(Context, NX_SPI_TX_TRANS_LEN, (ULONG)Length);
    if (Receive != NULL) {
        NxWriteRegister(Context, NX_SPI_RX_TRANS_LEN, (ULONG)Length);
    }
    NxWriteRegister(Context, NX_GENI_M_CMD0,
                    (opcode << NX_GENI_CMD_OPCODE_SHIFT) |
                    NX_GENI_CMD_FRAGMENTATION);

    NxGeniFillTxFifo(Context, Transmit, Length, &tx_transferred);
    if (tx_transferred < Length) {
        NxWriteRegister(Context, NX_GENI_TX_WATERMARK, 1u);
    }

    started = KeQueryInterruptTime();
    for (;;) {
        ULONG irq_status = NxReadRegister(Context, NX_GENI_M_IRQ_STATUS);

        /* RX_LAST must be serviced before CMD_DONE, including when both set. */
        if (Receive != NULL &&
            ((irq_status & (NX_GENI_M_RX_FIFO_WATERMARK |
                            NX_GENI_M_RX_FIFO_LAST)) != 0u ||
             (NxReadRegister(Context, NX_GENI_RX_FIFO_STATUS) &
              NX_GENI_RX_FIFO_COUNT_MASK) != 0u)) {
            NxGeniDrainRxFifo(Context, Receive, Length, &rx_transferred);
        }

        if (tx_transferred < Length) {
            NxGeniFillTxFifo(Context, Transmit, Length, &tx_transferred);
            if (tx_transferred == Length) {
                NxWriteRegister(Context, NX_GENI_TX_WATERMARK, 0u);
            }
        }

        if ((irq_status & NX_GENI_M_ERROR_MASK) != 0u) {
            NxWriteRegister(Context, NX_GENI_M_IRQ_CLEAR, irq_status);
            status = STATUS_IO_DEVICE_ERROR;
            break;
        }
        if ((irq_status & NX_GENI_M_CMD_DONE) != 0u) {
            NxWriteRegister(Context, NX_GENI_M_IRQ_CLEAR, irq_status);
            status = (tx_transferred == Length &&
                      (Receive == NULL || rx_transferred == Length))
                   ? STATUS_SUCCESS : STATUS_DEVICE_DATA_ERROR;
            break;
        }
        if (irq_status != 0u) {
            NxWriteRegister(Context, NX_GENI_M_IRQ_CLEAR, irq_status);
        }
        if (NxTimedOut(started, NX_GENI_COMMAND_TIMEOUT_MS)) {
            status = STATUS_IO_TIMEOUT;
            break;
        }
        KeStallExecutionProcessor(NX_GENI_POLL_STALL_US);
    }

    NxWriteRegister(Context, NX_GENI_TX_WATERMARK, 0u);
    if (!NT_SUCCESS(status)) {
        NxGeniAbortActiveCommand(Context);
    }
    cs_status = NxGeniRunSimpleCommand(Context, NX_SPI_OPCODE_CS_DEASSERT);
    return NT_SUCCESS(status) ? cs_status : status;
}

static NTSTATUS
NxTlmmConfigureSpiPins(_Inout_ NxDeviceContext *Context)
{
    PHYSICAL_ADDRESS physical_address;
    PUCHAR base;
    ULONG index;

    if (!Context->ProgramTlmm) {
        return STATUS_SUCCESS;
    }

    physical_address.QuadPart = NX_TLMM_SPI_PHYSICAL_BASE;
    base = (PUCHAR)MmMapIoSpaceEx(physical_address,
                                 NX_TLMM_SPI_MAPPING_SIZE,
                                 PAGE_READWRITE | PAGE_NOCACHE);
    if (base == NULL) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    for (index = 0u; index < 4u; ++index) {
        volatile ULONG *control =
            (volatile ULONG *)(base + index * NX_TLMM_PIN_STRIDE +
                               NX_TLMM_CTL_OFFSET);
        volatile ULONG *io =
            (volatile ULONG *)(base + index * NX_TLMM_PIN_STRIDE +
                               NX_TLMM_IO_OFFSET);
        ULONG value = READ_REGISTER_ULONG(control);

        Context->TlmmSavedControl[index] = value;
        Context->TlmmSavedIo[index] = READ_REGISTER_ULONG(io);
        value &= ~(NX_TLMM_PULL_MASK | NX_TLMM_FUNCTION_MASK |
                   NX_TLMM_DRIVE_MASK);
        value |= NX_TLMM_FUNCTION_QUP4_SE0 | NX_TLMM_DRIVE_6MA;
        if (index == 3u) {
            /* Match stock GPIO51 output-low state before handing it to CS0. */
            WRITE_REGISTER_ULONG(io,
                Context->TlmmSavedIo[index] & ~NX_TLMM_GPIO_OUTPUT_VALUE);
            value |= NX_TLMM_OUTPUT_ENABLE;
        }
        WRITE_REGISTER_ULONG(control, value);
    }
    KeMemoryBarrier();
    Context->TlmmStateSaved = TRUE;
    MmUnmapIoSpace(base, NX_TLMM_SPI_MAPPING_SIZE);
    return STATUS_SUCCESS;
}

static void
NxTlmmRestoreSpiPins(_Inout_ NxDeviceContext *Context)
{
    PHYSICAL_ADDRESS physical_address;
    PUCHAR base;
    ULONG index;

    if (!Context->ProgramTlmm || !Context->TlmmStateSaved) {
        return;
    }
    physical_address.QuadPart = NX_TLMM_SPI_PHYSICAL_BASE;
    base = (PUCHAR)MmMapIoSpaceEx(physical_address,
                                 NX_TLMM_SPI_MAPPING_SIZE,
                                 PAGE_READWRITE | PAGE_NOCACHE);
    if (base == NULL) {
        return;
    }
    for (index = 0u; index < 4u; ++index) {
        volatile ULONG *control =
            (volatile ULONG *)(base + index * NX_TLMM_PIN_STRIDE +
                               NX_TLMM_CTL_OFFSET);
        volatile ULONG *io =
            (volatile ULONG *)(base + index * NX_TLMM_PIN_STRIDE +
                               NX_TLMM_IO_OFFSET);
        WRITE_REGISTER_ULONG(io, Context->TlmmSavedIo[index]);
        WRITE_REGISTER_ULONG(control, Context->TlmmSavedControl[index]);
    }
    KeMemoryBarrier();
    MmUnmapIoSpace(base, NX_TLMM_SPI_MAPPING_SIZE);
    Context->TlmmStateSaved = FALSE;
}

NTSTATUS
NxGeniInitialize(_Inout_ NxDeviceContext *Context)
{
    ULONG protocol;
    ULONG parameter0;
    ULONG parameter1;
    ULONG value;
    NTSTATUS status;

    if (Context->SeBase == NULL || Context->SeLength < NX809J_SE19_MINIMUM_SIZE) {
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    status = NxTlmmConfigureSpiPins(Context);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    protocol = (NxReadRegister(Context, NX_GENI_FW_REVISION_RO) >> 8) & 0xFFu;
    NxDiagValue(Context, L"NXGeniProtocol", protocol);
    if (protocol != NX_GENI_PROTOCOL_SPI) {
        KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
            "NxSynaTouch: SE19 protocol %lu is not SPI; firmware clocks may be off\n",
            protocol));
        NxTlmmRestoreSpiPins(Context);
        return STATUS_DEVICE_NOT_READY;
    }
    value = NxReadRegister(Context, NX_GENI_IF_DISABLE_RO);
    NxDiagValue(Context, L"NXGeniIfDisable", value);
    if ((value & NX_GENI_FIFO_IF_DISABLE) != 0u) {
        KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
            "NxSynaTouch: SE19 FIFO is disabled (GENI_IF_DISABLE_RO=%#lx)\n",
            value));
        NxTlmmRestoreSpiPins(Context);
        return STATUS_NOT_SUPPORTED;
    }

    parameter0 = NxReadRegister(Context, NX_GENI_HW_PARAM_0);
    parameter1 = NxReadRegister(Context, NX_GENI_HW_PARAM_1);
    Context->GeniTxFifoDepth = (parameter0 >> 16) & 0xFFu;
    Context->GeniRxFifoDepth = (parameter1 >> 16) & 0xFFu;
    Context->GeniFifoWidthBits = (parameter0 >> 24) & 0x3Fu;
    NxDiagValue(Context, L"NXFifoTxDepth", Context->GeniTxFifoDepth);
    NxDiagValue(Context, L"NXFifoRxDepth", Context->GeniRxFifoDepth);
    NxDiagValue(Context, L"NXFifoWidth", Context->GeniFifoWidthBits);
    if (Context->GeniTxFifoDepth < 4u || Context->GeniTxFifoDepth > 256u ||
        Context->GeniRxFifoDepth < 4u || Context->GeniRxFifoDepth > 256u ||
        Context->GeniFifoWidthBits != 32u) {
        KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
            "NxSynaTouch: unsupported FIFO tx=%lu rx=%lu width=%lu\n",
            Context->GeniTxFifoDepth, Context->GeniRxFifoDepth,
            Context->GeniFifoWidthBits));
        NxTlmmRestoreSpiPins(Context);
        return STATUS_DEVICE_HARDWARE_ERROR;
    }

    NxGeniClearInterrupts(Context);
    value = NxReadRegister(Context, NX_GENI_CGC_CTRL);
    NxWriteRegister(Context, NX_GENI_CGC_CTRL, value | 0x7Fu);
    value = NxReadRegister(Context, NX_GENI_DMA_GENERAL_CFG);
    NxWriteRegister(Context, NX_GENI_DMA_GENERAL_CFG, value | 0x0Fu);
    NxWriteRegister(Context, NX_GENI_OUTPUT_CTRL, 0x7Fu);
    NxWriteRegister(Context, NX_GENI_FORCE_DEFAULT_REG, 1u);

    /* Poll the SE status registers without driving the unclaimed SE IRQ. */
    NxWriteRegister(Context, NX_GENI_IRQ_EN, 0u);
    value = NxReadRegister(Context, NX_GENI_DMA_MODE_EN);
    NxWriteRegister(Context, NX_GENI_DMA_MODE_EN,
                    value & ~NX_GENI_DMA_ENABLE);
    NxWriteRegister(Context, NX_GENI_GSI_EVENT_EN, 0u);

    value = NxReadRegister(Context, NX_GENI_M_IRQ_EN);
    NxWriteRegister(Context, NX_GENI_M_IRQ_EN,
                    value | NX_GENI_M_COMMON_IRQ_MASK |
                    NX_GENI_M_FIFO_IRQ_MASK);
    value = NxReadRegister(Context, NX_GENI_S_IRQ_EN);
    NxWriteRegister(Context, NX_GENI_S_IRQ_EN,
                    value | NX_GENI_S_COMMON_IRQ_MASK);

    NxWriteRegister(Context, NX_GENI_RX_WATERMARK,
                    Context->GeniRxFifoDepth - 3u);
    NxWriteRegister(Context, NX_GENI_RX_RFR_WATERMARK,
                    Context->GeniRxFifoDepth - 2u);
    NxWriteRegister(Context, NX_GENI_TX_WATERMARK, 0u);

    /* Exact 4x8, MSB-first packing generated by geni_se_config_packing(). */
    NxWriteRegister(Context, NX_GENI_TX_PACKING_CFG0, 0x0007F8FEu);
    NxWriteRegister(Context, NX_GENI_TX_PACKING_CFG1, 0x000FFEFEu);
    NxWriteRegister(Context, NX_GENI_RX_PACKING_CFG0, 0x0007F8FEu);
    NxWriteRegister(Context, NX_GENI_RX_PACKING_CFG1, 0x000FFEFEu);
    NxWriteRegister(Context, NX_GENI_BYTE_GRAN, 0u);

    NxWriteRegister(Context, NX_SPI_WORD_LEN, 4u); /* 8 bits - minimum 4 */
    NxWriteRegister(Context, NX_SPI_LOOPBACK, 0u);
    NxWriteRegister(Context, NX_SPI_CPHA, 0u);
    NxWriteRegister(Context, NX_SPI_CPOL, 0u);
    NxWriteRegister(Context, NX_SPI_DEMUX_SEL, 0u);
    NxWriteRegister(Context, NX_SPI_DEMUX_OUTPUT_INV, 0u);
    NxWriteRegister(Context, NX_SPI_PRE_POST_CMD_DELAY, 0u);
    NxWriteRegister(Context, NX_SPI_DELAY_COUNTERS, 0u);
    value = NxReadRegister(Context, NX_SPI_TRANS_CFG);
    NxWriteRegister(Context, NX_SPI_TRANS_CFG, value & ~NX_SPI_CS_TOGGLE);

    NxWriteRegister(Context, NX_GENI_CLK_SEL,
                    Context->GeniClockSelect & 0x7u);
    NxWriteRegister(Context, NX_GENI_SER_M_CLK_CFG,
                    ((Context->GeniClockDivider & 0xFFFu) <<
                     NX_GENI_SER_CLK_DIV_SHIFT) |
                    NX_GENI_SER_CLK_ENABLE);

    NxGeniDrainStaleRx(Context);
    NxGeniClearInterrupts(Context);
    Context->GeniReady = TRUE;
    return STATUS_SUCCESS;
}

void
NxGeniShutdown(_Inout_ NxDeviceContext *Context)
{
    if (Context->GeniReady) {
        NxGeniAbortActiveCommand(Context);
        NxWriteRegister(Context, NX_GENI_TX_WATERMARK, 0u);
        NxGeniClearInterrupts(Context);
    }
    Context->GeniReady = FALSE;
    NxTlmmRestoreSpiPins(Context);
}

NTSTATUS
NxGeniWrite(_In_ NxDeviceContext *Context,
            _In_reads_bytes_(Length) const uint8_t *Buffer,
            _In_ size_t Length)
{
    return NxGeniTransfer(Context, Buffer, NULL, Length);
}

NTSTATUS
NxGeniRead(_In_ NxDeviceContext *Context,
           _Out_writes_bytes_(Length) uint8_t *Buffer,
           _In_ size_t Length)
{
    /* Stock syna_spi_read sends 0xFF on MOSI for every byte read. */
    return NxGeniTransfer(Context, NULL, Buffer, Length);
}

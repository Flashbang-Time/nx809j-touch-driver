/* SPDX-License-Identifier: GPL-2.0-only */
#include "../protocol/syna_tcm.h"
#include "../protocol/syna_hid.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void
WriteBits(uint8_t *buffer, size_t bit_offset, uint8_t bit_count, uint32_t value)
{
    uint8_t bit;
    for (bit = 0; bit < bit_count; ++bit) {
        if ((value & (1u << bit)) != 0u) {
            size_t destination = bit_offset + bit;
            buffer[destination / 8u] |= (uint8_t)(1u << (destination % 8u));
        }
    }
}

static void
TestCommandAndMessage(void)
{
    uint8_t command[8];
    size_t command_length = 0;
    const uint8_t payload[] = { 0x12, 0x34 };
    const uint8_t response[] = { 0xA5, 0x11, 0x02, 0x00, 0xDE, 0xAD, 0x5A };
    SynaTcmMessage message;

    assert(SynaTcmBuildCommand(SYNA_CMD_IDENTIFY, NULL, 0,
                               command, sizeof(command), &command_length) == 0);
    assert(command_length == 3u);
    assert(command[0] == 0x02 && command[1] == 0 && command[2] == 0);

    assert(SynaTcmBuildCommand(0x44, payload, sizeof(payload),
                               command, sizeof(command), &command_length) == 0);
    assert(command_length == 5u);
    assert(memcmp(command, "\x44\x02\x00\x12\x34", 5u) == 0);

    assert(SynaTcmDecodeMessage(response, sizeof(response), &message) == 0);
    assert(message.code == SYNA_REPORT_TOUCH);
    assert(message.payload_length == 2u);
    assert(message.payload[0] == 0xDE && message.payload[1] == 0xAD);
}

static void
TestInfoParsing(void)
{
    uint8_t identification[24] = { 1, 1 };
    uint8_t application[38] = { 0 };
    SynaIdentification id;
    SynaApplicationInfo app;

    memcpy(&identification[2], "S3910V-NX809J", 13u);
    identification[18] = 0x78;
    identification[19] = 0x56;
    identification[20] = 0x34;
    identification[21] = 0x12;
    identification[22] = 0x00;
    identification[23] = 0x04;
    assert(SynaTcmParseIdentification(identification, sizeof(identification), &id) == 0);
    assert(strcmp(id.part_number, "S3910V-NX809J") == 0);
    assert(id.build_id == 0x12345678u);
    assert(id.max_write_size == 1024u);

    application[12] = 128;
    application[14] = 0x00;
    application[15] = 0x04;
    application[32] = 0xC0;
    application[33] = 0x04;
    application[34] = 0x80;
    application[35] = 0x0A;
    application[36] = 10;
    assert(SynaTcmParseApplicationInfo(application, sizeof(application), &app) == 0);
    assert(app.max_touch_report_config_size == 128u);
    assert(app.max_touch_report_payload_size == 1024u);
    assert(app.max_x == 1216u && app.max_y == 2688u && app.max_objects == 10u);
}

static void
TestTouchParserAndHid(void)
{
    uint8_t report_bytes[15] = { 0 };
    size_t offset = 0;
    SynaTouchFrame frame;
    NxHidTouchReport hid;

    WriteBits(report_bytes, offset, 4, 2); offset += 4;
    offset = (offset + 7u) & ~(size_t)7u;

    WriteBits(report_bytes, offset, 4, 0); offset += 4;
    WriteBits(report_bytes, offset, 4, 1); offset += 4;
    WriteBits(report_bytes, offset, 12, 100); offset += 12;
    WriteBits(report_bytes, offset, 12, 200); offset += 12;
    WriteBits(report_bytes, offset, 8, 50); offset += 8;
    WriteBits(report_bytes, offset, 8, 8); offset += 8;
    WriteBits(report_bytes, offset, 8, 9); offset += 8;

    WriteBits(report_bytes, offset, 4, 3); offset += 4;
    WriteBits(report_bytes, offset, 4, 2); offset += 4;
    WriteBits(report_bytes, offset, 12, 1200); offset += 12;
    WriteBits(report_bytes, offset, 12, 2500); offset += 12;
    WriteBits(report_bytes, offset, 8, 60); offset += 8;
    WriteBits(report_bytes, offset, 8, 10); offset += 8;
    WriteBits(report_bytes, offset, 8, 11); offset += 8;

    assert(offset == sizeof(report_bytes) * 8u);
    assert(SynaTcmParseTouchReport(SynaNx809jReportConfig,
                                   SynaNx809jReportConfigLength,
                                   report_bytes, sizeof(report_bytes),
                                   SYNA_TCM_MAX_CONTACTS, &frame) == 0);
    assert(frame.active_objects == 2u);
    assert(frame.contacts[0].classification == 1u);
    assert(frame.contacts[0].x == 100u && frame.contacts[0].y == 200u);
    assert(frame.contacts[3].classification == 2u);
    assert(frame.contacts[3].x == 1200u && frame.contacts[3].y == 2500u);

    SynaHidBuildTouchReport(&frame, NX809J_LOGICAL_MAX_X,
                            NX809J_LOGICAL_MAX_Y, &hid);
    assert(sizeof(hid) == 64u);
    assert(hid.report_id == NX809J_TOUCH_REPORT_ID);
    assert(hid.contact_count == 2u);
    assert(hid.contacts[0].contact_id == 0u);
    assert(hid.contacts[1].contact_id == 3u);
    assert(hid.contacts[1].x == 1200u && hid.contacts[1].y == 2500u);
}

static void
TestStockTouchAndRelease(void)
{
    uint8_t wire[24] = { 0 };
    SynaTouchFrame frame;
    NxHidTouchReport active;
    NxHidTouchReport released = { 0 };

    WriteBits(wire, 120, 8, 1); /* One active contact after vendor and gesture data. */
    WriteBits(wire, 128, 4, 7);
    WriteBits(wire, 132, 4, 1);
    WriteBits(wire, 136, 16, 900);
    WriteBits(wire, 152, 16, 2000);
    WriteBits(wire, 168, 12, 21);
    WriteBits(wire, 180, 12, 33);
    assert(SynaTcmParseTouchReport(SynaNx809jStockReportConfig,
                                   SynaNx809jStockReportConfigLength,
                                   wire, sizeof(wire),
                                   SYNA_TCM_MAX_CONTACTS, &frame) == 0);
    assert(frame.contacts[7].width_x == 21 && frame.contacts[7].width_y == 33);
    SynaHidBuildTouchReport(&frame, NX809J_LOGICAL_MAX_X,
                            NX809J_LOGICAL_MAX_Y, &active);
    assert(active.contact_count == 1 && active.contacts[0].contact_id == 7);
    SynaHidAppendReleases(&active, &released);
    assert(released.contact_count == 1 && released.contacts[0].contact_id == 7);
    assert(released.contacts[0].flags == NX_TOUCH_FLAG_CONFIDENCE);
    assert(released.contacts[0].x == 900 && released.contacts[0].y == 2000);
}

static void
TestFailures(void)
{
    const uint8_t bad_marker[] = { 0x00, 1, 0, 0, 0x5A };
    const uint8_t bad_padding[] = { 0xA5, 1, 0, 0, 0x00 };
    SynaTcmMessage message;
    uint32_t value;

    assert(SynaTcmDecodeMessage(bad_marker, sizeof(bad_marker), &message) ==
           SYNA_RESULT_BAD_MESSAGE);
    assert(SynaTcmDecodeMessage(bad_padding, sizeof(bad_padding), &message) ==
           SYNA_RESULT_BAD_MESSAGE);
    assert(SynaTcmGetBits(bad_padding, sizeof(bad_padding), 39, 2, &value) ==
           SYNA_RESULT_TRUNCATED);
}

int
main(void)
{
    TestCommandAndMessage();
    TestInfoParsing();
    TestTouchParserAndHid();
    TestStockTouchAndRelease();
    TestFailures();
    puts("NX809J Synaptics protocol tests: PASS");
    return 0;
}

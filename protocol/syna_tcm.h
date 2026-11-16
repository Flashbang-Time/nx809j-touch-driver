/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef NX809J_SYNA_TCM_H
#define NX809J_SYNA_TCM_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SYNA_TCM_MESSAGE_MARKER          0xA5u
#define SYNA_TCM_MESSAGE_PADDING         0x5Au
#define SYNA_TCM_MESSAGE_HEADER_SIZE     4u
#define SYNA_TCM_MAX_CONTACTS            10u
#define SYNA_TCM_MAX_CONFIG_SIZE         256u
#define SYNA_TCM_MAX_PAYLOAD_SIZE        4096u

enum SynaTcmCommand {
    SYNA_CMD_CONTINUE_WRITE = 0x01,
    SYNA_CMD_IDENTIFY = 0x02,
    SYNA_CMD_RESET = 0x04,
    SYNA_CMD_ENABLE_REPORT = 0x05,
    SYNA_CMD_GET_APPLICATION_INFO = 0x20,
    SYNA_CMD_GET_TOUCH_REPORT_CONFIG = 0x25,
    SYNA_CMD_SET_TOUCH_REPORT_CONFIG = 0x26,
    SYNA_CMD_REZERO = 0x27,
    SYNA_CMD_ENTER_DEEP_SLEEP = 0x2C,
    SYNA_CMD_EXIT_DEEP_SLEEP = 0x2D
};

enum SynaTcmStatus {
    SYNA_STATUS_IDLE = 0x00,
    SYNA_STATUS_OK = 0x01,
    SYNA_STATUS_BUSY = 0x02,
    SYNA_STATUS_CONTINUED_READ = 0x03,
    SYNA_STATUS_NOT_EXECUTED_IN_DEEP_SLEEP = 0x0B,
    SYNA_STATUS_RECEIVE_BUFFER_OVERFLOW = 0x0C,
    SYNA_STATUS_PREVIOUS_COMMAND_PENDING = 0x0D,
    SYNA_STATUS_NOT_IMPLEMENTED = 0x0E,
    SYNA_STATUS_ERROR = 0x0F,
    SYNA_REPORT_IDENTIFY = 0x10,
    SYNA_REPORT_TOUCH = 0x11,
    SYNA_STATUS_INVALID = 0xFF
};

enum SynaTouchReportCode {
    SYNA_TOUCH_END = 0,
    SYNA_TOUCH_FOREACH_ACTIVE_OBJECT = 1,
    SYNA_TOUCH_FOREACH_OBJECT = 2,
    SYNA_TOUCH_FOREACH_END = 3,
    SYNA_TOUCH_PAD_TO_NEXT_BYTE = 4,
    SYNA_TOUCH_TIMESTAMP = 5,
    SYNA_TOUCH_OBJECT_N_INDEX = 6,
    SYNA_TOUCH_OBJECT_N_CLASSIFICATION = 7,
    SYNA_TOUCH_OBJECT_N_X_POSITION = 8,
    SYNA_TOUCH_OBJECT_N_Y_POSITION = 9,
    SYNA_TOUCH_OBJECT_N_Z = 10,
    SYNA_TOUCH_OBJECT_N_X_WIDTH = 11,
    SYNA_TOUCH_OBJECT_N_Y_WIDTH = 12,
    SYNA_TOUCH_OBJECT_N_TX_POSITION_TIXELS = 13,
    SYNA_TOUCH_OBJECT_N_RX_POSITION_TIXELS = 14,
    SYNA_TOUCH_0D_BUTTONS_STATE = 15,
    SYNA_TOUCH_GESTURE_DOUBLE_TAP = 16,
    SYNA_TOUCH_FRAME_RATE = 17,
    SYNA_TOUCH_POWER_IM = 18,
    SYNA_TOUCH_CID_IM = 19,
    SYNA_TOUCH_RAIL_IM = 20,
    SYNA_TOUCH_CID_VARIANCE_IM = 21,
    SYNA_TOUCH_NSM_FREQUENCY = 22,
    SYNA_TOUCH_NSM_STATE = 23,
    SYNA_TOUCH_NUM_OF_ACTIVE_OBJECTS = 24,
    SYNA_TOUCH_NUM_OF_CPU_CYCLES_USED_SINCE_LAST_FRAME = 25,
    SYNA_TOUCH_TUNING_GAUSSIAN_WIDTHS = 0x80,
    SYNA_TOUCH_TUNING_SMALL_OBJECT_PARAMS = 0x81,
    SYNA_TOUCH_TUNING_0D_BUTTONS_VARIANCE = 0x82,
    /* TCM vendor format: skip the number of bits in the following byte. */
    SYNA_TOUCH_SKIP_BITS = 0xCA
};

enum SynaResult {
    SYNA_RESULT_OK = 0,
    SYNA_RESULT_INVALID_ARGUMENT = -1,
    SYNA_RESULT_BUFFER_TOO_SMALL = -2,
    SYNA_RESULT_BAD_MESSAGE = -3,
    SYNA_RESULT_OUT_OF_RANGE = -4,
    SYNA_RESULT_UNSUPPORTED = -5,
    SYNA_RESULT_TRUNCATED = -6,
    SYNA_RESULT_MALFORMED_CONFIG = -7
};

typedef struct SynaTcmMessage {
    uint8_t code;
    const uint8_t *payload;
    uint16_t payload_length;
    size_t wire_length;
} SynaTcmMessage;

typedef struct SynaIdentification {
    uint8_t version;
    uint8_t mode;
    char part_number[17];
    uint32_t build_id;
    uint16_t max_write_size;
} SynaIdentification;

typedef struct SynaApplicationInfo {
    uint16_t version;
    uint16_t status;
    uint16_t max_touch_report_config_size;
    uint16_t max_touch_report_payload_size;
    uint16_t max_x;
    uint16_t max_y;
    uint16_t max_objects;
} SynaApplicationInfo;

typedef struct SynaContact {
    uint8_t index;
    uint8_t classification;
    uint32_t x;
    uint32_t y;
    uint32_t pressure;
    uint32_t width_x;
    uint32_t width_y;
    uint32_t tx_position;
    uint32_t rx_position;
} SynaContact;

typedef struct SynaTouchFrame {
    SynaContact contacts[SYNA_TCM_MAX_CONTACTS];
    uint32_t timestamp;
    uint32_t buttons_state;
    uint32_t gesture_double_tap;
    uint32_t frame_rate;
    uint32_t active_objects;
    uint32_t cpu_cycles;
    uint8_t parsed_contacts;
} SynaTouchFrame;

extern const uint8_t SynaNx809jReportConfig[];
extern const size_t SynaNx809jReportConfigLength;
extern const uint8_t SynaNx809jStockReportConfig[];
extern const size_t SynaNx809jStockReportConfigLength;

int SynaTcmBuildCommand(uint8_t command,
                        const uint8_t *payload,
                        uint16_t payload_length,
                        uint8_t *wire,
                        size_t wire_capacity,
                        size_t *wire_length);

int SynaTcmDecodeMessage(const uint8_t *wire,
                         size_t wire_length,
                         SynaTcmMessage *message);

int SynaTcmParseIdentification(const uint8_t *payload,
                               size_t payload_length,
                               SynaIdentification *identification);

int SynaTcmParseApplicationInfo(const uint8_t *payload,
                                size_t payload_length,
                                SynaApplicationInfo *application_info);

int SynaTcmGetBits(const uint8_t *report,
                   size_t report_length,
                   size_t bit_offset,
                   uint8_t bit_count,
                   uint32_t *value);

int SynaTcmParseTouchReport(const uint8_t *config,
                            size_t config_length,
                            const uint8_t *report,
                            size_t report_length,
                            uint8_t max_contacts,
                            SynaTouchFrame *frame);

#ifdef __cplusplus
}
#endif

#endif

/* SPDX-License-Identifier: GPL-2.0-only */
#include "syna_tcm.h"

#include <string.h>

const uint8_t SynaNx809jReportConfig[] = {
    SYNA_TOUCH_NUM_OF_ACTIVE_OBJECTS, 4,
    SYNA_TOUCH_PAD_TO_NEXT_BYTE,
    SYNA_TOUCH_FOREACH_ACTIVE_OBJECT,
        SYNA_TOUCH_OBJECT_N_INDEX, 4,
        SYNA_TOUCH_OBJECT_N_CLASSIFICATION, 4,
        SYNA_TOUCH_OBJECT_N_X_POSITION, 12,
        SYNA_TOUCH_OBJECT_N_Y_POSITION, 12,
        SYNA_TOUCH_OBJECT_N_Z, 8,
        SYNA_TOUCH_OBJECT_N_X_WIDTH, 8,
        SYNA_TOUCH_OBJECT_N_Y_WIDTH, 8,
    SYNA_TOUCH_FOREACH_END,
    SYNA_TOUCH_END
};

const size_t SynaNx809jReportConfigLength = sizeof(SynaNx809jReportConfig);

/* Exact report format recovered from the stock NX809J zte_tpd module. */
const uint8_t SynaNx809jStockReportConfig[] = {
    SYNA_TOUCH_SKIP_BITS, 0x70,
    SYNA_TOUCH_GESTURE_DOUBLE_TAP, 8,
    SYNA_TOUCH_NUM_OF_ACTIVE_OBJECTS, 8,
    SYNA_TOUCH_FOREACH_ACTIVE_OBJECT,
        SYNA_TOUCH_OBJECT_N_INDEX, 4,
        SYNA_TOUCH_OBJECT_N_CLASSIFICATION, 4,
        SYNA_TOUCH_OBJECT_N_X_POSITION, 16,
        SYNA_TOUCH_OBJECT_N_Y_POSITION, 16,
        SYNA_TOUCH_OBJECT_N_X_WIDTH, 12,
        SYNA_TOUCH_OBJECT_N_Y_WIDTH, 12,
    SYNA_TOUCH_FOREACH_END,
    SYNA_TOUCH_END
};

const size_t SynaNx809jStockReportConfigLength =
    sizeof(SynaNx809jStockReportConfig);

static uint16_t
SynaReadLe16(const uint8_t *value)
{
    return (uint16_t)((uint16_t)value[0] | ((uint16_t)value[1] << 8));
}

static uint32_t
SynaReadLe32(const uint8_t *value)
{
    return ((uint32_t)value[0]) |
           ((uint32_t)value[1] << 8) |
           ((uint32_t)value[2] << 16) |
           ((uint32_t)value[3] << 24);
}

int
SynaTcmBuildCommand(uint8_t command,
                    const uint8_t *payload,
                    uint16_t payload_length,
                    uint8_t *wire,
                    size_t wire_capacity,
                    size_t *wire_length)
{
    size_t required = (size_t)payload_length + 3u;

    if (wire == NULL || wire_length == NULL ||
        (payload_length != 0u && payload == NULL)) {
        return SYNA_RESULT_INVALID_ARGUMENT;
    }

    if (wire_capacity < required) {
        return SYNA_RESULT_BUFFER_TOO_SMALL;
    }

    wire[0] = command;
    wire[1] = (uint8_t)(payload_length & 0xffu);
    wire[2] = (uint8_t)(payload_length >> 8);
    if (payload_length != 0u) {
        memcpy(&wire[3], payload, payload_length);
    }
    *wire_length = required;
    return SYNA_RESULT_OK;
}

int
SynaTcmDecodeMessage(const uint8_t *wire,
                     size_t wire_length,
                     SynaTcmMessage *message)
{
    uint16_t payload_length;
    size_t required;

    if (wire == NULL || message == NULL) {
        return SYNA_RESULT_INVALID_ARGUMENT;
    }
    if (wire_length < SYNA_TCM_MESSAGE_HEADER_SIZE + 1u) {
        return SYNA_RESULT_TRUNCATED;
    }
    if (wire[0] != SYNA_TCM_MESSAGE_MARKER) {
        return SYNA_RESULT_BAD_MESSAGE;
    }

    payload_length = SynaReadLe16(&wire[2]);
    required = SYNA_TCM_MESSAGE_HEADER_SIZE + (size_t)payload_length + 1u;
    if (wire_length < required) {
        return SYNA_RESULT_TRUNCATED;
    }
    if (wire[required - 1u] != SYNA_TCM_MESSAGE_PADDING) {
        return SYNA_RESULT_BAD_MESSAGE;
    }

    message->code = wire[1];
    message->payload = &wire[SYNA_TCM_MESSAGE_HEADER_SIZE];
    message->payload_length = payload_length;
    message->wire_length = required;
    return SYNA_RESULT_OK;
}

int
SynaTcmParseIdentification(const uint8_t *payload,
                           size_t payload_length,
                           SynaIdentification *identification)
{
    if (payload == NULL || identification == NULL) {
        return SYNA_RESULT_INVALID_ARGUMENT;
    }
    if (payload_length < 24u) {
        return SYNA_RESULT_TRUNCATED;
    }

    memset(identification, 0, sizeof(*identification));
    identification->version = payload[0];
    identification->mode = payload[1];
    memcpy(identification->part_number, &payload[2], 16u);
    identification->part_number[16] = '\0';
    identification->build_id = SynaReadLe32(&payload[18]);
    identification->max_write_size = SynaReadLe16(&payload[22]);
    return SYNA_RESULT_OK;
}

int
SynaTcmParseApplicationInfo(const uint8_t *payload,
                            size_t payload_length,
                            SynaApplicationInfo *application_info)
{
    if (payload == NULL || application_info == NULL) {
        return SYNA_RESULT_INVALID_ARGUMENT;
    }
    if (payload_length < 38u) {
        return SYNA_RESULT_TRUNCATED;
    }

    memset(application_info, 0, sizeof(*application_info));
    application_info->version = SynaReadLe16(&payload[0]);
    application_info->status = SynaReadLe16(&payload[2]);
    application_info->max_touch_report_config_size = SynaReadLe16(&payload[12]);
    application_info->max_touch_report_payload_size = SynaReadLe16(&payload[14]);
    application_info->max_x = SynaReadLe16(&payload[32]);
    application_info->max_y = SynaReadLe16(&payload[34]);
    application_info->max_objects = SynaReadLe16(&payload[36]);
    return SYNA_RESULT_OK;
}

int
SynaTcmGetBits(const uint8_t *report,
               size_t report_length,
               size_t bit_offset,
               uint8_t bit_count,
               uint32_t *value)
{
    uint32_t output = 0;
    uint8_t output_shift = 0;

    if (report == NULL || value == NULL || bit_count == 0u || bit_count > 32u) {
        return SYNA_RESULT_INVALID_ARGUMENT;
    }
    if (bit_offset > report_length * 8u ||
        (size_t)bit_count > report_length * 8u - bit_offset) {
        return SYNA_RESULT_TRUNCATED;
    }

    while (bit_count != 0u) {
        size_t byte_offset = bit_offset / 8u;
        uint8_t in_byte_offset = (uint8_t)(bit_offset % 8u);
        uint8_t available = (uint8_t)(8u - in_byte_offset);
        uint8_t take = bit_count < available ? bit_count : available;
        uint8_t mask = (uint8_t)((1u << take) - 1u);
        uint8_t part = (uint8_t)((report[byte_offset] >> in_byte_offset) & mask);

        output |= ((uint32_t)part << output_shift);
        output_shift = (uint8_t)(output_shift + take);
        bit_offset += take;
        bit_count = (uint8_t)(bit_count - take);
    }

    *value = output;
    return SYNA_RESULT_OK;
}

static int
SynaFindForeachEnd(const uint8_t *config,
                   size_t config_length,
                   size_t start,
                   size_t *after_end)
{
    size_t index = start;
    unsigned int depth = 1u;

    while (index < config_length) {
        uint8_t code = config[index++];
        if (code == SYNA_TOUCH_FOREACH_ACTIVE_OBJECT ||
            code == SYNA_TOUCH_FOREACH_OBJECT) {
            ++depth;
        } else if (code == SYNA_TOUCH_FOREACH_END) {
            if (--depth == 0u) {
                *after_end = index;
                return SYNA_RESULT_OK;
            }
        } else if (code == SYNA_TOUCH_END) {
            return SYNA_RESULT_MALFORMED_CONFIG;
        } else if (code != SYNA_TOUCH_PAD_TO_NEXT_BYTE) {
            if (index >= config_length) {
                return SYNA_RESULT_MALFORMED_CONFIG;
            }
            ++index;
        }
    }
    return SYNA_RESULT_MALFORMED_CONFIG;
}

static int
SynaReadField(const uint8_t *config,
              size_t config_length,
              size_t *config_index,
              const uint8_t *report,
              size_t report_length,
              size_t *bit_offset,
              uint32_t *value)
{
    uint8_t bits;
    int result;

    if (*config_index >= config_length) {
        return SYNA_RESULT_MALFORMED_CONFIG;
    }
    bits = config[(*config_index)++];
    result = SynaTcmGetBits(report, report_length, *bit_offset, bits, value);
    if (result != SYNA_RESULT_OK) {
        return result;
    }
    *bit_offset += bits;
    return SYNA_RESULT_OK;
}

int
SynaTcmParseTouchReport(const uint8_t *config,
                        size_t config_length,
                        const uint8_t *report,
                        size_t report_length,
                        uint8_t max_contacts,
                        SynaTouchFrame *frame)
{
    size_t config_index = 0;
    size_t bit_offset = 0;
    size_t loop_start = 0;
    size_t previous_loop_bit_offset = 0;
    uint32_t data = 0;
    uint32_t active_objects = 0;
    uint8_t object_index = 0;
    uint8_t loop_iteration = 0;
    uint8_t loop_limit = 0;
    uint8_t loop_active_only = 0;
    uint8_t in_loop = 0;
    uint8_t have_active_count = 0;
    int result;

    if (config == NULL || report == NULL || frame == NULL ||
        config_length == 0u || max_contacts == 0u ||
        max_contacts > SYNA_TCM_MAX_CONTACTS) {
        return SYNA_RESULT_INVALID_ARGUMENT;
    }

    memset(frame, 0, sizeof(*frame));
    while (config_index < config_length) {
        uint8_t code = config[config_index++];
        SynaContact *contact;

        switch (code) {
        case SYNA_TOUCH_END:
            return SYNA_RESULT_OK;

        case SYNA_TOUCH_FOREACH_ACTIVE_OBJECT:
        case SYNA_TOUCH_FOREACH_OBJECT:
            if (in_loop) {
                return SYNA_RESULT_UNSUPPORTED;
            }
            loop_active_only = (uint8_t)(code == SYNA_TOUCH_FOREACH_ACTIVE_OBJECT);
            if (loop_active_only && have_active_count && active_objects == 0u) {
                result = SynaFindForeachEnd(config, config_length,
                                            config_index, &config_index);
                if (result != SYNA_RESULT_OK) {
                    return result;
                }
                break;
            }
            in_loop = 1u;
            loop_start = config_index;
            loop_iteration = 0u;
            loop_limit = loop_active_only && have_active_count
                       ? (uint8_t)(active_objects > max_contacts
                                   ? max_contacts : active_objects)
                       : max_contacts;
            object_index = 0u;
            previous_loop_bit_offset = bit_offset;
            break;

        case SYNA_TOUCH_FOREACH_END:
            if (!in_loop) {
                return SYNA_RESULT_MALFORMED_CONFIG;
            }
            ++loop_iteration;
            if (loop_iteration < loop_limit &&
                (!loop_active_only || have_active_count ||
                 bit_offset < report_length * 8u)) {
                if (bit_offset == previous_loop_bit_offset) {
                    return SYNA_RESULT_MALFORMED_CONFIG;
                }
                previous_loop_bit_offset = bit_offset;
                object_index = loop_iteration;
                config_index = loop_start;
            } else {
                in_loop = 0u;
            }
            break;

        case SYNA_TOUCH_PAD_TO_NEXT_BYTE:
            bit_offset = (bit_offset + 7u) & ~(size_t)7u;
            if (bit_offset > report_length * 8u) {
                return SYNA_RESULT_TRUNCATED;
            }
            break;

        case SYNA_TOUCH_SKIP_BITS:
            if (config_index >= config_length) {
                return SYNA_RESULT_MALFORMED_CONFIG;
            }
            bit_offset += config[config_index++];
            if (bit_offset > report_length * 8u) {
                return SYNA_RESULT_TRUNCATED;
            }
            break;

        case SYNA_TOUCH_OBJECT_N_INDEX:
            result = SynaReadField(config, config_length, &config_index,
                                   report, report_length, &bit_offset, &data);
            if (result != SYNA_RESULT_OK) return result;
            object_index = (uint8_t)data;
            if (object_index >= max_contacts) {
                return SYNA_RESULT_OUT_OF_RANGE;
            }
            frame->contacts[object_index].index = object_index;
            if (frame->parsed_contacts <= object_index) {
                frame->parsed_contacts = (uint8_t)(object_index + 1u);
            }
            break;

        case SYNA_TOUCH_OBJECT_N_CLASSIFICATION:
        case SYNA_TOUCH_OBJECT_N_X_POSITION:
        case SYNA_TOUCH_OBJECT_N_Y_POSITION:
        case SYNA_TOUCH_OBJECT_N_Z:
        case SYNA_TOUCH_OBJECT_N_X_WIDTH:
        case SYNA_TOUCH_OBJECT_N_Y_WIDTH:
        case SYNA_TOUCH_OBJECT_N_TX_POSITION_TIXELS:
        case SYNA_TOUCH_OBJECT_N_RX_POSITION_TIXELS:
            if (object_index >= max_contacts) {
                return SYNA_RESULT_OUT_OF_RANGE;
            }
            result = SynaReadField(config, config_length, &config_index,
                                   report, report_length, &bit_offset, &data);
            if (result != SYNA_RESULT_OK) return result;
            contact = &frame->contacts[object_index];
            contact->index = object_index;
            if (frame->parsed_contacts <= object_index) {
                frame->parsed_contacts = (uint8_t)(object_index + 1u);
            }
            if (code == SYNA_TOUCH_OBJECT_N_CLASSIFICATION) contact->classification = (uint8_t)data;
            else if (code == SYNA_TOUCH_OBJECT_N_X_POSITION) contact->x = data;
            else if (code == SYNA_TOUCH_OBJECT_N_Y_POSITION) contact->y = data;
            else if (code == SYNA_TOUCH_OBJECT_N_Z) contact->pressure = data;
            else if (code == SYNA_TOUCH_OBJECT_N_X_WIDTH) contact->width_x = data;
            else if (code == SYNA_TOUCH_OBJECT_N_Y_WIDTH) contact->width_y = data;
            else if (code == SYNA_TOUCH_OBJECT_N_TX_POSITION_TIXELS) contact->tx_position = data;
            else contact->rx_position = data;
            break;

        case SYNA_TOUCH_TIMESTAMP:
        case SYNA_TOUCH_0D_BUTTONS_STATE:
        case SYNA_TOUCH_GESTURE_DOUBLE_TAP:
        case SYNA_TOUCH_FRAME_RATE:
        case SYNA_TOUCH_POWER_IM:
        case SYNA_TOUCH_CID_IM:
        case SYNA_TOUCH_RAIL_IM:
        case SYNA_TOUCH_CID_VARIANCE_IM:
        case SYNA_TOUCH_NSM_FREQUENCY:
        case SYNA_TOUCH_NSM_STATE:
        case SYNA_TOUCH_NUM_OF_ACTIVE_OBJECTS:
        case SYNA_TOUCH_NUM_OF_CPU_CYCLES_USED_SINCE_LAST_FRAME:
            result = SynaReadField(config, config_length, &config_index,
                                   report, report_length, &bit_offset, &data);
            if (result != SYNA_RESULT_OK) return result;
            if (code == SYNA_TOUCH_TIMESTAMP) frame->timestamp = data;
            else if (code == SYNA_TOUCH_0D_BUTTONS_STATE) frame->buttons_state = data;
            else if (code == SYNA_TOUCH_GESTURE_DOUBLE_TAP) frame->gesture_double_tap = data;
            else if (code == SYNA_TOUCH_FRAME_RATE) frame->frame_rate = data;
            else if (code == SYNA_TOUCH_NUM_OF_ACTIVE_OBJECTS) {
                active_objects = data;
                frame->active_objects = data;
                have_active_count = 1u;
            } else if (code == SYNA_TOUCH_NUM_OF_CPU_CYCLES_USED_SINCE_LAST_FRAME) {
                frame->cpu_cycles = data;
            }
            break;

        case SYNA_TOUCH_TUNING_GAUSSIAN_WIDTHS:
        case SYNA_TOUCH_TUNING_SMALL_OBJECT_PARAMS:
        case SYNA_TOUCH_TUNING_0D_BUTTONS_VARIANCE:
            result = SynaReadField(config, config_length, &config_index,
                                   report, report_length, &bit_offset, &data);
            if (result != SYNA_RESULT_OK) return result;
            break;

        default:
            return SYNA_RESULT_UNSUPPORTED;
        }
    }

    return SYNA_RESULT_MALFORMED_CONFIG;
}

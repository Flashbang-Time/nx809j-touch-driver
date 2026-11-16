/* SPDX-License-Identifier: GPL-2.0-only */
#include "syna_hid.h"

#include <string.h>

static uint16_t
SynaClamp16(uint32_t value, uint16_t maximum)
{
    return value > maximum ? maximum : (uint16_t)value;
}

void
SynaHidBuildTouchReport(const SynaTouchFrame *frame,
                        uint16_t logical_max_x,
                        uint16_t logical_max_y,
                        NxHidTouchReport *report)
{
    uint8_t source_index;
    uint8_t destination_index = 0;

    memset(report, 0, sizeof(*report));
    report->report_id = NX809J_TOUCH_REPORT_ID;
    report->scan_time = (uint16_t)(frame->timestamp & 0xffffu);

    for (source_index = 0;
         source_index < SYNA_TCM_MAX_CONTACTS &&
         destination_index < SYNA_TCM_MAX_CONTACTS;
         ++source_index) {
        const SynaContact *source = &frame->contacts[source_index];
        NxHidContact *destination;

        /* S3910V reports 1 for finger and 2 for gloved finger. */
        if (source->classification != 1u && source->classification != 2u) {
            continue;
        }

        destination = &report->contacts[destination_index++];
        destination->flags = NX_TOUCH_FLAG_TIP_SWITCH | NX_TOUCH_FLAG_CONFIDENCE;
        destination->contact_id = source->index;
        destination->x = SynaClamp16(source->x, logical_max_x);
        destination->y = SynaClamp16(source->y, logical_max_y);
    }

    report->contact_count = destination_index;
}

void
SynaHidAppendReleases(const NxHidTouchReport *previous, NxHidTouchReport *report)
{
    unsigned int active_ids = 0;
    uint8_t i;

    for (i = 0; i < report->contact_count; ++i) {
        active_ids |= 1u << report->contacts[i].contact_id;
    }
    for (i = 0; i < previous->contact_count &&
                report->contact_count < SYNA_TCM_MAX_CONTACTS; ++i) {
        const NxHidContact *old = &previous->contacts[i];
        NxHidContact *released;

        if ((active_ids & (1u << old->contact_id)) != 0u) {
            continue;
        }
        released = &report->contacts[report->contact_count++];
        *released = *old;
        released->flags &= (uint8_t)~NX_TOUCH_FLAG_TIP_SWITCH;
    }
}

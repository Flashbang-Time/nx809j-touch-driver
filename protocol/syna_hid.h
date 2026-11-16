/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef NX809J_SYNA_HID_H
#define NX809J_SYNA_HID_H

#include "syna_tcm.h"

#define NX809J_TOUCH_REPORT_ID       1u
#define NX809J_MAX_COUNT_REPORT_ID   2u
#define NX809J_LOGICAL_MAX_X         1215u
#define NX809J_LOGICAL_MAX_Y         2687u

#define NX_TOUCH_FLAG_TIP_SWITCH     0x01u
#define NX_TOUCH_FLAG_CONFIDENCE     0x02u

#pragma pack(push, 1)
typedef struct NxHidContact {
    uint8_t flags;
    uint8_t contact_id;
    uint16_t x;
    uint16_t y;
} NxHidContact;

typedef struct NxHidTouchReport {
    uint8_t report_id;
    NxHidContact contacts[SYNA_TCM_MAX_CONTACTS];
    uint16_t scan_time;
    uint8_t contact_count;
} NxHidTouchReport;

typedef struct NxHidMaximumCountReport {
    uint8_t report_id;
    uint8_t maximum_count;
} NxHidMaximumCountReport;
#pragma pack(pop)

void SynaHidBuildTouchReport(const SynaTouchFrame *frame,
                             uint16_t logical_max_x,
                             uint16_t logical_max_y,
                             NxHidTouchReport *report);
void SynaHidAppendReleases(const NxHidTouchReport *previous,
                          NxHidTouchReport *report);

#endif

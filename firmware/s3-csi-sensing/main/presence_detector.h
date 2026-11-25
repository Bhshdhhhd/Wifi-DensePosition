#ifndef PRESENCE_DETECTOR_H
#define PRESENCE_DETECTOR_H

#include <stdbool.h>
#include <stdint.h>

#define PD_MAX_SUBCARRIERS 306
#define PD_CALIBRATION_FRAMES 300

typedef struct {
    float mean[PD_MAX_SUBCARRIERS];
    float m2[PD_MAX_SUBCARRIERS];
    uint16_t count[PD_MAX_SUBCARRIERS];

    uint16_t frames_collected;
    uint16_t valid_bins;

    float score;
    bool calibrated;
    bool present;

    uint8_t enter_count;
    uint8_t exit_count;
} presence_detector_t;

void presence_detector_init(presence_detector_t *detector);

void presence_detector_update(
    presence_detector_t *detector,
    const uint8_t *csi,
    uint16_t len,
    bool first_word_invalid
);

#endif

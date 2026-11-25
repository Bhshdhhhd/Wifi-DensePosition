#include "presence_detector.h"

#include <math.h>
#include <string.h>
#include <stdlib.h>

void presence_detector_init(presence_detector_t *detector)
{
    memset(detector, 0, sizeof(*detector));
}

/*
 * Convert each complex CSI pair into magnitude:
 *
 * magnitude = sqrt(I^2 + Q^2)
 *
 * First 300 frames are used to learn the empty-room baseline.
 */
void presence_detector_update(
    presence_detector_t *detector,
    const uint8_t *csi,
    uint16_t len,
    bool first_word_invalid
)
{
    if (detector == NULL || csi == NULL) {
        return;
    }

    uint16_t start = first_word_invalid ? 4 : 0;

    if (len <= start) {
        return;
    }

    uint16_t bins = (len - start) / 2;

    if (bins > PD_MAX_SUBCARRIERS) {
        bins = PD_MAX_SUBCARRIERS;
    }

    float magnitudes[PD_MAX_SUBCARRIERS];

    for (uint16_t b = 0; b < bins; b++) {
        uint16_t i = start + b * 2;

        int16_t imag = (int8_t)csi[i];
        int16_t real = (int8_t)csi[i + 1];

        magnitudes[b] = sqrtf(
            (float)(real * real + imag * imag)
        );
    }

    /*
     * Phase 1: Empty-room calibration.
     * Welford's algorithm calculates mean and variance
     * independently for every CSI subcarrier.
     */
    if (!detector->calibrated) {
        for (uint16_t b = 0; b < bins; b++) {
            uint16_t n = ++detector->count[b];

            float delta = magnitudes[b] - detector->mean[b];

            detector->mean[b] += delta / (float)n;

            float delta2 = magnitudes[b] - detector->mean[b];

            detector->m2[b] += delta * delta2;
        }

        detector->frames_collected++;

        if (detector->frames_collected >= PD_CALIBRATION_FRAMES) {
            detector->valid_bins = 0;

            for (uint16_t b = 0; b < bins; b++) {
                if (detector->mean[b] >= 2.0f &&
                    detector->count[b] >= PD_CALIBRATION_FRAMES) {
                    detector->valid_bins++;
                }
            }

            detector->calibrated = true;
        }

        return;
    }

    /*
     * Phase 2: Compare current CSI magnitudes against
     * the empty-room baseline.
     *
     * The score is the average normalized deviation
     * over the usable subcarriers.
     */
    float total_score = 0.0f;
    uint16_t used_bins = 0;

    for (uint16_t b = 0; b < bins; b++) {
        if (detector->count[b] < PD_CALIBRATION_FRAMES ||
            detector->mean[b] < 2.0f) {
            continue;
        }

        float variance =
            detector->m2[b] / (float)(detector->count[b] - 1);

        float stddev = sqrtf(variance);

        /* Prevent nearly constant bins from dominating the score. */
        if (stddev < 2.0f) {
            stddev = 2.0f;
        }

        float deviation = fabsf(
            magnitudes[b] - detector->mean[b]
        );

        total_score += deviation / stddev;
        used_bins++;
    }

    if (used_bins == 0) {
        detector->score = 0.0f;
        return;
    }

    detector->score = total_score / (float)used_bins;

    /*
     * Initial hysteresis thresholds.
     * These are experimental and need validation on the actual setup.
     */
    if (detector->score >= 2.2f) {
        if (detector->enter_count < 10) {
            detector->enter_count++;
        }

        detector->exit_count = 0;

        if (detector->enter_count >= 10) {
            detector->present = true;
        }
    }
    else if (detector->score <= 1.4f) {
        if (detector->exit_count < 50) {
            detector->exit_count++;
        }

        detector->enter_count = 0;

        if (detector->exit_count >= 50) {
            detector->present = false;
        }
    }
    else {
        detector->enter_count = 0;
        detector->exit_count = 0;
    }
}

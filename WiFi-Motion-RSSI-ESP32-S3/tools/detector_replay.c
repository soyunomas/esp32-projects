#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "motion_detector.h"

static bool parse_profile(const char *name, motion_sensitivity_profile_t *value)
{
    for (int candidate = 0; candidate < MOTION_PROFILE_COUNT; ++candidate) {
        if (strcmp(name,
                   motion_sensitivity_profile_name(
                       (motion_sensitivity_profile_t)candidate)) == 0) {
            *value = (motion_sensitivity_profile_t)candidate;
            return true;
        }
    }
    return false;
}

static bool parse_algorithm(const char *name, motion_feature_algorithm_t *value)
{
    for (int candidate = 0; candidate < MOTION_FEATURE_COUNT; ++candidate) {
        if (strcmp(name,
                   motion_feature_algorithm_name(
                       (motion_feature_algorithm_t)candidate)) == 0) {
            *value = (motion_feature_algorithm_t)candidate;
            return true;
        }
    }
    return false;
}

static bool parse_baseline(const char *name, motion_baseline_mode_t *value)
{
    for (int candidate = 0; candidate < MOTION_BASELINE_COUNT; ++candidate) {
        if (strcmp(name,
                   motion_baseline_mode_name(
                       (motion_baseline_mode_t)candidate)) == 0) {
            *value = (motion_baseline_mode_t)candidate;
            return true;
        }
    }
    return false;
}

static bool parse_size(const char *text, size_t *value)
{
    errno = 0;
    char *end = NULL;
    const unsigned long parsed = strtoul(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0') {
        return false;
    }
    *value = (size_t)parsed;
    return (unsigned long)*value == parsed;
}

static bool parse_positive_float(const char *text, float *value)
{
    errno = 0;
    char *end = NULL;
    const float parsed = strtof(text, &end);
    if (errno != 0 || end == text || *end != '\0' ||
        !isfinite(parsed) || parsed <= 0.0f) {
        return false;
    }
    *value = parsed;
    return true;
}

int main(int argc, char **argv)
{
    if (argc != 4 && argc != 7) {
        fprintf(stderr,
                "usage: %s PROFILE ALGORITHM BASELINE_MODE "
                "[WINDOW SIGMA MIN_THRESHOLD]\n",
                argv[0]);
        return 2;
    }

    motion_sensitivity_profile_t profile;
    motion_feature_algorithm_t algorithm;
    motion_baseline_mode_t baseline;
    if (!parse_profile(argv[1], &profile) ||
        !parse_algorithm(argv[2], &algorithm) ||
        !parse_baseline(argv[3], &baseline)) {
        fputs("invalid detector selection\n", stderr);
        return 2;
    }

    motion_detector_config_t config;
    if (!motion_detector_config_for_profile(&config,
                                             profile,
                                             algorithm,
                                             baseline)) {
        fputs("invalid detector configuration\n", stderr);
        return 2;
    }
    if (argc == 7 &&
        (!parse_size(argv[4], &config.window_size) ||
         !parse_positive_float(argv[5], &config.sigma_multiplier) ||
         !parse_positive_float(argv[6], &config.minimum_threshold))) {
        fputs("invalid detector overrides\n", stderr);
        return 2;
    }

    motion_detector_t detector;
    if (!motion_detector_init(&detector, &config)) {
        fputs("invalid detector configuration\n", stderr);
        return 2;
    }

    puts("state,score,threshold,release_threshold,baseline,baseline_spread,calibrated,transition");
    char line[64];
    while (fgets(line, sizeof(line), stdin) != NULL) {
        errno = 0;
        char *end = NULL;
        const float sample = strtof(line, &end);
        if (errno != 0 || end == line ||
            strspn(end, "\r\n\t ") != strlen(end) || !isfinite(sample)) {
            fputs("invalid detector sample\n", stderr);
            return 2;
        }
        const motion_detector_result_t result =
            motion_detector_push(&detector, sample);
        printf("%s,%.9g,%.9g,%.9g,%.9g,%.9g,%d,%d\n",
               motion_detector_state_name(result.state),
               result.score,
               result.threshold,
               result.release_threshold,
               result.baseline_mean,
               result.baseline_stddev,
               result.calibrated ? 1 : 0,
               result.state_changed ? 1 : 0);
    }
    return ferror(stdin) ? 1 : 0;
}

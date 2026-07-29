#include "multiref_detector.h"

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define REPLAY_LINE_SIZE 2048U

typedef struct {
    uint32_t scans;
    uint32_t score_ready;
    uint32_t transitions;
    uint32_t state_counts[6];
    uint16_t maximum_score_x100;
} replay_summary_t;

static bool parse_integer_field(const char *line,
                                const char *field,
                                long *value)
{
    const char *position = strstr(line, field);
    if (position == NULL) {
        return false;
    }
    position += strlen(field);
    char *end = NULL;
    errno = 0;
    const long parsed = strtol(position, &end, 10);
    if (errno != 0 || end == position) {
        return false;
    }
    *value = parsed;
    return true;
}

static bool parse_bssid(const char *text, uint8_t bssid[6])
{
    unsigned values[6];
    int consumed = 0;
    if (sscanf(text,
               "%2x:%2x:%2x:%2x:%2x:%2x%n",
               &values[0],
               &values[1],
               &values[2],
               &values[3],
               &values[4],
               &values[5],
               &consumed) != 6 ||
        consumed != 17) {
        return false;
    }
    for (size_t index = 0U; index < 6U; ++index) {
        bssid[index] = (uint8_t)values[index];
    }
    return true;
}

static bool parse_bssid_field(const char *line, uint8_t bssid[6])
{
    const char *position = strstr(line, "\"bssid\":\"");
    return position != NULL &&
           parse_bssid(position + strlen("\"bssid\":\""), bssid);
}

static void accumulate(replay_summary_t *summary,
                       const multiref_result_t *result)
{
    summary->scans++;
    if ((unsigned)result->state <
        sizeof(summary->state_counts) /
            sizeof(summary->state_counts[0])) {
        summary->state_counts[result->state]++;
    }
    if (result->score_ready) {
        summary->score_ready++;
        if (result->score_x100 > summary->maximum_score_x100) {
            summary->maximum_score_x100 = result->score_x100;
        }
    }
    if (result->state_changed) {
        summary->transitions++;
    }
}

static bool parse_reference_arguments(int argc,
                                      char **argv,
                                      int first_reference,
                                      multiref_reference_t *references,
                                      size_t *reference_count)
{
    if (argc <= first_reference ||
        ((argc - first_reference) % 3) != 0) {
        return false;
    }
    *reference_count =
        (size_t)(argc - first_reference) / 3U;
    if (*reference_count > MULTIREF_DETECTOR_MAX_REFERENCES) {
        return false;
    }
    for (size_t index = 0U; index < *reference_count; ++index) {
        multiref_reference_t *reference = &references[index];
        const size_t argument =
            (size_t)first_reference + index * 3U;
        const char *bssid_text = argv[argument];
        char *median_end = NULL;
        char *mad_end = NULL;
        const long median = strtol(
            argv[argument + 1U], &median_end, 10);
        const unsigned long mad = strtoul(
            argv[argument + 2U], &mad_end, 10);
        if (!parse_bssid(bssid_text, reference->bssid) ||
            median_end == argv[argument + 1U] ||
            *median_end != '\0' || median < INT16_MIN ||
            median > INT16_MAX ||
            mad_end == argv[argument + 2U] ||
            *mad_end != '\0' || mad > UINT16_MAX) {
            return false;
        }
        reference->baseline_rssi_x10 = (int16_t)median;
        reference->baseline_mad_x10 = (uint16_t)mad;
    }
    return true;
}

int main(int argc, char **argv)
{
    if (argc < 8 || strcmp(argv[1], "--input") != 0) {
        fprintf(stderr,
                "usage: %s --input capture.jsonl trigger_x100 release_x100 "
                "BSSID median_x10 mad_x10 [BSSID median_x10 mad_x10 ...]\n",
                argv[0]);
        return 2;
    }
    char *trigger_end = NULL;
    char *release_end = NULL;
    const unsigned long trigger =
        strtoul(argv[3], &trigger_end, 10);
    const unsigned long release =
        strtoul(argv[4], &release_end, 10);
    if (trigger_end == argv[3] || *trigger_end != '\0' ||
        release_end == argv[4] || *release_end != '\0' ||
        trigger == 0U || trigger > UINT16_MAX ||
        release >= trigger || release > UINT16_MAX) {
        fprintf(stderr, "invalid trigger or release score\n");
        return 2;
    }
    multiref_reference_t
        references[MULTIREF_DETECTOR_MAX_REFERENCES] = {0};
    size_t reference_count = 0U;
    if (!parse_reference_arguments(
            argc, argv, 5, references, &reference_count)) {
        fprintf(stderr, "invalid reference arguments\n");
        return 2;
    }
    const multiref_config_t config = {
        .minimum_coverage_permille = 600U,
        .noise_floor_x10 = 20U,
        .trigger_score_x100 = (uint16_t)trigger,
        .release_score_x100 = (uint16_t)release,
        .baseline_alpha_permille = 5U,
        .warmup_scans = 5U,
        .trigger_consecutive = 3U,
        .clear_consecutive = 5U,
        .unhealthy_consecutive = 2U,
        .recovery_consecutive = 3U,
        .stale_after_scans = 3U,
    };
    multiref_detector_t detector;
    if (multiref_detector_init(
            &detector, &config, references, reference_count) !=
        MULTIREF_OK) {
        fprintf(stderr, "detector initialization failed\n");
        return 2;
    }
    FILE *input = fopen(argv[2], "r");
    if (input == NULL) {
        perror(argv[2]);
        return 2;
    }

    replay_summary_t summary = {0};
    char line[REPLAY_LINE_SIZE];
    bool scan_open = false;
    uint32_t open_scan_id = 0U;
    while (fgets(line, sizeof(line), input) != NULL) {
        const bool ap = strstr(line, "\"type\":\"ap\"") != NULL;
        const bool scan = strstr(line, "\"type\":\"scan\"") != NULL;
        if (!ap && !scan) {
            continue;
        }
        long parsed_scan_id = 0;
        if (!parse_integer_field(
                line, "\"scan_id\":", &parsed_scan_id) ||
            parsed_scan_id <= 0 || parsed_scan_id > UINT32_MAX) {
            continue;
        }
        const uint32_t scan_id = (uint32_t)parsed_scan_id;
        if (!scan_open) {
            if (multiref_detector_begin_scan(
                    &detector, scan_id) != MULTIREF_OK) {
                fclose(input);
                return 3;
            }
            scan_open = true;
            open_scan_id = scan_id;
        }
        if (scan_id != open_scan_id) {
            fprintf(stderr, "incomplete or unordered scan %" PRIu32 "\n",
                    open_scan_id);
            fclose(input);
            return 3;
        }
        if (ap) {
            uint8_t bssid[6];
            long rssi = 0;
            if (parse_bssid_field(line, bssid) &&
                parse_integer_field(line, "\"rssi\":", &rssi) &&
                rssi >= INT8_MIN && rssi <= INT8_MAX) {
                const multiref_status_t status =
                    multiref_detector_observe(
                        &detector, bssid, (int8_t)rssi);
                if (status != MULTIREF_OK) {
                    fclose(input);
                    return 3;
                }
            }
        } else {
            multiref_result_t result;
            if (multiref_detector_end_scan(
                    &detector, &result) != MULTIREF_OK) {
                fclose(input);
                return 3;
            }
            accumulate(&summary, &result);
            scan_open = false;
        }
    }
    fclose(input);
    if (scan_open) {
        fprintf(stderr, "capture ends with incomplete scan\n");
        return 3;
    }
    printf(
        "{\"scans\":%" PRIu32 ",\"score_ready\":%" PRIu32 ","
        "\"transitions\":%" PRIu32 ",\"maximum_score_x100\":%u,"
        "\"states\":{\"CALIBRATING\":%" PRIu32 ","
        "\"WARMUP\":%" PRIu32 ",\"IDLE\":%" PRIu32 ","
        "\"MOTION\":%" PRIu32 ",\"DEGRADED\":%" PRIu32 ","
        "\"NO_DATA\":%" PRIu32 "}}\n",
        summary.scans,
        summary.score_ready,
        summary.transitions,
        summary.maximum_score_x100,
        summary.state_counts[MULTIREF_STATE_CALIBRATING],
        summary.state_counts[MULTIREF_STATE_WARMUP],
        summary.state_counts[MULTIREF_STATE_IDLE],
        summary.state_counts[MULTIREF_STATE_MOTION],
        summary.state_counts[MULTIREF_STATE_DEGRADED],
        summary.state_counts[MULTIREF_STATE_NO_DATA]);
    return 0;
}

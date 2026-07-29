#include <inttypes.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "event_marker.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "multiref_detector.h"
#include "maintenance_portal.h"
#include "observation_store.h"
#include "probe_config.h"
#include "probe_format.h"
#include "reference_selector.h"
#include "reference_store.h"
#include "scan_scheduler.h"
#include "sdkconfig.h"

#define PROBE_SCHEMA "wifi_ap_scan/v1"
#define PROBE_SCAN_TASK_STACK 6144U
#define PROBE_MARKER_TASK_STACK 3072U
#define PROBE_SCAN_QUEUE_LENGTH 2U
#define PROBE_TELEMETRY_LINE_SIZE 768U

#if CONFIG_PROBE_SCAN_PASSIVE
#define PROBE_SCAN_MODE_NAME "passive"
#define PROBE_DWELL_MIN_MS CONFIG_PROBE_PASSIVE_DWELL_MS
#define PROBE_DWELL_MAX_MS CONFIG_PROBE_PASSIVE_DWELL_MS
#else
#define PROBE_SCAN_MODE_NAME "active"
#define PROBE_DWELL_MIN_MS CONFIG_PROBE_ACTIVE_MIN_DWELL_MS
#define PROBE_DWELL_MAX_MS CONFIG_PROBE_ACTIVE_MAX_DWELL_MS
#endif

#if CONFIG_PROBE_LIMIT_CHANNELS
#define PROBE_CHANNEL_MODE_NAME "selected"
#else
#define PROBE_CHANNEL_MODE_NAME "all"
#endif

typedef struct {
    uint32_t status;
    uint16_t number;
    uint8_t scan_id;
} scan_done_message_t;

static const char *TAG = "ap_scan_probe";
static QueueHandle_t scan_done_queue;
static SemaphoreHandle_t telemetry_mutex;
static SemaphoreHandle_t marker_mutex;
static event_marker_t marker;
static atomic_uint_fast32_t dropped_scan_events;

static void status_led_write(bool on)
{
#if CONFIG_PROBE_STATUS_LED_GPIO >= 0
#if CONFIG_PROBE_STATUS_LED_ACTIVE_LOW
    const int level = on ? 0 : 1;
#else
    const int level = on ? 1 : 0;
#endif
    (void)gpio_set_level(
        (gpio_num_t)CONFIG_PROBE_STATUS_LED_GPIO, level);
#else
    (void)on;
#endif
}

static void update_status_led(multiref_state_t state, uint32_t scan_id)
{
    switch (state) {
    case MULTIREF_STATE_MOTION:
        status_led_write(true);
        break;
    case MULTIREF_STATE_WARMUP:
    case MULTIREF_STATE_CALIBRATING:
        status_led_write((scan_id & 1U) != 0U);
        break;
    case MULTIREF_STATE_DEGRADED:
        status_led_write((scan_id & 1U) == 0U);
        break;
    case MULTIREF_STATE_NO_DATA:
        status_led_write((scan_id % 4U) == 0U);
        break;
    case MULTIREF_STATE_IDLE:
    default:
        status_led_write(false);
        break;
    }
}

static void telemetry_emit(const char *format, ...)
{
    char line[PROBE_TELEMETRY_LINE_SIZE];
    va_list arguments;
    va_start(arguments, format);
    const int length = vsnprintf(line, sizeof(line), format, arguments);
    va_end(arguments);
    if (length < 0) {
        return;
    }

    if (xSemaphoreTake(telemetry_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        puts(line);
        xSemaphoreGive(telemetry_mutex);
    }
}

static event_marker_result_t marker_snapshot(void)
{
    event_marker_result_t result = {0};
    if (xSemaphoreTake(marker_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        result.active = marker.active;
        result.event_id = marker.event_id;
        xSemaphoreGive(marker_mutex);
    }
    return result;
}

static void wifi_scan_event_handler(void *argument,
                                    esp_event_base_t event_base,
                                    int32_t event_id,
                                    void *event_data)
{
    (void)argument;
    if (event_base != WIFI_EVENT || event_id != WIFI_EVENT_SCAN_DONE ||
        event_data == NULL) {
        return;
    }

    const wifi_event_sta_scan_done_t *event = event_data;
    const scan_done_message_t message = {
        .status = event->status,
        .number = event->number,
        .scan_id = event->scan_id,
    };
    if (xQueueSend(scan_done_queue, &message, 0) != pdTRUE) {
        atomic_fetch_add(&dropped_scan_events, 1U);
    }
}

static wifi_scan_config_t make_scan_config(uint8_t channel)
{
    wifi_scan_config_t config = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = channel,
        .show_hidden = true,
    };
#if CONFIG_PROBE_SCAN_PASSIVE
    config.scan_type = WIFI_SCAN_TYPE_PASSIVE;
    config.scan_time.passive = CONFIG_PROBE_PASSIVE_DWELL_MS;
#else
    config.scan_type = WIFI_SCAN_TYPE_ACTIVE;
    config.scan_time.active.min = CONFIG_PROBE_ACTIVE_MIN_DWELL_MS;
    config.scan_time.active.max = CONFIG_PROBE_ACTIVE_MAX_DWELL_MS;
#endif
    return config;
}

static void emit_ap_record(uint32_t scan_id,
                           int64_t observed_us,
                           const wifi_ap_record_t *record,
                           const event_marker_result_t *marker_state)
{
    const size_t ssid_length =
        strnlen((const char *)record->ssid, sizeof(record->ssid));
    char ssid_text[(sizeof(record->ssid) * 6U) + 1U];
    char ssid_hex[(sizeof(record->ssid) * 2U) + 1U];
    char bssid[18];
    (void)probe_json_escape_bytes(ssid_text,
                                  sizeof(ssid_text),
                                  record->ssid,
                                  ssid_length);
    probe_bytes_to_hex(ssid_hex,
                       sizeof(ssid_hex),
                       record->ssid,
                       ssid_length);
    probe_format_bssid(bssid, record->bssid);

    telemetry_emit(
        "{\"schema\":\"" PROBE_SCHEMA "\",\"type\":\"ap\","
        "\"scan_id\":%" PRIu32 ",\"observed_us\":%" PRId64 ","
        "\"ssid\":\"%s\",\"ssid_hex\":\"%s\",\"bssid\":\"%s\","
        "\"rssi\":%d,\"channel\":%u,\"auth\":%d,"
        "\"marker_active\":%s,\"event_id\":%" PRIu32 "}",
        scan_id,
        observed_us,
        ssid_text,
        ssid_hex,
        bssid,
        record->rssi,
        record->primary,
        record->authmode,
        marker_state->active ? "true" : "false",
        marker_state->event_id);
}

static void emit_scan_error(uint32_t scan_id,
                            int64_t started_us,
                            esp_err_t error,
                            const char *stage,
                            uint8_t channel)
{
    telemetry_emit(
        "{\"schema\":\"" PROBE_SCHEMA "\",\"type\":\"scan_error\","
        "\"scan_id\":%" PRIu32 ",\"started_us\":%" PRId64 ","
        "\"ended_us\":%" PRId64 ",\"stage\":\"%s\","
        "\"channel\":%u,\"esp_error\":%d,"
        "\"dropped_events\":%" PRIu32 "}",
        scan_id,
        started_us,
        esp_timer_get_time(),
        stage,
        channel,
        error,
        (uint32_t)atomic_load(&dropped_scan_events));
}

static scan_scheduler_t make_scan_scheduler(void)
{
    scan_scheduler_t scheduler;
#if CONFIG_PROBE_LIMIT_CHANNELS
    const uint8_t configured_channels[] = {
        CONFIG_PROBE_REFERENCE_CHANNEL_1,
        CONFIG_PROBE_REFERENCE_CHANNEL_2,
    };
    const size_t configured_count =
        configured_channels[0] == configured_channels[1] ? 1U : 2U;
#else
    const uint8_t configured_channels[] = {0U};
    const size_t configured_count = 1U;
#endif
    ESP_ERROR_CHECK(
        scan_scheduler_init(&scheduler,
                            configured_channels,
                            configured_count)
            ? ESP_OK
            : ESP_ERR_INVALID_ARG);
    return scheduler;
}

static bool merge_ap_record(wifi_ap_record_t *destination,
                            uint16_t *destination_count,
                            const wifi_ap_record_t *record)
{
    for (uint16_t index = 0U; index < *destination_count; ++index) {
        if (memcmp(destination[index].bssid,
                   record->bssid,
                   sizeof(record->bssid)) == 0) {
            if (record->rssi > destination[index].rssi) {
                destination[index] = *record;
            }
            return true;
        }
    }
    if (*destination_count >= CONFIG_PROBE_MAX_AP_RECORDS) {
        return false;
    }
    destination[*destination_count] = *record;
    (*destination_count)++;
    return true;
}

static const char *scheduler_mode(const scan_scheduler_t *scheduler)
{
    return scan_scheduler_channel_count(scheduler) == 1U &&
                   scan_scheduler_channel_at(scheduler, 0U) == 0U
               ? "all"
               : "selected";
}

static bool configured_manual_ssids(
                                    reference_ssid_t manual_ssids[
                                        PROBE_CONFIG_MAX_SSIDS],
                                    size_t *manual_ssid_count)
{
    probe_config_blob_t persisted = {0};
    const probe_config_status_t config_status =
        probe_config_load(&persisted);
    if (config_status == PROBE_CONFIG_OK) {
        *manual_ssid_count = persisted.ssid_count;
        memcpy(manual_ssids,
               persisted.ssids,
               persisted.ssid_count * sizeof(manual_ssids[0]));
        return true;
    }
    const char *configured[] = {
        CONFIG_PROBE_MANUAL_SSID_1,
        CONFIG_PROBE_MANUAL_SSID_2,
    };
    *manual_ssid_count = 0U;
    for (size_t index = 0U; index < 2U; ++index) {
        const size_t length =
            strnlen(configured[index],
                    REFERENCE_SELECTOR_SSID_MAX_LENGTH + 1U);
        if (length > REFERENCE_SELECTOR_SSID_MAX_LENGTH) {
            return false;
        }
        if (length == 0U) {
            continue;
        }
        reference_ssid_t *ssid =
            &manual_ssids[(*manual_ssid_count)++];
        memset(ssid, 0, sizeof(*ssid));
        memcpy(ssid->bytes, configured[index], length);
        ssid->length = (uint8_t)length;
    }
    return true;
}

static void emit_calibration_error(uint32_t scan_id,
                                   observation_store_status_t status,
                                   const char *stage)
{
    telemetry_emit(
        "{\"schema\":\"" PROBE_SCHEMA "\","
        "\"type\":\"calibration_error\","
        "\"scan_id\":%" PRIu32 ",\"stage\":\"%s\","
        "\"status\":%d}",
        scan_id,
        stage,
        status);
}

static void emit_reference_decision(
    const reference_candidate_t *candidate,
    const reference_decision_t *decision,
    const char *selection_mode);

static bool scheduler_from_selection(
    scan_scheduler_t *scheduler,
    const reference_candidate_t *candidates,
    const reference_decision_t *decisions,
    size_t candidate_count)
{
    uint8_t channels[SCAN_SCHEDULER_MAX_CHANNELS] = {0};
    size_t channel_count = 0U;
    for (size_t index = 0U; index < candidate_count; ++index) {
        if (!decisions[index].selected) {
            continue;
        }
        const uint8_t channel = candidates[index].channel;
        bool already_present = false;
        for (size_t existing = 0U;
             existing < channel_count;
             ++existing) {
            if (channels[existing] == channel) {
                already_present = true;
                break;
            }
        }
        if (!already_present &&
            channel_count < SCAN_SCHEDULER_MAX_CHANNELS) {
            channels[channel_count++] = channel;
        }
    }
    return channel_count > 0U &&
           scan_scheduler_init(scheduler, channels, channel_count);
}

static bool scheduler_from_store(scan_scheduler_t *scheduler,
                                 const reference_store_blob_t *blob)
{
    uint8_t channels[SCAN_SCHEDULER_MAX_CHANNELS] = {0};
    size_t channel_count = 0U;
    for (uint8_t index = 0U; index < blob->count; ++index) {
        const uint8_t channel = blob->entries[index].channel;
        bool already_present = false;
        for (size_t existing = 0U;
             existing < channel_count;
             ++existing) {
            if (channels[existing] == channel) {
                already_present = true;
                break;
            }
        }
        if (!already_present &&
            channel_count < SCAN_SCHEDULER_MAX_CHANNELS) {
            channels[channel_count++] = channel;
        }
    }
    return channel_count > 0U &&
           scan_scheduler_init(scheduler, channels, channel_count);
}

static void emit_stored_reference(const reference_store_entry_t *entry,
                                  uint8_t rank,
                                  const char *selection_mode)
{
    reference_candidate_t candidate = {
        .ssid = entry->ssid,
        .channel = entry->channel,
        .median_rssi_x10 = entry->median_rssi_x10,
        .mad_x10 = entry->mad_x10,
        .ssid_stable = true,
        .channel_stable = true,
    };
    memcpy(candidate.bssid, entry->bssid, sizeof(candidate.bssid));
    const reference_decision_t decision = {
        .selected = true,
        .rank = rank,
        .rejection_flags = REFERENCE_REJECT_NONE,
    };
    emit_reference_decision(&candidate, &decision, selection_mode);
}

static bool detector_from_store(multiref_detector_t *detector,
                                const reference_store_blob_t *blob)
{
    multiref_reference_t
        references[MULTIREF_DETECTOR_MAX_REFERENCES] = {0};
    for (uint8_t index = 0U; index < blob->count; ++index) {
        memcpy(references[index].bssid,
               blob->entries[index].bssid,
               sizeof(references[index].bssid));
        references[index].baseline_rssi_x10 =
            blob->entries[index].median_rssi_x10;
        references[index].baseline_mad_x10 =
            blob->entries[index].mad_x10;
    }
    const multiref_config_t config = {
        .minimum_coverage_permille =
            CONFIG_PROBE_DETECTOR_MINIMUM_COVERAGE_PERCENT * 10,
        .noise_floor_x10 =
            CONFIG_PROBE_DETECTOR_NOISE_FLOOR_DB_X10,
        .trigger_score_x100 =
            CONFIG_PROBE_DETECTOR_TRIGGER_SCORE_X100,
        .release_score_x100 =
            CONFIG_PROBE_DETECTOR_RELEASE_SCORE_X100,
        .adaptive_sigma_x100 =
            CONFIG_PROBE_DETECTOR_ADAPTIVE_SIGMA_X100,
        .baseline_alpha_permille =
            CONFIG_PROBE_DETECTOR_BASELINE_ALPHA_PERMILLE,
        .adaptive_window =
            CONFIG_PROBE_DETECTOR_ADAPTIVE_WINDOW,
        .warmup_scans = CONFIG_PROBE_DETECTOR_WARMUP_SCANS,
        .trigger_consecutive =
            CONFIG_PROBE_DETECTOR_TRIGGER_CONSECUTIVE,
        .clear_consecutive =
            CONFIG_PROBE_DETECTOR_CLEAR_CONSECUTIVE,
        .unhealthy_consecutive =
            CONFIG_PROBE_DETECTOR_UNHEALTHY_CONSECUTIVE,
        .recovery_consecutive =
            CONFIG_PROBE_DETECTOR_RECOVERY_CONSECUTIVE,
        .stale_after_scans =
            CONFIG_PROBE_DETECTOR_STALE_AFTER_SCANS,
    };
    return multiref_detector_init(
               detector, &config, references, blob->count) ==
           MULTIREF_OK;
}

static void emit_detector_result(uint32_t scan_id,
                                 const multiref_result_t *result)
{
    telemetry_emit(
        "{\"schema\":\"" PROBE_SCHEMA "\","
        "\"type\":\"detector\",\"scan_id\":%" PRIu32 ","
        "\"state\":\"%s\",\"state_changed\":%s,"
        "\"score_ready\":%s,\"score_x100\":%u,"
        "\"trigger_score_x100\":%u,"
        "\"release_score_x100\":%u,"
        "\"baseline_score_center_x100\":%u,"
        "\"baseline_score_spread_x100\":%u,"
        "\"trigger_count\":%u,\"trigger_required\":%u,"
        "\"coverage_permille\":%u,"
        "\"observed_references\":%u,\"reference_count\":%u,"
        "\"stale_references\":%u,"
        "\"oldest_reference_age_scans\":%u}",
        scan_id,
        multiref_state_name(result->state),
        result->state_changed ? "true" : "false",
        result->score_ready ? "true" : "false",
        result->score_x100,
        result->trigger_score_x100,
        result->release_score_x100,
        result->baseline_score_center_x100,
        result->baseline_score_spread_x100,
        result->trigger_count,
        result->trigger_required,
        result->coverage_permille,
        result->observed_references,
        result->reference_count,
        result->stale_references,
        result->oldest_reference_age_scans);
    if (result->state_changed &&
        (result->state == MULTIREF_STATE_MOTION ||
         result->previous_state == MULTIREF_STATE_MOTION)) {
        telemetry_emit(
            "{\"schema\":\"" PROBE_SCHEMA "\","
            "\"type\":\"motion_event\",\"scan_id\":%" PRIu32 ","
            "\"state\":\"%s\",\"score_x100\":%u,"
            "\"coverage_permille\":%u}",
            scan_id,
            result->state == MULTIREF_STATE_MOTION
                ? "started"
                : "cleared",
            result->score_x100,
            result->coverage_permille);
    }
}

static void emit_reference_decision(
    const reference_candidate_t *candidate,
    const reference_decision_t *decision,
    const char *selection_mode)
{
    char ssid_text[
        (REFERENCE_SELECTOR_SSID_MAX_LENGTH * 6U) + 1U];
    char ssid_hex[
        (REFERENCE_SELECTOR_SSID_MAX_LENGTH * 2U) + 1U];
    char bssid[18];
    (void)probe_json_escape_bytes(ssid_text,
                                  sizeof(ssid_text),
                                  candidate->ssid.bytes,
                                  candidate->ssid.length);
    probe_bytes_to_hex(ssid_hex,
                       sizeof(ssid_hex),
                       candidate->ssid.bytes,
                       candidate->ssid.length);
    probe_format_bssid(bssid, candidate->bssid);
    const uint16_t presence_permille =
        candidate->total_scans == 0U
            ? 0U
            : (uint16_t)((uint32_t)candidate->observed_scans *
                         1000U / candidate->total_scans);
    telemetry_emit(
        "{\"schema\":\"" PROBE_SCHEMA "\","
        "\"type\":\"reference\",\"selection_mode\":\"%s\","
        "\"ssid\":\"%s\",\"ssid_hex\":\"%s\","
        "\"bssid\":\"%s\",\"channel\":%u,"
        "\"samples\":%u,\"observed_scans\":%u,"
        "\"total_scans\":%u,\"presence_permille\":%u,"
        "\"median_rssi_x10\":%d,\"mad_x10\":%u,"
        "\"ssid_stable\":%s,\"channel_stable\":%s,"
        "\"selected\":%s,\"rank\":%u,"
        "\"rejection_flags\":%" PRIu32 "}",
        selection_mode,
        ssid_text,
        ssid_hex,
        bssid,
        candidate->channel,
        candidate->samples,
        candidate->observed_scans,
        candidate->total_scans,
        presence_permille,
        candidate->median_rssi_x10,
        candidate->mad_x10,
        candidate->ssid_stable ? "true" : "false",
        candidate->channel_stable ? "true" : "false",
        decision->selected ? "true" : "false",
        decision->rank,
        decision->rejection_flags);
}

static bool finish_calibration(observation_store_t *store,
                               scan_scheduler_t *scheduler,
                               uint32_t scan_id)
{
    static reference_candidate_t
        candidates[REFERENCE_SELECTOR_MAX_CANDIDATES];
    static reference_decision_t
        decisions[REFERENCE_SELECTOR_MAX_CANDIDATES];
    size_t candidate_count = 0U;
    observation_store_status_t store_status =
        observation_store_export_candidates(
            store,
            candidates,
            REFERENCE_SELECTOR_MAX_CANDIDATES,
            &candidate_count);
    if (store_status != OBSERVATION_STORE_OK) {
        emit_calibration_error(scan_id, store_status, "export");
        return false;
    }

    reference_ssid_t manual_ssids[PROBE_CONFIG_MAX_SSIDS] = {0};
    size_t manual_ssid_count = 0U;
    if (!configured_manual_ssids(manual_ssids,
                                 &manual_ssid_count)) {
        emit_calibration_error(scan_id,
                               OBSERVATION_STORE_INVALID_ARGUMENT,
                               "manual_ssid");
        return false;
    }
    const reference_selector_policy_t policy = {
        .minimum_samples =
            CONFIG_PROBE_SELECTOR_MINIMUM_SAMPLES,
        .minimum_presence_permille =
            CONFIG_PROBE_SELECTOR_MINIMUM_PRESENCE_PERCENT * 10,
        .minimum_rssi_x10 =
            CONFIG_PROBE_SELECTOR_MINIMUM_RSSI_DBM * 10,
        .maximum_mad_x10 =
            CONFIG_PROBE_SELECTOR_MAXIMUM_MAD_DB * 10,
        .maximum_references =
            CONFIG_PROBE_SELECTOR_MAXIMUM_REFERENCES,
    };
    size_t selected_count = 0U;
    const reference_selector_status_t selector_status =
        reference_selector_select(
            candidates,
            candidate_count,
            manual_ssids,
            manual_ssid_count,
            &policy,
            decisions,
            REFERENCE_SELECTOR_MAX_CANDIDATES,
            &selected_count);
    if (selector_status != REFERENCE_SELECTOR_OK) {
        telemetry_emit(
            "{\"schema\":\"" PROBE_SCHEMA "\","
            "\"type\":\"calibration_error\","
            "\"scan_id\":%" PRIu32 ",\"stage\":\"select\","
            "\"status\":%d}",
            scan_id,
            selector_status);
        return false;
    }

    const char *selection_mode =
        manual_ssid_count > 0U ? "manual" : "automatic";
    for (size_t index = 0U; index < candidate_count; ++index) {
        emit_reference_decision(
            &candidates[index], &decisions[index], selection_mode);
    }
    const bool has_selection =
        scheduler_from_selection(scheduler,
                                 candidates,
                                 decisions,
                                 candidate_count);
    reference_store_status_t persistence_status =
        REFERENCE_STORE_INVALID;
    if (has_selection) {
        reference_store_blob_t blob = {
            .manual_selection = manual_ssid_count > 0U ? 1U : 0U,
        };
        for (size_t index = 0U;
             index < candidate_count &&
             blob.count < REFERENCE_STORE_MAX_REFERENCES;
             ++index) {
            if (!decisions[index].selected) {
                continue;
            }
            reference_store_entry_t *entry =
                &blob.entries[blob.count++];
            memcpy(entry->bssid,
                   candidates[index].bssid,
                   sizeof(entry->bssid));
            entry->ssid = candidates[index].ssid;
            entry->channel = candidates[index].channel;
            entry->median_rssi_x10 =
                candidates[index].median_rssi_x10;
            entry->mad_x10 = candidates[index].mad_x10;
        }
        persistence_status = reference_store_save(&blob);
    } else {
        *scheduler = make_scan_scheduler();
    }
    telemetry_emit(
        "{\"schema\":\"" PROBE_SCHEMA "\","
        "\"type\":\"calibration\",\"state\":\"completed\","
        "\"scan_id\":%" PRIu32 ",\"selection_mode\":\"%s\","
        "\"candidate_count\":%u,\"selected_count\":%u,"
        "\"channel_mode\":\"%s\",\"channel_count\":%u,"
        "\"persistence_status\":%d}",
        scan_id,
        selection_mode,
        (unsigned)candidate_count,
        (unsigned)selected_count,
        scheduler_mode(scheduler),
        (unsigned)scan_scheduler_channel_count(scheduler),
        persistence_status);
    return true;
}

static void scan_task(void *argument)
{
    (void)argument;
    static wifi_ap_record_t records[CONFIG_PROBE_MAX_AP_RECORDS];
    static wifi_ap_record_t channel_records[CONFIG_PROBE_MAX_AP_RECORDS];
    static observation_store_t calibration_store;
    static multiref_detector_t detector;
    uint32_t scan_id = 0U;
    scan_scheduler_t scheduler;
    const uint8_t all_channels[] = {0U};
    ESP_ERROR_CHECK(
        scan_scheduler_init(&scheduler, all_channels, 1U)
            ? ESP_OK
            : ESP_ERR_INVALID_ARG);
    ESP_ERROR_CHECK(
        observation_store_init(
            &calibration_store,
            OBSERVATION_STORE_MAX_ENTRIES,
            CONFIG_PROBE_CALIBRATION_SCANS) ==
                OBSERVATION_STORE_OK
            ? ESP_OK
            : ESP_ERR_INVALID_ARG);
    reference_ssid_t manual_ssids[PROBE_CONFIG_MAX_SSIDS] = {0};
    size_t manual_ssid_count = 0U;
    ESP_ERROR_CHECK(
        configured_manual_ssids(manual_ssids,
                                 &manual_ssid_count)
            ? ESP_OK
            : ESP_ERR_INVALID_ARG);
    bool calibration_finished = false;
    bool calibration_warning_reported = false;
    bool detector_ready = false;
    reference_store_blob_t persisted = {0};
    reference_store_status_t persistence_status;
#if CONFIG_PROBE_FORCE_RECALIBRATION
    persistence_status = reference_store_erase();
    if (persistence_status == REFERENCE_STORE_OK) {
        persistence_status = REFERENCE_STORE_NOT_FOUND;
    }
#else
    persistence_status = reference_store_load(&persisted);
#endif
    if (persistence_status == REFERENCE_STORE_OK &&
        scheduler_from_store(&scheduler, &persisted)) {
        calibration_finished = true;
        detector_ready = detector_from_store(&detector, &persisted);
        const char *selection_mode =
            persisted.manual_selection ? "manual" : "automatic";
        for (uint8_t index = 0U; index < persisted.count; ++index) {
            emit_stored_reference(
                &persisted.entries[index], index + 1U, selection_mode);
        }
        telemetry_emit(
            "{\"schema\":\"" PROBE_SCHEMA "\","
            "\"type\":\"calibration\",\"state\":\"restored\","
            "\"selection_mode\":\"%s\",\"selected_count\":%u,"
            "\"channel_mode\":\"%s\",\"channel_count\":%u,"
            "\"persistence_status\":%d}",
            selection_mode,
            persisted.count,
            scheduler_mode(&scheduler),
            (unsigned)scan_scheduler_channel_count(&scheduler),
            persistence_status);
        if (!detector_ready) {
            telemetry_emit(
                "{\"schema\":\"" PROBE_SCHEMA "\","
                "\"type\":\"detector_error\","
                "\"stage\":\"restore_init\"}");
        }
    } else {
        telemetry_emit(
            "{\"schema\":\"" PROBE_SCHEMA "\","
            "\"type\":\"calibration\",\"state\":\"started\","
            "\"selection_mode\":\"%s\",\"target_scans\":%d,"
            "\"channel_mode\":\"all\",\"persistence_status\":%d}",
            manual_ssid_count > 0U ? "manual" : "automatic",
            CONFIG_PROBE_CALIBRATION_SCANS,
            persistence_status);
    }

    for (;;) {
        if (maintenance_portal_take_calibration_request()) {
            const uint8_t calibration_channels[] = {0U};
            ESP_ERROR_CHECK(
                scan_scheduler_init(
                    &scheduler, calibration_channels, 1U)
                    ? ESP_OK
                    : ESP_ERR_INVALID_ARG);
            ESP_ERROR_CHECK(
                observation_store_init(
                    &calibration_store,
                    OBSERVATION_STORE_MAX_ENTRIES,
                    CONFIG_PROBE_CALIBRATION_SCANS) ==
                        OBSERVATION_STORE_OK
                    ? ESP_OK
                    : ESP_ERR_INVALID_ARG);
            const reference_store_status_t erase_status =
                reference_store_erase();
            calibration_finished = false;
            calibration_warning_reported = false;
            detector_ready = false;
            maintenance_portal_update_calibration(
                true, 0U, CONFIG_PROBE_CALIBRATION_SCANS);
            update_status_led(MULTIREF_STATE_CALIBRATING, scan_id);
            telemetry_emit(
                "{\"schema\":\"" PROBE_SCHEMA "\","
                "\"type\":\"calibration\",\"state\":\"started\","
                "\"selection_mode\":\"%s\",\"target_scans\":%d,"
                "\"channel_mode\":\"all\",\"persistence_status\":%d,"
                "\"source\":\"portal\"}",
                manual_ssid_count > 0U ? "manual" : "automatic",
                CONFIG_PROBE_CALIBRATION_SCANS,
                erase_status);
        }
        scan_id++;
        const int64_t started_us = esp_timer_get_time();
        int64_t ended_us = started_us;
        uint16_t reported_count = 0U;
        uint16_t record_count = 0U;
        bool truncated = false;
        bool failed = false;
        scan_done_message_t done = {0};
        memset(records, 0, sizeof(records));
        for (size_t channel_index = 0U;
             channel_index < scan_scheduler_channel_count(&scheduler);
             ++channel_index) {
            const uint8_t channel =
                scan_scheduler_channel_at(&scheduler, channel_index);
            const wifi_scan_config_t scan_config =
                make_scan_config(channel);
            xQueueReset(scan_done_queue);
            esp_err_t error = esp_wifi_scan_start(&scan_config, false);
            if (error != ESP_OK) {
                emit_scan_error(
                    scan_id, started_us, error, "start", channel);
                failed = true;
                break;
            }
            if (xQueueReceive(scan_done_queue,
                              &done,
                              pdMS_TO_TICKS(CONFIG_PROBE_SCAN_TIMEOUT_MS)) !=
                pdTRUE) {
                emit_scan_error(scan_id,
                                started_us,
                                ESP_ERR_TIMEOUT,
                                "wait",
                                channel);
                (void)esp_wifi_scan_stop();
                (void)esp_wifi_clear_ap_list();
                failed = true;
                break;
            }
            ended_us = esp_timer_get_time();
            if (done.status != 0U) {
                emit_scan_error(scan_id,
                                started_us,
                                ESP_FAIL,
                                "result",
                                channel);
                (void)esp_wifi_clear_ap_list();
                failed = true;
                break;
            }

            uint16_t channel_reported_count = 0U;
            error = esp_wifi_scan_get_ap_num(&channel_reported_count);
            if (error != ESP_OK) {
                emit_scan_error(
                    scan_id, started_us, error, "count", channel);
                (void)esp_wifi_clear_ap_list();
                failed = true;
                break;
            }
            reported_count += channel_reported_count;

            uint16_t channel_record_count = channel_reported_count;
            if (channel_record_count > CONFIG_PROBE_MAX_AP_RECORDS) {
                channel_record_count = CONFIG_PROBE_MAX_AP_RECORDS;
                truncated = true;
            }
            memset(channel_records, 0, sizeof(channel_records));
            error = esp_wifi_scan_get_ap_records(&channel_record_count,
                                                 channel_records);
            if (error != ESP_OK) {
                emit_scan_error(
                    scan_id, started_us, error, "records", channel);
                (void)esp_wifi_clear_ap_list();
                failed = true;
                break;
            }
            (void)esp_wifi_clear_ap_list();
            for (uint16_t index = 0U;
                 index < channel_record_count;
                 ++index) {
                if (!merge_ap_record(
                        records, &record_count, &channel_records[index])) {
                    truncated = true;
                }
            }
        }
        if (failed) {
            vTaskDelay(pdMS_TO_TICKS(CONFIG_PROBE_INTER_SCAN_DELAY_MS));
            continue;
        }

        maintenance_portal_update_networks(records, record_count);
        const event_marker_result_t marker_state = marker_snapshot();
        for (uint16_t index = 0U; index < record_count; ++index) {
            emit_ap_record(scan_id, ended_us, &records[index], &marker_state);
        }

        telemetry_emit(
            "{\"schema\":\"" PROBE_SCHEMA "\",\"type\":\"scan\","
            "\"scan_id\":%" PRIu32 ",\"driver_scan_id\":%u,"
            "\"started_us\":%" PRId64 ",\"ended_us\":%" PRId64 ","
            "\"duration_ms\":%" PRId64 ",\"scan_mode\":\"%s\","
            "\"dwell_min_ms\":%d,\"dwell_max_ms\":%d,"
            "\"channel_mode\":\"%s\",\"channel_count\":%u,"
            "\"channel_1\":%u,\"channel_2\":%u,"
            "\"inter_scan_delay_ms\":%d,\"reported_aps\":%u,"
            "\"emitted_aps\":%u,\"truncated\":%s,"
            "\"free_heap\":%" PRIu32 ",\"minimum_free_heap\":%" PRIu32 ","
            "\"dropped_events\":%" PRIu32 ","
            "\"marker_active\":%s,\"event_id\":%" PRIu32 "}",
            scan_id,
            done.scan_id,
            started_us,
            ended_us,
            (ended_us - started_us) / 1000,
            PROBE_SCAN_MODE_NAME,
            PROBE_DWELL_MIN_MS,
            PROBE_DWELL_MAX_MS,
            scheduler_mode(&scheduler),
            (unsigned)scan_scheduler_channel_count(&scheduler),
            scan_scheduler_channel_at(&scheduler, 0U),
            scan_scheduler_channel_at(&scheduler, 1U),
            CONFIG_PROBE_INTER_SCAN_DELAY_MS,
            reported_count,
            record_count,
            truncated ? "true" : "false",
            esp_get_free_heap_size(),
            esp_get_minimum_free_heap_size(),
            (uint32_t)atomic_load(&dropped_scan_events),
            marker_state.active ? "true" : "false",
            marker_state.event_id);

        if (!calibration_finished) {
            observation_store_status_t store_status =
                observation_store_begin_scan(
                    &calibration_store, scan_id);
            if (store_status != OBSERVATION_STORE_OK) {
                emit_calibration_error(
                    scan_id, store_status, "begin_scan");
                calibration_finished = true;
                maintenance_portal_update_calibration(
                    false,
                    calibration_store.completed_scans,
                    CONFIG_PROBE_CALIBRATION_SCANS);
            } else {
                for (uint16_t index = 0U;
                     index < record_count;
                     ++index) {
                    const wifi_ap_record_t *record = &records[index];
                    const size_t ssid_length =
                        strnlen((const char *)record->ssid,
                                sizeof(record->ssid));
                    store_status = observation_store_observe(
                        &calibration_store,
                        record->bssid,
                        record->ssid,
                        ssid_length,
                        record->primary,
                        record->rssi);
                    if (store_status != OBSERVATION_STORE_OK &&
                        !calibration_warning_reported) {
                        emit_calibration_error(
                            scan_id, store_status, "observe");
                        calibration_warning_reported = true;
                    }
                }
                store_status =
                    observation_store_end_scan(&calibration_store);
                maintenance_portal_update_calibration(
                    true,
                    calibration_store.completed_scans,
                    CONFIG_PROBE_CALIBRATION_SCANS);
                if (store_status != OBSERVATION_STORE_OK) {
                    emit_calibration_error(
                        scan_id, store_status, "end_scan");
                    calibration_finished = true;
                    maintenance_portal_update_calibration(
                        false,
                        calibration_store.completed_scans,
                        CONFIG_PROBE_CALIBRATION_SCANS);
                } else if (calibration_store.completed_scans >=
                           CONFIG_PROBE_CALIBRATION_SCANS) {
                    const bool completed =
                        finish_calibration(&calibration_store,
                                           &scheduler,
                                           scan_id);
                    calibration_finished = true;
                    maintenance_portal_update_calibration(
                        false,
                        calibration_store.completed_scans,
                        CONFIG_PROBE_CALIBRATION_SCANS);
                    if (!completed) {
                        scheduler = make_scan_scheduler();
                        telemetry_emit(
                            "{\"schema\":\"" PROBE_SCHEMA "\","
                            "\"type\":\"calibration\","
                            "\"state\":\"failed\","
                            "\"scan_id\":%" PRIu32 ","
                            "\"channel_mode\":\"%s\"}",
                            scan_id,
                            scheduler_mode(&scheduler));
                    }
                }
            }
        }

        if (calibration_finished && !detector_ready) {
            reference_store_blob_t calibrated = {0};
            if (reference_store_load(&calibrated) ==
                    REFERENCE_STORE_OK &&
                detector_from_store(&detector, &calibrated)) {
                detector_ready = true;
            }
        }
        if (detector_ready) {
            multiref_status_t detector_status =
                multiref_detector_begin_scan(&detector, scan_id);
            for (uint16_t index = 0U;
                 detector_status == MULTIREF_OK &&
                 index < record_count;
                 ++index) {
                detector_status = multiref_detector_observe(
                    &detector,
                    records[index].bssid,
                    records[index].rssi);
            }
            multiref_result_t detector_result = {0};
            if (detector_status == MULTIREF_OK) {
                detector_status = multiref_detector_end_scan(
                    &detector, &detector_result);
            }
            if (detector_status == MULTIREF_OK) {
                emit_detector_result(scan_id, &detector_result);
                update_status_led(detector_result.state, scan_id);
                maintenance_portal_update_detector(
                    scan_id,
                    &detector_result);
            } else {
                telemetry_emit(
                    "{\"schema\":\"" PROBE_SCHEMA "\","
                    "\"type\":\"detector_error\","
                    "\"scan_id\":%" PRIu32 ",\"status\":%d}",
                    scan_id,
                    detector_status);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(CONFIG_PROBE_INTER_SCAN_DELAY_MS));
    }
}

#if CONFIG_PROBE_MARKER_GPIO >= 0
static void marker_task(void *argument)
{
    (void)argument;
    bool previous_reported_pressed =
        gpio_get_level((gpio_num_t)CONFIG_PROBE_MARKER_GPIO) == 0;
    int64_t pressed_since_ms =
        previous_reported_pressed ? esp_timer_get_time() / 1000 : 0;
    bool portal_started_for_press = false;
    telemetry_emit(
        "{\"schema\":\"" PROBE_SCHEMA "\",\"type\":\"button\","
        "\"timestamp_us\":%" PRId64 ",\"gpio\":%d,"
        "\"pressed\":%s,\"initial\":true}",
        esp_timer_get_time(),
        CONFIG_PROBE_MARKER_GPIO,
        previous_reported_pressed ? "true" : "false");
    for (;;) {
        const bool pressed =
            gpio_get_level((gpio_num_t)CONFIG_PROBE_MARKER_GPIO) == 0;
        const int64_t timestamp_ms = esp_timer_get_time() / 1000;
        if (pressed != previous_reported_pressed) {
            previous_reported_pressed = pressed;
            pressed_since_ms = pressed ? timestamp_ms : 0;
            portal_started_for_press = false;
            telemetry_emit(
                "{\"schema\":\"" PROBE_SCHEMA "\",\"type\":\"button\","
                "\"timestamp_us\":%" PRId64 ",\"gpio\":%d,"
                "\"pressed\":%s,\"initial\":false}",
                esp_timer_get_time(),
                CONFIG_PROBE_MARKER_GPIO,
                pressed ? "true" : "false");
        }
        if (pressed && !portal_started_for_press &&
            timestamp_ms - pressed_since_ms >=
                CONFIG_PROBE_PORTAL_LONG_PRESS_MS) {
            portal_started_for_press = true;
            const esp_err_t portal_error =
                maintenance_portal_start(
                    CONFIG_PROBE_PORTAL_AP_SSID,
                    CONFIG_PROBE_PORTAL_AP_PASSWORD);
            telemetry_emit(
                "{\"schema\":\"" PROBE_SCHEMA "\","
                "\"type\":\"maintenance\","
                "\"state\":\"%s\",\"ssid\":\"%s\","
                "\"ip\":\"192.168.4.1\",\"esp_error\":%d}",
                portal_error == ESP_OK ? "started" : "failed",
                CONFIG_PROBE_PORTAL_AP_SSID,
                portal_error);
        }
        event_marker_result_t result = {0};
        if (xSemaphoreTake(marker_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            result = event_marker_update(&marker, pressed, timestamp_ms);
            xSemaphoreGive(marker_mutex);
        }
        if (result.transition != EVENT_MARKER_NO_TRANSITION) {
            telemetry_emit(
                "{\"schema\":\"" PROBE_SCHEMA "\",\"type\":\"marker\","
                "\"timestamp_us\":%" PRId64 ",\"state\":\"%s\","
                "\"event_id\":%" PRIu32 "}",
                esp_timer_get_time(),
                result.active ? "started" : "finished",
                result.event_id);
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
#endif

static void init_marker(void)
{
#if CONFIG_PROBE_MARKER_GPIO >= 0
    const gpio_config_t marker_gpio_config = {
        .pin_bit_mask = 1ULL << CONFIG_PROBE_MARKER_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&marker_gpio_config));
    const bool initially_pressed =
        gpio_get_level((gpio_num_t)CONFIG_PROBE_MARKER_GPIO) == 0;
    event_marker_init(&marker,
                      initially_pressed,
                      CONFIG_PROBE_MARKER_DEBOUNCE_MS);
    ESP_ERROR_CHECK(
        xTaskCreate(marker_task,
                    "event_marker",
                    PROBE_MARKER_TASK_STACK,
                    NULL,
                    5,
                    NULL) == pdPASS
            ? ESP_OK
            : ESP_ERR_NO_MEM);
#else
    event_marker_init(&marker, false, 0U);
#endif
}

static void init_status_led(void)
{
#if CONFIG_PROBE_STATUS_LED_GPIO >= 0
    const gpio_config_t led_config = {
        .pin_bit_mask =
            1ULL << CONFIG_PROBE_STATUS_LED_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&led_config));
    status_led_write(false);
#endif
}

static void init_wifi_scan_only(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(
        esp_netif_create_default_wifi_ap() != NULL
            ? ESP_OK
            : ESP_ERR_NO_MEM);
    const wifi_init_config_t wifi_config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wifi_config));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT,
                                               WIFI_EVENT_SCAN_DONE,
                                               wifi_scan_event_handler,
                                               NULL));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
}

void app_main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    esp_err_t error = nvs_flash_init();
    if (error == ESP_ERR_NVS_NO_FREE_PAGES ||
        error == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        error = nvs_flash_init();
    }
    ESP_ERROR_CHECK(error);

    scan_done_queue =
        xQueueCreate(PROBE_SCAN_QUEUE_LENGTH, sizeof(scan_done_message_t));
    telemetry_mutex = xSemaphoreCreateMutex();
    marker_mutex = xSemaphoreCreateMutex();
    ESP_ERROR_CHECK(scan_done_queue != NULL && telemetry_mutex != NULL &&
                            marker_mutex != NULL
                        ? ESP_OK
                        : ESP_ERR_NO_MEM);

    init_status_led();
    init_marker();
    init_wifi_scan_only();
    ESP_ERROR_CHECK(
        maintenance_portal_start(
            CONFIG_PROBE_PORTAL_AP_SSID,
            CONFIG_PROBE_PORTAL_AP_PASSWORD));

    uint8_t mac[6] = {0};
    ESP_ERROR_CHECK(esp_read_mac(mac, ESP_MAC_WIFI_STA));
    char mac_text[18];
    probe_format_bssid(mac_text, mac);
    const scan_scheduler_t scheduler = make_scan_scheduler();
    telemetry_emit(
        "{\"schema\":\"" PROBE_SCHEMA "\",\"type\":\"boot\","
        "\"target\":\"esp32c3\",\"station_mac\":\"%s\","
        "\"scan_mode\":\"%s\",\"dwell_min_ms\":%d,\"dwell_max_ms\":%d,"
        "\"channel_mode\":\"%s\",\"channel_count\":%u,"
        "\"channel_1\":%u,\"channel_2\":%u,"
        "\"inter_scan_delay_ms\":%d,"
        "\"max_ap_records\":%d,\"marker_gpio\":%d,"
        "\"reset_reason\":%d}",
        mac_text,
        PROBE_SCAN_MODE_NAME,
        PROBE_DWELL_MIN_MS,
        PROBE_DWELL_MAX_MS,
        PROBE_CHANNEL_MODE_NAME,
        (unsigned)scan_scheduler_channel_count(&scheduler),
        scan_scheduler_channel_at(&scheduler, 0U),
        scan_scheduler_channel_at(&scheduler, 1U),
        CONFIG_PROBE_INTER_SCAN_DELAY_MS,
        CONFIG_PROBE_MAX_AP_RECORDS,
        CONFIG_PROBE_MARKER_GPIO,
        esp_reset_reason());

    ESP_LOGI(TAG,
             "Starting disconnected %s scans; esp_wifi_connect() is never called",
             PROBE_SCAN_MODE_NAME
    );
    ESP_ERROR_CHECK(
        xTaskCreate(scan_task,
                    "scan_probe",
                    PROBE_SCAN_TASK_STACK,
                    NULL,
                    6,
                    NULL) == pdPASS
            ? ESP_OK
            : ESP_ERR_NO_MEM);
}

#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
host_build_dir="${project_dir}/tests/host/build-host"
firmware_dir="${project_dir}/firmware"

if [[ -z "${IDF_PATH:-}" ]]; then
    echo "IDF_PATH is not set. Run: source /path/to/esp-idf/export.sh" >&2
    exit 1
fi

cmake -S "${project_dir}/tests/host" -B "${host_build_dir}"
cmake --build "${host_build_dir}"
ctest --test-dir "${host_build_dir}" --output-on-failure

idf.py -C "${project_dir}" build
mkdir -p "${firmware_dir}"
cp "${project_dir}/build/bootloader/bootloader.bin" \
   "${firmware_dir}/bootloader.bin"
cp "${project_dir}/build/partition_table/partition-table.bin" \
   "${firmware_dir}/partition-table.bin"
cp "${project_dir}/build/wifi_motion_rssi_c3_ap_scanner.bin" \
   "${firmware_dir}/wifi-ap-scan-probe-c3.bin"

python -m esptool --chip esp32c3 merge-bin \
    --flash-mode dio --flash-freq 80m --flash-size 4MB \
    -o "${firmware_dir}/wifi-ap-scan-probe-c3-complete.bin" \
    0x0 "${firmware_dir}/bootloader.bin" \
    0x8000 "${firmware_dir}/partition-table.bin" \
    0x10000 "${firmware_dir}/wifi-ap-scan-probe-c3.bin"

idf_revision="$(git -C "${IDF_PATH}" rev-parse HEAD)"
idf_description="$(git -C "${IDF_PATH}" describe --always --dirty)"
if grep -q '^CONFIG_PROBE_LIMIT_CHANNELS=y$' "${project_dir}/sdkconfig"; then
    channel_1="$(
        sed -n 's/^CONFIG_PROBE_REFERENCE_CHANNEL_1=//p' \
            "${project_dir}/sdkconfig"
    )"
    channel_2="$(
        sed -n 's/^CONFIG_PROBE_REFERENCE_CHANNEL_2=//p' \
            "${project_dir}/sdkconfig"
    )"
    channel_plan="selected (${channel_1},${channel_2})"
else
    channel_plan="all"
fi
calibration_scans="$(
    sed -n 's/^CONFIG_PROBE_CALIBRATION_SCANS=//p' \
        "${project_dir}/sdkconfig"
)"
detector_trigger="$(
    sed -n 's/^CONFIG_PROBE_DETECTOR_TRIGGER_SCORE_X100=//p' \
        "${project_dir}/sdkconfig"
)"
detector_release="$(
    sed -n 's/^CONFIG_PROBE_DETECTOR_RELEASE_SCORE_X100=//p' \
        "${project_dir}/sdkconfig"
)"
detector_sigma="$(
    sed -n 's/^CONFIG_PROBE_DETECTOR_ADAPTIVE_SIGMA_X100=//p' \
        "${project_dir}/sdkconfig"
)"
detector_window="$(
    sed -n 's/^CONFIG_PROBE_DETECTOR_ADAPTIVE_WINDOW=//p' \
        "${project_dir}/sdkconfig"
)"
manual_ssid_1="$(
    sed -n 's/^CONFIG_PROBE_MANUAL_SSID_1=//p' \
        "${project_dir}/sdkconfig"
)"
manual_ssid_2="$(
    sed -n 's/^CONFIG_PROBE_MANUAL_SSID_2=//p' \
        "${project_dir}/sdkconfig"
)"
if [[ "${manual_ssid_1}" == '""' && "${manual_ssid_2}" == '""' ]]; then
    selection_mode="automatic"
else
    selection_mode="manual"
fi
{
    echo "Project: WiFi-Motion-RSSI-C3-AP-Scanner"
    echo "Target: esp32c3"
    echo "ESP-IDF commit: ${idf_revision}"
    echo "ESP-IDF describe: ${idf_description}"
    echo "Scan mode: passive"
    echo "Channel plan: ${channel_plan}"
    echo "Calibration scans: ${calibration_scans}"
    echo "Reference selection: ${selection_mode}"
    echo "Reference persistence: NVS reference schema v1; config schema v2 with CRC32"
    echo "Detector minimum score thresholds x100: ${detector_trigger}/${detector_release}"
    echo "Detector adaptive threshold: sigma x100 ${detector_sigma}; quiet window ${detector_window}"
    echo "Schema: wifi_ap_scan/v1"
} > "${firmware_dir}/BUILD-INFO.txt"

(
    cd "${firmware_dir}"
    sha256sum \
        bootloader.bin \
        partition-table.bin \
        wifi-ap-scan-probe-c3.bin \
        wifi-ap-scan-probe-c3-complete.bin \
        > SHA256SUMS
)

echo "Firmware generated in ${firmware_dir}"

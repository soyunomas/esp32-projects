cmake_minimum_required(VERSION 3.16)
project(ap_scan_probe_host_tests C)

enable_testing()
find_package(Python3 COMPONENTS Interpreter REQUIRED)

add_executable(
    event_marker_tests
    test_event_marker.c
    ../../components/event_marker/event_marker.c
)
target_include_directories(
    event_marker_tests
    PRIVATE ../../components/event_marker/include
)
target_compile_features(event_marker_tests PRIVATE c_std_11)
target_compile_options(event_marker_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_test(NAME event_marker_tests COMMAND event_marker_tests)

add_executable(
    probe_format_tests
    test_probe_format.c
    ../../components/probe_format/probe_format.c
)
target_include_directories(
    probe_format_tests
    PRIVATE ../../components/probe_format/include
)
target_compile_features(probe_format_tests PRIVATE c_std_11)
target_compile_options(probe_format_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_test(NAME probe_format_tests COMMAND probe_format_tests)

add_executable(
    scan_scheduler_tests
    test_scan_scheduler.c
    ../../components/scan_scheduler/scan_scheduler.c
)
target_include_directories(
    scan_scheduler_tests
    PRIVATE ../../components/scan_scheduler/include
)
target_compile_features(scan_scheduler_tests PRIVATE c_std_11)
target_compile_options(
    scan_scheduler_tests PRIVATE -Wall -Wextra -Wpedantic -Werror
)
add_test(NAME scan_scheduler_tests COMMAND scan_scheduler_tests)

add_executable(
    motion_history_tests
    test_motion_history.c
    ../../components/motion_history/motion_history.c
)
target_include_directories(
    motion_history_tests
    PRIVATE ../../components/motion_history/include
)
target_compile_features(motion_history_tests PRIVATE c_std_11)
target_compile_options(
    motion_history_tests PRIVATE -Wall -Wextra -Wpedantic -Werror
)
add_test(NAME motion_history_tests COMMAND motion_history_tests)

add_executable(
    reference_selector_tests
    test_reference_selector.c
    ../../components/reference_selector/reference_selector.c
)
target_include_directories(
    reference_selector_tests
    PRIVATE ../../components/reference_selector/include
)
target_compile_features(reference_selector_tests PRIVATE c_std_11)
target_compile_options(
    reference_selector_tests PRIVATE -Wall -Wextra -Wpedantic -Werror
)
add_test(NAME reference_selector_tests COMMAND reference_selector_tests)

add_executable(
    observation_store_tests
    test_observation_store.c
    ../../components/observation_store/observation_store.c
    ../../components/reference_selector/reference_selector.c
)
target_include_directories(
    observation_store_tests
    PRIVATE
        ../../components/observation_store/include
        ../../components/reference_selector/include
)
target_compile_features(observation_store_tests PRIVATE c_std_11)
target_compile_options(
    observation_store_tests PRIVATE -Wall -Wextra -Wpedantic -Werror
)
add_test(NAME observation_store_tests COMMAND observation_store_tests)

add_executable(
    reference_store_tests
    test_reference_store.c
    ../../components/reference_store/reference_store_codec.c
)
target_include_directories(
    reference_store_tests
    PRIVATE
        ../../components/reference_store/include
        ../../components/reference_selector/include
)
target_compile_features(reference_store_tests PRIVATE c_std_11)
target_compile_options(
    reference_store_tests PRIVATE -Wall -Wextra -Wpedantic -Werror
)
add_test(NAME reference_store_tests COMMAND reference_store_tests)

add_executable(
    probe_config_tests
    test_probe_config.c
    ../../components/probe_config/probe_config_codec.c
    ../../components/reference_store/reference_store_codec.c
)
target_include_directories(
    probe_config_tests
    PRIVATE
        ../../components/probe_config/include
        ../../components/reference_store/include
        ../../components/reference_selector/include
)
target_compile_features(probe_config_tests PRIVATE c_std_11)
target_compile_options(
    probe_config_tests PRIVATE -Wall -Wextra -Wpedantic -Werror
)
add_test(NAME probe_config_tests COMMAND probe_config_tests)

add_executable(
    probe_config_nvs_tests
    test_probe_config_nvs.c
    ../../components/probe_config/probe_config_codec.c
    ../../components/probe_config/probe_config_nvs.c
    ../../components/reference_store/reference_store_codec.c
)
target_include_directories(
    probe_config_nvs_tests
    PRIVATE
        fakes
        ../../components/probe_config/include
        ../../components/reference_store/include
        ../../components/reference_selector/include
)
target_compile_features(probe_config_nvs_tests PRIVATE c_std_11)
target_compile_options(
    probe_config_nvs_tests PRIVATE -Wall -Wextra -Wpedantic -Werror
)
add_test(NAME probe_config_nvs_tests COMMAND probe_config_nvs_tests)

add_executable(
    multiref_detector_tests
    test_multiref_detector.c
    ../../components/multiref_detector/multiref_detector.c
)
target_include_directories(
    multiref_detector_tests
    PRIVATE ../../components/multiref_detector/include
)
target_compile_features(multiref_detector_tests PRIVATE c_std_11)
target_compile_options(
    multiref_detector_tests PRIVATE -Wall -Wextra -Wpedantic -Werror
)
add_test(NAME multiref_detector_tests COMMAND multiref_detector_tests)

add_executable(
    detector_replay
    ../../tools/detector_replay.c
    ../../components/multiref_detector/multiref_detector.c
)
target_include_directories(
    detector_replay
    PRIVATE ../../components/multiref_detector/include
)
target_compile_features(detector_replay PRIVATE c_std_11)
target_compile_options(
    detector_replay PRIVATE -Wall -Wextra -Wpedantic -Werror
)

add_test(
    NAME capture_jsonl_tests
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_capture_jsonl.py
)

add_test(
    NAME summarize_capture_tests
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/test_summarize_capture.py
)

add_test(
    NAME portal_source_tests
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/test_portal_source.py
)

cmake_minimum_required(VERSION 3.16)
project(motion_detector_host_tests C)

enable_testing()
find_package(Python3 COMPONENTS Interpreter REQUIRED)

add_executable(
    motion_detector_tests
    test_motion_detector.c
    ../../components/motion_detector/motion_detector.c
)

target_include_directories(
    motion_detector_tests
    PRIVATE ../../components/motion_detector/include
)

target_compile_features(motion_detector_tests PRIVATE c_std_11)
target_compile_options(motion_detector_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
target_link_libraries(motion_detector_tests PRIVATE m)

add_test(NAME motion_detector_tests COMMAND motion_detector_tests)

add_executable(
    sample_metrics_tests
    test_sample_metrics.c
    ../../components/sample_metrics/sample_metrics.c
)

target_include_directories(
    sample_metrics_tests
    PRIVATE ../../components/sample_metrics/include
)

target_compile_features(sample_metrics_tests PRIVATE c_std_11)
target_compile_options(sample_metrics_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_test(NAME sample_metrics_tests COMMAND sample_metrics_tests)

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
    csi_features_tests
    test_csi_features.c
    ../../components/csi_features/csi_features.c
)

target_include_directories(
    csi_features_tests
    PRIVATE ../../components/csi_features/include
)

target_compile_features(csi_features_tests PRIVATE c_std_11)
target_compile_options(csi_features_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
target_link_libraries(csi_features_tests PRIVATE m)

add_test(NAME csi_features_tests COMMAND csi_features_tests)

add_executable(
    app_config_tests
    test_app_config.c
    ../../components/app_config/app_config.c
    ../../components/motion_detector/motion_detector.c
)

target_include_directories(
    app_config_tests
    PRIVATE
        stubs
        ../../components/app_config/include
        ../../components/motion_detector/include
)

target_compile_features(app_config_tests PRIVATE c_std_11)
target_compile_definitions(app_config_tests PRIVATE _POSIX_C_SOURCE=200809L)
target_compile_options(app_config_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
target_link_libraries(app_config_tests PRIVATE m)

add_test(NAME app_config_tests COMMAND app_config_tests)

add_executable(
    portal_auth_tests
    test_portal_auth.c
    ../../components/portal_auth/portal_auth.c
)

target_include_directories(
    portal_auth_tests
    PRIVATE
        stubs
        ../../components/portal_auth/include
)

target_compile_features(portal_auth_tests PRIVATE c_std_11)
target_compile_definitions(portal_auth_tests PRIVATE _POSIX_C_SOURCE=200809L)
target_compile_options(portal_auth_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_test(NAME portal_auth_tests COMMAND portal_auth_tests)

add_executable(
    telegram_validation_tests
    test_telegram_validation.c
    ../../components/telegram_notifier/telegram_validation.c
)

target_include_directories(
    telegram_validation_tests
    PRIVATE
        stubs
        ../../components/telegram_notifier/include
)

target_compile_features(telegram_validation_tests PRIVATE c_std_11)
target_compile_definitions(telegram_validation_tests PRIVATE _POSIX_C_SOURCE=200809L)
target_compile_options(telegram_validation_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_test(NAME telegram_validation_tests COMMAND telegram_validation_tests)

add_executable(
    captive_dns_tests
    test_captive_dns.c
    ../../components/captive_dns/captive_dns_protocol.c
)

target_include_directories(
    captive_dns_tests
    PRIVATE
        stubs
        ../../components/captive_dns/include
)

target_compile_features(captive_dns_tests PRIVATE c_std_11)
target_compile_options(captive_dns_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_test(NAME captive_dns_tests COMMAND captive_dns_tests)

add_executable(
    recovery_button_tests
    test_recovery_button.c
    ../../components/recovery_button/recovery_button.c
)

target_include_directories(
    recovery_button_tests
    PRIVATE ../../components/recovery_button/include
)

target_compile_features(recovery_button_tests PRIVATE c_std_11)
target_compile_options(recovery_button_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_test(NAME recovery_button_tests COMMAND recovery_button_tests)

add_executable(
    detector_replay
    ../../tools/detector_replay.c
    ../../components/motion_detector/motion_detector.c
)

target_include_directories(
    detector_replay
    PRIVATE ../../components/motion_detector/include
)

target_compile_features(detector_replay PRIVATE c_std_11)
target_compile_options(detector_replay PRIVATE -Wall -Wextra -Wpedantic -Werror)
target_link_libraries(detector_replay PRIVATE m)

add_test(
    NAME phase1_validator_tests
    COMMAND ${Python3_EXECUTABLE} -m unittest -v test_validate_phase1.py
)

add_test(
    NAME experiment_tool_tests
    COMMAND ${Python3_EXECUTABLE} -m unittest -v test_experiment.py
)
add_test(
    NAME portal_html_tests
    COMMAND ${Python3_EXECUTABLE} -m unittest -v test_portal_html.py
)
set_tests_properties(
    experiment_tool_tests
    PROPERTIES
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        ENVIRONMENT
            "PYTHONDONTWRITEBYTECODE=1;REPLAY_BINARY=$<TARGET_FILE:detector_replay>"
)
set_tests_properties(
    phase1_validator_tests
    PROPERTIES
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        ENVIRONMENT PYTHONDONTWRITEBYTECODE=1
)
set_tests_properties(
    portal_html_tests
    PROPERTIES
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        ENVIRONMENT PYTHONDONTWRITEBYTECODE=1
)

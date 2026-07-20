cmake_minimum_required(VERSION 3.16)
project(motion_detector_host_tests C)

enable_testing()

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

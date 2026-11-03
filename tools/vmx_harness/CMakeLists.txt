cmake_minimum_required(VERSION 3.25)
project(fifa07_vmx_regression LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 23)
find_package(rexglue CONFIG REQUIRED)
add_executable(vmx_regression
  "${CMAKE_CURRENT_SOURCE_DIR}/../../logs/vmx/generated/ppc_test_functions.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/../../logs/vmx/generated/ppc_test_cases.cpp"
  main.cpp)
target_include_directories(vmx_regression PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}"
  "${CMAKE_CURRENT_SOURCE_DIR}/../../logs/vmx/generated")
target_link_libraries(vmx_regression PRIVATE rex::runtime)
rexglue_apply_target_settings(vmx_regression)
target_compile_options(vmx_regression PRIVATE
  -UTRACY_ENABLE -UREXGLUE_ENABLE_PROFILING -UREXGLUE_ENABLE_PERF_COUNTERS
  -UREXGLUE_PROFILE_GUEST_FUNCTIONS -msse4.1 -mssse3)
add_custom_command(TARGET vmx_regression POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "$<TARGET_FILE:rex::runtime>" "$<TARGET_FILE_DIR:vmx_regression>")

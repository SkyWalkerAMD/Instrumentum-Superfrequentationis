cmake_minimum_required(VERSION 3.16)
project(octool_core LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
find_package(Threads REQUIRED)

add_library(octool_core STATIC amd_pstates.cpp hardware.cpp helper_protocol.cpp amd_pm_native.cpp
    register_update.cpp intel_controls.cpp intel_oc.cpp intel_turbo.cpp intel_uncore.cpp intel_energy.cpp intel_thermal.cpp intel_limits.cpp amd_smu.cpp amd_curve.cpp amd_pbo.cpp amd_pm.cpp amd_umc.cpp umc_capture.cpp amd_topology.cpp spd.cpp intel_memory.cpp intel_mchbar.cpp intel_clock.cpp)
target_include_directories(octool_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(octool_core PUBLIC Threads::Threads)
if(MSVC)
    target_compile_options(octool_core PRIVATE /W4 /WX)
else()
    target_compile_options(octool_core PRIVATE -Wall -Wextra -Werror)
endif()

include(CTest)
if(BUILD_TESTING)
    foreach(component pstates hardware helper_protocol platform memory intel_oc intel_turbo intel_uncore intel_energy intel_thermal intel_limits intel_avx intel_bclk_voltage intel_iccmax intel_pl4 intel_vf intel_tvb intel_tvb_table intel_tvb_table_edit amd_curve amd_curve_all amd_pbo amd_pbo_query amd_power_adjustment amd_pbo_fmax amd_pbo_limits amd_oc amd_oc_cores amd_pm amd_pm_native amd_granite_pm umc_capture spd intel_memory intel_clock)
        add_executable(${component}_core_test ../tests/${component}_core_test.cpp)
        target_link_libraries(${component}_core_test PRIVATE octool_core)
        # These regressions use assertions. Release builds must test too.
        if(MSVC)
            target_compile_options(${component}_core_test PRIVATE /W4 /WX /UNDEBUG)
        else()
            target_compile_options(${component}_core_test PRIVATE -Wall -Wextra -Werror -UNDEBUG)
        endif()
        add_test(NAME ${component}_core COMMAND ${component}_core_test)
        set_tests_properties(${component}_core PROPERTIES TIMEOUT 30)
    endforeach()
    # Exhaustive fault positions preserve the original 4 ms UMC index delay.
    # Windows timer granularity can make this synthetic test take longer.
    set_tests_properties(memory_core PROPERTIES TIMEOUT 120)
    set_tests_properties(amd_curve_core PROPERTIES TIMEOUT 90)
    # PBO faults/cancellations traverse all six mailbox arguments. Preserve the
    # real transport delays; Windows/macOS timer granularity makes this much
    # slower than Linux. This changes only the whole test executable deadline.
    set_tests_properties(amd_pbo_core PROPERTIES TIMEOUT 240)
    set_tests_properties(amd_pbo_fmax_core PROPERTIES TIMEOUT 240)
    set_tests_properties(amd_pbo_limits_core PROPERTIES TIMEOUT 240)
    set_tests_properties(amd_curve_all_core PROPERTIES TIMEOUT 240)
    set_tests_properties(amd_pbo_query_core PROPERTIES TIMEOUT 240)
    set_tests_properties(amd_power_adjustment_core PROPERTIES TIMEOUT 360)
    set_tests_properties(amd_oc_core PROPERTIES TIMEOUT 360)
    set_tests_properties(amd_oc_cores_core PROPERTIES TIMEOUT 360)
    # Optional CCD mapping issues 32 presence queries per capture. Tests retain
    # transport sleeps; coarse Windows/macOS timers need a larger suite budget.
    set_tests_properties(amd_pm_native_core PROPERTIES TIMEOUT 900)
    set_tests_properties(amd_granite_pm_core PROPERTIES TIMEOUT 300)
    # Keep every-byte JSON truncation coverage as the monitor fields grow.
    # Address/undefined-behavior sanitizers make this parser exercise slower.
    set_tests_properties(amd_pm_core PROPERTIES TIMEOUT 240)
endif()

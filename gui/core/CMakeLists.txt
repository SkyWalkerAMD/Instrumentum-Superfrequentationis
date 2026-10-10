cmake_minimum_required(VERSION 3.16)
project(octool_core LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
find_package(Threads REQUIRED)

add_library(octool_core STATIC amd_pstates.cpp hardware.cpp helper_protocol.cpp
    register_update.cpp intel_controls.cpp intel_oc.cpp intel_turbo.cpp intel_uncore.cpp amd_smu.cpp amd_curve.cpp amd_umc.cpp umc_capture.cpp amd_topology.cpp spd.cpp)
target_include_directories(octool_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(octool_core PUBLIC Threads::Threads)
if(MSVC)
    target_compile_options(octool_core PRIVATE /W4 /WX)
else()
    target_compile_options(octool_core PRIVATE -Wall -Wextra -Werror)
endif()

include(CTest)
if(BUILD_TESTING)
    foreach(component pstates hardware helper_protocol platform memory intel_oc intel_turbo intel_uncore intel_vf amd_curve umc_capture spd)
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
endif()

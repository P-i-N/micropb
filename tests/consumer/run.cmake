# Runs the consumer project with one Arm compiler: cmake -P run.cmake
# Inputs (-D): COMPILER, CPU, SOURCE_DIR (micropb), BINARY_DIR (a folder for this run),
# MODE (build or reflection_on), REFLECTION (on or off: what the compiler supports).
#   build:          configure and build; the configure output must show the status line of REFLECTION.
#   reflection_on:  configure with -DMICROPB_REFLECTION=ON; it must fail with the message of micropb.
#                   Only for a compiler without reflection.
foreach(var COMPILER CPU SOURCE_DIR BINARY_DIR MODE REFLECTION)
    if(NOT DEFINED ${var})
        message(FATAL_ERROR "run.cmake: ${var} is not set")
    endif()
endforeach()

file(REMOVE_RECURSE ${BINARY_DIR})

set(configure_command ${CMAKE_COMMAND} -S ${SOURCE_DIR}/tests/consumer -B ${BINARY_DIR} -G Ninja
    -DCMAKE_SYSTEM_NAME=Generic
    -DCMAKE_CXX_COMPILER=${COMPILER}
    -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY
    "-DCMAKE_CXX_FLAGS=-mcpu=${CPU} -mthumb"
    -DMICROPB_SOURCE_DIR=${SOURCE_DIR})
if(MODE STREQUAL "reflection_on")
    list(APPEND configure_command -DMICROPB_REFLECTION=ON)
endif()

execute_process(COMMAND ${configure_command}
    RESULT_VARIABLE configure_result OUTPUT_VARIABLE configure_output ERROR_VARIABLE configure_error)
message("${configure_output}")
message("${configure_error}")

if(MODE STREQUAL "reflection_on")
    if(configure_result EQUAL 0)
        message(FATAL_ERROR "run.cmake: configure with MICROPB_REFLECTION=ON must fail for this compiler")
    endif()
    if(NOT configure_error MATCHES "MICROPB_REFLECTION is ON, but the compiler does not support")
        message(FATAL_ERROR "run.cmake: configure failed, but not with the message of micropb")
    endif()
    return()
elseif(NOT MODE STREQUAL "build")
    message(FATAL_ERROR "run.cmake: MODE must be build or reflection_on, not '${MODE}'")
endif()

if(NOT configure_result EQUAL 0)
    message(FATAL_ERROR "run.cmake: configure failed (${configure_result})")
endif()
if(REFLECTION STREQUAL "on")
    set(expected "micropb: reflection on")
else()
    set(expected "micropb: reflection off (C++23, field lists only)")
endif()
string(FIND "${configure_output}" "${expected}" found)
if(found EQUAL -1)
    message(FATAL_ERROR "run.cmake: the configure output does not contain '${expected}'")
endif()

execute_process(COMMAND ${CMAKE_COMMAND} --build ${BINARY_DIR}
    RESULT_VARIABLE build_result OUTPUT_VARIABLE build_output ERROR_VARIABLE build_error)
message("${build_output}")
message("${build_error}")
if(NOT build_result EQUAL 0)
    message(FATAL_ERROR "run.cmake: build failed (${build_result})")
endif()

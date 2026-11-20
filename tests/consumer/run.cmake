# Runs the consumer project with one Arm compiler: cmake -P run.cmake
# Inputs (-D): COMPILER, CPU, SOURCE_DIR (micropb), BINARY_DIR (a folder for this run),
# GENERATOR (the CMake generator of the outer project), MAKE_PROGRAM (optional: its CMAKE_MAKE_PROGRAM).
foreach(var COMPILER CPU SOURCE_DIR BINARY_DIR GENERATOR)
    if(NOT DEFINED ${var})
        message(FATAL_ERROR "run.cmake: ${var} is not set")
    endif()
endforeach()

file(REMOVE_RECURSE ${BINARY_DIR})

set(configure_command ${CMAKE_COMMAND} -S ${SOURCE_DIR}/tests/consumer -B ${BINARY_DIR} -G "${GENERATOR}"
    -DCMAKE_SYSTEM_NAME=Generic
    -DCMAKE_CXX_COMPILER=${COMPILER}
    -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY
    "-DCMAKE_CXX_FLAGS=-mcpu=${CPU} -mthumb"
    -DMICROPB_SOURCE_DIR=${SOURCE_DIR})
if(MAKE_PROGRAM)
    list(APPEND configure_command "-DCMAKE_MAKE_PROGRAM=${MAKE_PROGRAM}")
endif()

execute_process(COMMAND ${configure_command}
    RESULT_VARIABLE configure_result OUTPUT_VARIABLE configure_output ERROR_VARIABLE configure_error)
message("${configure_output}")
message("${configure_error}")
if(NOT configure_result EQUAL 0)
    message(FATAL_ERROR "run.cmake: configure failed (${configure_result})")
endif()

execute_process(COMMAND ${CMAKE_COMMAND} --build ${BINARY_DIR}
    RESULT_VARIABLE build_result OUTPUT_VARIABLE build_output ERROR_VARIABLE build_error)
message("${build_output}")
message("${build_error}")
if(NOT build_result EQUAL 0)
    message(FATAL_ERROR "run.cmake: build failed (${build_result})")
endif()

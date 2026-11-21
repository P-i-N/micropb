cmake_minimum_required(VERSION 3.25)
project(micropb LANGUAGES CXX)

set(CMAKE_CXX_EXTENSIONS OFF)

option(MICROPB_TESTS "Build the micropb tests" ${PROJECT_IS_TOP_LEVEL})

add_library(micropb INTERFACE)
target_include_directories(micropb INTERFACE src)
target_compile_features(micropb INTERFACE cxx_std_23)

if(MICROPB_TESTS)
    enable_testing()

    set(micropb_compile_options -fno-exceptions -fno-rtti -Wall -Wextra -Werror)

    # The codec reads and writes messages through byte offsets, so constant
    # evaluation cannot examine it. A sanitizer build of the tests does this.
    option(MICROPB_SANITIZE "Build the tests with sanitizers" OFF)
    set(micropb_sanitize_options)
    set(micropb_sanitize_link_options)
    if(MICROPB_SANITIZE)
        if(MINGW)
            # MinGW has no sanitizer run-time library. A trap needs none.
            set(micropb_sanitize_options -fsanitize=undefined -fsanitize-undefined-trap-on-error)
        else()
            set(micropb_sanitize_options -fsanitize=address,undefined -fno-sanitize-recover=all)
            set(micropb_sanitize_link_options -fsanitize=address,undefined)
        endif()
    endif()

    function(micropb_add_test name)
        add_executable(${name} tests/${name}.cpp)
        target_link_libraries(${name} PRIVATE micropb)
        target_compile_options(${name} PRIVATE ${micropb_compile_options} ${micropb_sanitize_options})
        target_link_options(${name} PRIVATE ${micropb_sanitize_link_options})
        add_test(NAME ${name} COMMAND ${name})
    endfunction()

    # The test builds an object target that must not compile. It passes only when
    # the compiler output contains the static_assert message that names the rule.
    function(micropb_add_compile_fail_test name rule)
        add_library(${name} OBJECT EXCLUDE_FROM_ALL tests/compile_fail/${name}.cpp)
        target_link_libraries(${name} PRIVATE micropb)
        target_compile_options(${name} PRIVATE ${micropb_compile_options})
        add_test(NAME compile_fail.${name} COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target ${name})
        set_tests_properties(compile_fail.${name} PROPERTIES
            PASS_REGULAR_EXPRESSION "static assertion failed: pb: ${rule}:"
            RESOURCE_LOCK micropb_build_dir)
    endfunction()

    micropb_add_test(test_wire)
    micropb_add_test(test_types)
    micropb_add_test(test_schema)
    micropb_add_test(test_codec)
    micropb_add_test(test_encode)
    micropb_add_test(test_decode)
    micropb_add_test(test_wire_compat)
    micropb_add_test(test_generated)
    micropb_add_test(test_field_list)
    micropb_add_test(test_scalars)

    # The tables must be constants in ROM: the program must have no code that makes them before main().
    if(NOT MICROPB_SANITIZE)
        add_test(NAME no_startup_code COMMAND ${CMAKE_NM} $<TARGET_FILE:test_generated>)
        set_tests_properties(no_startup_code PROPERTIES FAIL_REGULAR_EXPRESSION "_GLOBAL__sub_I")
    endif()

    # Arm tests: compile only (no linking, no running). They need arm-none-eabi-g++; without it they are
    # off. MICROPB_ARM_COMPILERS can list the compilers; by default it is PATH and the Arm GNU Toolchain
    # folders on Windows.
    set(MICROPB_ARM_COMPILERS "" CACHE STRING "arm-none-eabi-g++ compilers for the Arm tests (empty: find them)")
    set(MICROPB_ARM_CPU cortex-m4 CACHE STRING "Value of -mcpu for the Arm tests")
    set(micropb_arm_compilers ${MICROPB_ARM_COMPILERS})
    set(micropb_arm_listed FALSE)
    if(micropb_arm_compilers)
        set(micropb_arm_listed TRUE)
    endif()
    if(NOT micropb_arm_compilers)
        find_program(MICROPB_ARM_GXX_ON_PATH arm-none-eabi-g++)
        file(GLOB micropb_arm_gnu_toolchains
            "C:/Program Files (x86)/Arm GNU Toolchain arm-none-eabi/*/bin/arm-none-eabi-g++.exe")
        set(micropb_arm_candidates ${micropb_arm_gnu_toolchains})
        if(MICROPB_ARM_GXX_ON_PATH)
            list(PREPEND micropb_arm_candidates "${MICROPB_ARM_GXX_ON_PATH}")
        endif()
        # Each compiler once, by its resolved path; the path as found is kept.
        set(micropb_arm_real_paths)
        foreach(gxx ${micropb_arm_candidates})
            get_filename_component(real "${gxx}" REALPATH)
            if(NOT real IN_LIST micropb_arm_real_paths)
                list(APPEND micropb_arm_real_paths "${real}")
                list(APPEND micropb_arm_compilers "${gxx}")
            endif()
        endforeach()
    endif()
    if(NOT micropb_arm_compilers)
        message(STATUS "micropb: no arm-none-eabi-g++ found, the Arm tests are off")
    endif()

    set(micropb_arm_flags -std=c++23 -fno-exceptions -fno-rtti -Wall -Wextra -Werror -Os
        -mcpu=${MICROPB_ARM_CPU} -mthumb)
    set(micropb_arm_prefixes)
    foreach(gxx ${micropb_arm_compilers})
        execute_process(COMMAND "${gxx}" -dumpversion OUTPUT_VARIABLE version OUTPUT_STRIP_TRAILING_WHITESPACE
            RESULT_VARIABLE version_result)
        if(NOT version_result EQUAL 0)
            if(micropb_arm_listed)
                message(FATAL_ERROR "micropb: '${gxx}' does not run (-dumpversion)")
            endif()
            message(WARNING "micropb: the Arm compiler '${gxx}' does not run (-dumpversion), skipping it")
            continue()
        endif()
        set(prefix arm_${version})
        if(prefix IN_LIST micropb_arm_prefixes)
            message(WARNING "micropb: two Arm compilers have the version ${version}, ignoring '${gxx}'")
            continue()
        endif()
        list(APPEND micropb_arm_prefixes ${prefix})
        set(micropb_${prefix}_gxx "${gxx}")
        file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/${prefix})
        message(STATUS "micropb: Arm compiler ${prefix} (${gxx})")
    endforeach()

    # Compile tests/<name>.cpp for each Arm compiler.
    function(micropb_add_arm_test name)
        foreach(prefix ${micropb_arm_prefixes})
            add_test(NAME ${prefix}.${name}
                COMMAND "${micropb_${prefix}_gxx}" ${micropb_arm_flags}
                    -I${CMAKE_CURRENT_SOURCE_DIR}/src -I${CMAKE_CURRENT_SOURCE_DIR}/tests
                    -c ${CMAKE_CURRENT_SOURCE_DIR}/tests/${name}.cpp
                    -o ${CMAKE_CURRENT_BINARY_DIR}/${prefix}/${name}.o)
        endforeach()
    endfunction()

    micropb_add_arm_test(test_wire)
    micropb_add_arm_test(test_types)
    micropb_add_arm_test(test_schema)
    micropb_add_arm_test(test_codec)
    micropb_add_arm_test(test_encode)
    micropb_add_arm_test(test_decode)
    micropb_add_arm_test(test_wire_compat)
    micropb_add_arm_test(test_field_list)
    micropb_add_arm_test(test_generated)
    micropb_add_arm_test(test_scalars)

    foreach(prefix ${micropb_arm_prefixes})
        # The tables must be ROM constants on Arm too.
        # The nm of the same toolchain: the folder of the compiler path as found, then the resolved one.
        get_filename_component(gxx_dir "${micropb_${prefix}_gxx}" DIRECTORY)
        get_filename_component(gxx_real "${micropb_${prefix}_gxx}" REALPATH)
        get_filename_component(gxx_real_dir "${gxx_real}" DIRECTORY)
        unset(nm)
        find_program(nm NAMES arm-none-eabi-nm HINTS "${gxx_dir}" "${gxx_real_dir}" NO_DEFAULT_PATH NO_CACHE)
        set_tests_properties(${prefix}.test_generated PROPERTIES FIXTURES_SETUP ${prefix}_generated)
        if(nm)
            add_test(NAME ${prefix}.no_startup_code COMMAND "${nm}" ${CMAKE_CURRENT_BINARY_DIR}/${prefix}/test_generated.o)
            set_tests_properties(${prefix}.no_startup_code PROPERTIES
                FIXTURES_REQUIRED ${prefix}_generated FAIL_REGULAR_EXPRESSION "_GLOBAL__sub_I")
        else()
            message(STATUS "micropb: no arm-none-eabi-nm next to ${micropb_${prefix}_gxx}, ${prefix}.no_startup_code is off")
        endif()

        # A project that uses micropb: configure and build with this compiler.
        set(consumer_command ${CMAKE_COMMAND} -DCOMPILER=${micropb_${prefix}_gxx} -DCPU=${MICROPB_ARM_CPU}
            -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR} -DBINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}/${prefix}_consumer
            "-DGENERATOR=${CMAKE_GENERATOR}")
        if(CMAKE_MAKE_PROGRAM)
            list(APPEND consumer_command "-DMAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}")
        endif()
        add_test(NAME ${prefix}.consumer
            COMMAND ${consumer_command} -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/consumer/run.cmake)
        set_tests_properties(${prefix}.consumer PROPERTIES RESOURCE_LOCK ${prefix}_consumer)
    endforeach()

    # The tests of tools/protoc-gen-micropb.py need Python and protoc.
    # Without them, CTest does not have the test `generator`. The other tests do not need these tools.
    find_package(Python3 COMPONENTS Interpreter)
    if(Python3_Interpreter_FOUND)
        set(micropb_plugin_tests ${CMAKE_CURRENT_SOURCE_DIR}/tests/generator/test_plugin.py)
        execute_process(COMMAND ${Python3_EXECUTABLE} ${micropb_plugin_tests} --check-tools
            RESULT_VARIABLE micropb_plugin_tools OUTPUT_QUIET ERROR_QUIET)
        if(micropb_plugin_tools EQUAL 0)
            add_test(NAME generator COMMAND ${Python3_EXECUTABLE} ${micropb_plugin_tests})
        else()
            message(STATUS "micropb: no protoc, the test `generator` is off")
        endif()
    endif()

    foreach(rule
            duplicate_tag tag_zero tag_too_large tag_reserved
            variant_no_monostate variant_duplicate_alternative variant_repeated_alternative
            variant_optional_alternative variant_variant_alternative unmapped_type monostate_member
            optional_repeated optional_optional optional_variant
            repeated_repeated repeated_optional repeated_variant
            decode_encode_only not_aggregate enum_too_wide
            member_offset_too_large decode_const_member decode_fixed_span
            field_list_type field_list_offset field_list_alternative recursive_message
            decode_needs_arena span_element_trivial no_field_list zigzag_unsigned fixed_width)
        micropb_add_compile_fail_test(${rule} ${rule})
    endforeach()

    micropb_add_compile_fail_test(list_member_type optional_repeated)
    micropb_add_compile_fail_test(list_duplicate_number duplicate_tag)
    micropb_add_compile_fail_test(list_decode_alternative decode_encode_only)
    micropb_add_compile_fail_test(list_decode_fixed_span decode_fixed_span)
    micropb_add_compile_fail_test(list_decode_const_span_element decode_const_member)
    micropb_add_compile_fail_test(decode_const_fixed_bytes decode_const_member)
    micropb_add_compile_fail_test(decode_const_repeated_element decode_const_member)
    micropb_add_compile_fail_test(decode_const_alternative decode_const_member)
    micropb_add_compile_fail_test(list_alternative_on_plain_member field_list_alternative)
    micropb_add_compile_fail_test(list_alternative_index_too_large field_list_alternative)
    micropb_add_compile_fail_test(list_alternative_missing field_list_alternative)
    micropb_add_compile_fail_test(list_alternative_shared_offset field_list_alternative)
    micropb_add_compile_fail_test(list_member_twice field_list_overlap)
    micropb_add_compile_fail_test(list_empty_member_twice field_list_overlap)
    micropb_add_compile_fail_test(char_member unmapped_type)
    micropb_add_compile_fail_test(volatile_member unmapped_type)
    micropb_add_compile_fail_test(wchar_member unmapped_type)
    # `__int128` is an integer type only with GNU extensions, so this target turns them on.
    micropb_add_compile_fail_test(int128_member unmapped_type)
    set_target_properties(int128_member PROPERTIES CXX_EXTENSIONS ON)

    # Size report: not a test. `cmake --build <dir> --target size_report` prints the
    # size of one translation unit that encodes and decodes 16 and 32 message types.
    foreach(count 16 32)
        add_library(size_report_${count} OBJECT EXCLUDE_FROM_ALL tests/size/size_report.cpp)
        target_link_libraries(size_report_${count} PRIVATE micropb)
        target_compile_options(size_report_${count} PRIVATE ${micropb_compile_options} -Os)
        target_compile_definitions(size_report_${count} PRIVATE MICROPB_SIZE_TYPES=${count})
    endforeach()

    # The headers must stay clean for a project that builds with these warnings as
    # errors. This object is part of the default build; it has no test of its own.
    add_library(micropb_strict_warnings OBJECT tests/size/size_report.cpp)
    target_link_libraries(micropb_strict_warnings PRIVATE micropb)
    target_compile_options(micropb_strict_warnings PRIVATE ${micropb_compile_options} -Wsign-conversion)
    target_compile_definitions(micropb_strict_warnings PRIVATE MICROPB_SIZE_TYPES=32)

    find_program(MICROPB_SIZE_TOOL size)
    add_custom_target(size_report
        COMMAND ${CMAKE_COMMAND} -DSIZE_TOOL=${MICROPB_SIZE_TOOL}
                -DOBJECT_16=$<TARGET_OBJECTS:size_report_16> -DOBJECT_32=$<TARGET_OBJECTS:size_report_32>
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/size/report.cmake
        VERBATIM)
    add_dependencies(size_report size_report_16 size_report_32)
endif()

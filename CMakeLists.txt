cmake_minimum_required(VERSION 3.25)
project(micropb LANGUAGES CXX)

set(CMAKE_CXX_EXTENSIONS OFF)

# MICROPB_REFLECTION: AUTO uses C++26 static reflection when the compiler has it, ON requires it,
# OFF never uses it. Without reflection, `micropb` is C++23 and each message type needs a field
# list (`pb::fields_of`).
set(MICROPB_REFLECTION AUTO CACHE STRING "Use C++26 reflection: AUTO, ON or OFF")
set_property(CACHE MICROPB_REFLECTION PROPERTY STRINGS AUTO ON OFF)
option(MICROPB_TESTS "Build the micropb tests" ${PROJECT_IS_TOP_LEVEL})

if(NOT MICROPB_REFLECTION MATCHES "^(AUTO|ON|OFF)$")
    message(FATAL_ERROR "micropb: MICROPB_REFLECTION must be AUTO, ON or OFF, not '${MICROPB_REFLECTION}'")
endif()

set(MICROPB_HAS_REFLECTION FALSE)
if(NOT MICROPB_REFLECTION STREQUAL "OFF")
    include(CheckCXXSourceCompiles)
    # CMake adds its own -std flag after the required flags, so the standard is set the CMake way too.
    set(CMAKE_REQUIRED_FLAGS "-std=c++26 -freflection")
    set(micropb_saved_cxx_standard ${CMAKE_CXX_STANDARD})
    set(CMAKE_CXX_STANDARD 26)
    # Compile only: a bare-metal toolchain cannot link a program.
    set(micropb_saved_try_compile_type ${CMAKE_TRY_COMPILE_TARGET_TYPE})
    set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
    check_cxx_source_compiles("
#if !defined(__cpp_impl_reflection)
#error no reflection
#endif
int main() {}
" MICROPB_COMPILER_HAS_REFLECTION)
    unset(CMAKE_REQUIRED_FLAGS)
    set(CMAKE_CXX_STANDARD ${micropb_saved_cxx_standard})
    set(CMAKE_TRY_COMPILE_TARGET_TYPE ${micropb_saved_try_compile_type})
    if(MICROPB_COMPILER_HAS_REFLECTION)
        set(MICROPB_HAS_REFLECTION TRUE)
    elseif(MICROPB_REFLECTION STREQUAL "ON")
        message(FATAL_ERROR "micropb: MICROPB_REFLECTION is ON, but the compiler does not support -std=c++26 -freflection")
    endif()
endif()

add_library(micropb INTERFACE)
target_include_directories(micropb INTERFACE src)
if(MICROPB_HAS_REFLECTION)
    target_compile_features(micropb INTERFACE cxx_std_26)
    target_compile_options(micropb INTERFACE -freflection)
    message(STATUS "micropb: reflection on")
else()
    target_compile_features(micropb INTERFACE cxx_std_23)
    message(STATUS "micropb: reflection off (C++23, field lists only)")
endif()

# For a compiler without reflection: the include directory and C++23 only. Each
# message type then needs a field list (`pb::fields_of`).
add_library(micropb_cxx23 INTERFACE)
target_include_directories(micropb_cxx23 INTERFACE src)
target_compile_features(micropb_cxx23 INTERFACE cxx_std_23)

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
    function(micropb_add_compile_fail_test name)
        add_library(${name} OBJECT EXCLUDE_FROM_ALL tests/compile_fail/${name}.cpp)
        target_link_libraries(${name} PRIVATE micropb)
        target_compile_options(${name} PRIVATE ${micropb_compile_options})
        add_test(NAME compile_fail.${name} COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target ${name})
        set_tests_properties(compile_fail.${name} PROPERTIES
            PASS_REGULAR_EXPRESSION "static assertion failed: pb: ${name}:"
            RESOURCE_LOCK micropb_build_dir)
    endfunction()

    micropb_add_test(test_wire)
    micropb_add_test(test_types)
    micropb_add_test(test_generated)
    micropb_add_test(test_field_list)
    micropb_add_test(test_scalars)
    if(MICROPB_HAS_REFLECTION)
        micropb_add_test(test_schema)
        micropb_add_test(test_codec)
        micropb_add_test(test_encode)
        micropb_add_test(test_decode)
        micropb_add_test(test_wire_compat)
    endif()

    # The tests that do not need reflection, built as C++23 and without -freflection. A message type
    # works there when it has a field list (`pb::fields_of`), which protoc-gen-micropb writes.
    function(micropb_add_cxx23_test name)
        add_executable(${name}_cxx23 tests/${name}.cpp)
        target_link_libraries(${name}_cxx23 PRIVATE micropb_cxx23)
        set_target_properties(${name}_cxx23 PROPERTIES CXX_STANDARD 23)
        target_compile_options(${name}_cxx23 PRIVATE ${micropb_compile_options} ${micropb_sanitize_options})
        target_link_options(${name}_cxx23 PRIVATE ${micropb_sanitize_link_options})
        add_test(NAME ${name}_cxx23 COMMAND ${name}_cxx23)
    endfunction()

    micropb_add_cxx23_test(test_wire)
    micropb_add_cxx23_test(test_types)
    micropb_add_cxx23_test(test_field_list)
    micropb_add_cxx23_test(test_generated)
    micropb_add_cxx23_test(test_scalars)

    # The tables must be constants in ROM: the program must have no code that makes them before main().
    if(NOT MICROPB_SANITIZE)
        add_test(NAME no_startup_code COMMAND ${CMAKE_NM} $<TARGET_FILE:test_generated_cxx23>)
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
        # Each compiler once, by its resolved path; the path as found is kept.
        set(micropb_arm_real_paths)
        foreach(gxx ${MICROPB_ARM_GXX_ON_PATH} ${micropb_arm_gnu_toolchains})
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
    file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/arm_reflection_check.cpp "
#if !defined(__cpp_impl_reflection)
#error no reflection
#endif
")
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
        # Does this compiler have reflection? Compile only.
        execute_process(COMMAND "${gxx}" -std=c++26 -freflection -c
            ${CMAKE_CURRENT_BINARY_DIR}/arm_reflection_check.cpp -o ${CMAKE_CURRENT_BINARY_DIR}/${prefix}/reflection_check.o
            RESULT_VARIABLE reflection_result OUTPUT_QUIET ERROR_QUIET)
        if(reflection_result EQUAL 0)
            set(micropb_${prefix}_reflection on)
        else()
            set(micropb_${prefix}_reflection off)
        endif()
        message(STATUS "micropb: Arm compiler ${prefix} (${gxx}), reflection ${micropb_${prefix}_reflection}")
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
            -DREFLECTION=${micropb_${prefix}_reflection} "-DGENERATOR=${CMAKE_GENERATOR}")
        if(CMAKE_MAKE_PROGRAM)
            list(APPEND consumer_command "-DMAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}")
        endif()
        add_test(NAME ${prefix}.consumer
            COMMAND ${consumer_command} -DMODE=build -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/consumer/run.cmake)
        set_tests_properties(${prefix}.consumer PROPERTIES RESOURCE_LOCK ${prefix}_consumer)
        if(micropb_${prefix}_reflection STREQUAL "off")
            add_test(NAME ${prefix}.consumer_reflection_on
                COMMAND ${consumer_command} -DMODE=reflection_on -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/consumer/run.cmake)
            set_tests_properties(${prefix}.consumer_reflection_on PROPERTIES RESOURCE_LOCK ${prefix}_consumer)
        endif()
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

    if(MICROPB_HAS_REFLECTION)
        foreach(rule
                duplicate_tag tag_zero tag_too_large tag_reserved oneof_tags_count
                variant_no_monostate variant_duplicate_alternative variant_repeated_alternative
                variant_optional_alternative variant_variant_alternative unmapped_type monostate_member
                span_buffer_in_variant span_buffer_in_optional span_buffer_in_repeated
                optional_repeated optional_optional optional_variant
                repeated_repeated repeated_optional repeated_variant
                tag_on_variant oneof_tags_on_non_variant decode_encode_only
                not_aggregate base_class inaccessible_member enum_too_wide
                bit_field member_offset_too_large decode_const_member decode_fixed_span
                field_list_type field_list_offset field_list_alternative recursive_message
                decode_needs_arena span_element_trivial)
            micropb_add_compile_fail_test(${rule})
        endforeach()
    endif()

    # Rules for a field list that a person wrote. These files are built as C++23 without reflection.
    function(micropb_add_list_compile_fail_test name rule)
        add_library(${name} OBJECT EXCLUDE_FROM_ALL tests/compile_fail/${name}.cpp)
        target_link_libraries(${name} PRIVATE micropb_cxx23)
        set_target_properties(${name} PROPERTIES CXX_STANDARD 23)
        target_compile_options(${name} PRIVATE ${micropb_compile_options})
        add_test(NAME compile_fail.${name} COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target ${name})
        set_tests_properties(compile_fail.${name} PROPERTIES
            PASS_REGULAR_EXPRESSION "static assertion failed: pb: ${rule}:"
            RESOURCE_LOCK micropb_build_dir)
    endfunction()

    micropb_add_list_compile_fail_test(no_field_list no_field_list)
    micropb_add_list_compile_fail_test(list_member_type optional_repeated)
    micropb_add_list_compile_fail_test(list_duplicate_number duplicate_tag)
    micropb_add_list_compile_fail_test(list_decode_alternative decode_encode_only)
    micropb_add_list_compile_fail_test(list_decode_fixed_span decode_fixed_span)
    micropb_add_list_compile_fail_test(list_alternative_on_plain_member field_list_alternative)
    micropb_add_list_compile_fail_test(list_alternative_index_too_large field_list_alternative)
    micropb_add_list_compile_fail_test(list_alternative_missing field_list_alternative)
    micropb_add_list_compile_fail_test(list_member_twice field_list_overlap)
    micropb_add_list_compile_fail_test(char_member unmapped_type)
    micropb_add_list_compile_fail_test(volatile_member unmapped_type)
    micropb_add_list_compile_fail_test(wchar_member unmapped_type)
    # `__int128` is an integer type only with GNU extensions, so this target turns them on.
    micropb_add_list_compile_fail_test(int128_member unmapped_type)
    micropb_add_list_compile_fail_test(zigzag_unsigned zigzag_unsigned)
    set_target_properties(int128_member PROPERTIES CXX_EXTENSIONS ON)

    if(MICROPB_HAS_REFLECTION)
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
endif()

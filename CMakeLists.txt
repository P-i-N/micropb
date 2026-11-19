cmake_minimum_required(VERSION 3.25)
project(micropb LANGUAGES CXX)

set(CMAKE_CXX_EXTENSIONS OFF)

# The headers use C++26 static reflection, so each target that links micropb
# gets these flags.
add_library(micropb INTERFACE)
target_include_directories(micropb INTERFACE src)
target_compile_features(micropb INTERFACE cxx_std_26)
target_compile_options(micropb INTERFACE -freflection)

# For a compiler without reflection: the include directory and C++23 only. Each
# message type then needs a field list (`pb::fields_of`).
add_library(micropb_cxx23 INTERFACE)
target_include_directories(micropb_cxx23 INTERFACE src)
target_compile_features(micropb_cxx23 INTERFACE cxx_std_23)

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
micropb_add_test(test_schema)
micropb_add_test(test_codec)
micropb_add_test(test_encode)
micropb_add_test(test_decode)
micropb_add_test(test_wire_compat)
micropb_add_test(test_generated)
micropb_add_test(test_field_list)

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

# The tables must be constants in ROM: the program must have no code that makes them before main().
if(NOT MICROPB_SANITIZE)
    add_test(NAME no_startup_code COMMAND ${CMAKE_NM} $<TARGET_FILE:test_generated_cxx23>)
    set_tests_properties(no_startup_code PROPERTIES FAIL_REGULAR_EXPRESSION "_GLOBAL__sub_I")
endif()

# The tests of tools/protoc-gen-micropb.py need Python, the Python package protobuf and protoc.
# Without them, CTest does not have the test `generator`. The other tests do not need these tools.
find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    set(micropb_plugin_tests ${CMAKE_CURRENT_SOURCE_DIR}/tests/generator/test_plugin.py)
    execute_process(COMMAND ${Python3_EXECUTABLE} ${micropb_plugin_tests} --check-tools
        RESULT_VARIABLE micropb_plugin_tools OUTPUT_QUIET ERROR_QUIET)
    if(micropb_plugin_tools EQUAL 0)
        add_test(NAME generator COMMAND ${Python3_EXECUTABLE} ${micropb_plugin_tests})
    else()
        message(STATUS "micropb: no protoc or no Python package protobuf, the test `generator` is off")
    endif()
endif()

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
        field_list_type field_list_offset field_list_alternative)
    micropb_add_compile_fail_test(${rule})
endforeach()

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
micropb_add_list_compile_fail_test(list_alternative_on_plain_member field_list_alternative)
micropb_add_list_compile_fail_test(list_alternative_index_too_large field_list_alternative)
micropb_add_list_compile_fail_test(list_alternative_missing field_list_alternative)
micropb_add_list_compile_fail_test(list_member_twice field_list_overlap)

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

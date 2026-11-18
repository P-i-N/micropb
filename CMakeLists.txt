cmake_minimum_required(VERSION 3.25)
project(micropb LANGUAGES CXX)

add_library(micropb INTERFACE)
target_include_directories(micropb INTERFACE src)

enable_testing()

set(micropb_compile_options -std=c++26 -freflection -fno-exceptions -fno-rtti -Wall -Wextra -Werror)

function(micropb_add_test name)
    add_executable(${name} tests/${name}.cpp)
    target_link_libraries(${name} PRIVATE micropb)
    target_compile_options(${name} PRIVATE ${micropb_compile_options})
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
micropb_add_test(test_encode)

foreach(rule
        duplicate_tag tag_zero tag_too_large tag_reserved oneof_tags_count
        variant_no_monostate variant_duplicate_alternative variant_repeated_alternative
        variant_optional_alternative variant_variant_alternative unmapped_type monostate_member
        span_buffer_in_variant span_buffer_in_optional span_buffer_in_repeated
        optional_repeated optional_optional optional_variant
        repeated_repeated repeated_optional repeated_variant
        tag_on_variant oneof_tags_on_non_variant)
    micropb_add_compile_fail_test(${rule})
endforeach()

cmake_minimum_required(VERSION 3.25)
project(micropb LANGUAGES CXX)

add_library(micropb INTERFACE)
target_include_directories(micropb INTERFACE src)

enable_testing()

function(micropb_add_test name)
    add_executable(${name} tests/${name}.cpp)
    target_link_libraries(${name} PRIVATE micropb)
    target_compile_options(${name} PRIVATE
        -std=c++26 -freflection -fno-exceptions -fno-rtti -Wall -Wextra -Werror)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

micropb_add_test(test_wire)
micropb_add_test(test_types)

cmake_minimum_required(VERSION 3.25)
project(consumer LANGUAGES CXX)

# MICROPB_SOURCE_DIR is the MicroPB folder. The test `arm_<version>.consumer` sets it.
add_subdirectory(${MICROPB_SOURCE_DIR} micropb)

add_library(consumer STATIC consumer.cpp)
target_include_directories(consumer PRIVATE ${MICROPB_SOURCE_DIR}/tests)
target_link_libraries(consumer PRIVATE micropb)
target_compile_options(consumer PRIVATE -fno-exceptions -fno-rtti -Wall -Wextra -Werror)

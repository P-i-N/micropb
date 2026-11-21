# Prints the size of the two size report objects. Arguments: SIZE_TOOL (GNU
# size), OBJECT_16, OBJECT_32.

if(NOT SIZE_TOOL OR NOT EXISTS "${SIZE_TOOL}")
    message("MicroPB size report: GNU size is not available")
    return()
endif()

# The sum of the text and data columns: code, constants and tables.
function(object_size path result)
    execute_process(COMMAND "${SIZE_TOOL}" "${path}" OUTPUT_VARIABLE output RESULT_VARIABLE status)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "size failed for ${path}")
    endif()
    string(REGEX MATCH "\n[ \t]*([0-9]+)[ \t]+([0-9]+)" unused "${output}")
    math(EXPR total "${CMAKE_MATCH_1} + ${CMAKE_MATCH_2}")
    set(${result} ${total} PARENT_SCOPE)
endfunction()

object_size("${OBJECT_16}" size_16)
object_size("${OBJECT_32}" size_32)
math(EXPR per_type "(${size_32} - ${size_16}) / 16")

message("MicroPB size report (text + data, -Os)")
message("16 message types: ${size_16} bytes")
message("32 message types: ${size_32} bytes (target: 15000 or less)")
message("one more message type: ${per_type} bytes (target: 300 or less)")

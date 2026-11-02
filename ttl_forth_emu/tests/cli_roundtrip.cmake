execute_process(COMMAND "${EMU}" --demo --cycles 20
    --export-flash "${OUTPUT}" RESULT_VARIABLE status OUTPUT_VARIABLE output)
if(NOT status EQUAL 0 OR NOT output MATCHES "ram\\[0\\]=42")
    message(FATAL_ERROR "Demo/flash export failed: ${status}: ${output}")
endif()
file(SIZE "${OUTPUT}" size)
if(NOT size EQUAL 524288)
    message(FATAL_ERROR "Wrong flash image size: ${size}")
endif()
execute_process(COMMAND "${EMU}" --flash "${OUTPUT}" --cycles 0
    RESULT_VARIABLE status OUTPUT_VARIABLE output)
if(NOT status EQUAL 0 OR NOT output MATCHES "ram\\[0\\]=00")
    message(FATAL_ERROR "Volatile RAM not reset: ${output}")
endif()
execute_process(COMMAND "${EMU}" --flash "${OUTPUT}" --cycles 20
    RESULT_VARIABLE status OUTPUT_VARIABLE output)
if(NOT status EQUAL 0 OR NOT output MATCHES "ram\\[0\\]=42")
    message(FATAL_ERROR "Restored I-RAM did not execute: ${output}")
endif()
execute_process(COMMAND "${EMU}" --flash "${CMAKE_CURRENT_LIST_FILE}" --cycles 0
    RESULT_VARIABLE status)
if(status EQUAL 0)
    message(FATAL_ERROR "Wrong-sized image unexpectedly accepted")
endif()

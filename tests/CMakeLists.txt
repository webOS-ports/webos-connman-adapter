# Unit tests. Built as part of the normal build; run them with
# `ctest` (or execute the binaries directly / on the target device).

add_executable(test-ssid-conversion test-ssid-conversion.c
            ${CMAKE_SOURCE_DIR}/src/utils.c)
target_link_libraries(test-ssid-conversion ${GLIB2_LDFLAGS})
add_test(NAME ssid-conversion COMMAND test-ssid-conversion)

add_executable(test-utils test-utils.c
            ${CMAKE_SOURCE_DIR}/src/utils.c)
target_link_libraries(test-utils ${GLIB2_LDFLAGS})
add_test(NAME utils COMMAND test-utils)

add_executable(test-json-utils test-json-utils.c
            ${CMAKE_SOURCE_DIR}/src/json_utils.c)
target_link_libraries(test-json-utils ${GLIB2_LDFLAGS} ${PBNJSON_C_LDFLAGS})
add_test(NAME json-utils COMMAND test-json-utils)

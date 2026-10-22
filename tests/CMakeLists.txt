# Offline regression: cmake -S tests -B build-tests
#                     cmake --build build-tests
#                     ctest --test-dir build-tests --output-on-failure
cmake_minimum_required(VERSION 3.16)
project(OpenSAEJ1939Tests C)

set(OPENSAE_J1939_TARGET_PLATFORM INTERNAL_CALLBACK)
add_subdirectory(.. opensaej1939)

enable_testing()
add_executable(transport_eom transport_eom.c)
target_link_libraries(transport_eom PRIVATE opensaej1939)
add_test(NAME transport_eom COMMAND transport_eom)

add_executable(multiple_can_contexts multiple_can_contexts.c)
target_link_libraries(multiple_can_contexts PRIVATE opensaej1939)
add_test(NAME multiple_can_contexts COMMAND multiple_can_contexts)

add_executable(const_contracts const_contracts.c)
target_link_libraries(const_contracts PRIVATE opensaej1939)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(const_contracts PRIVATE -Wall -Wextra -Werror -Wcast-qual)
endif()
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/const_contracts_data")
add_test(NAME const_contracts COMMAND const_contracts)
set_tests_properties(const_contracts PROPERTIES
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/const_contracts_data")

add_library(feedback_backend OBJECT ../Src/Hardware/CAN_Transmit_Receive.c)
target_include_directories(feedback_backend PUBLIC ../Src)
target_compile_definitions(feedback_backend PUBLIC OPENSAE_J1939_TARGET_PLATFORM=NO_PLATFORM
    $<TARGET_PROPERTY:opensaej1939,INTERFACE_COMPILE_DEFINITIONS>)
add_executable(feedback_contexts feedback_contexts.c)
target_link_libraries(feedback_contexts PRIVATE feedback_backend)
add_test(NAME feedback_contexts COMMAND feedback_contexts)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_executable(socketcan_contexts socketcan_contexts.c
        ../Src/Hardware/CAN_Transmit_Receive.c ../Src/Hardware/SocketCAN_Transmit_Receive.c)
    target_include_directories(socketcan_contexts PRIVATE ../Src)
    target_compile_definitions(socketcan_contexts PRIVATE OPENSAE_J1939_TARGET_PLATFORM=SOCKETCAN
        socket=test_socket setsockopt=test_setsockopt ioctl=test_ioctl bind=test_bind
        close=test_close write=test_write read=test_read usleep=test_usleep)
    add_test(NAME socketcan_contexts COMMAND socketcan_contexts)
endif()

option(OPENSAE_J1939_TEST_EXAMPLES "Build and run the host examples" OFF)
if(OPENSAE_J1939_TEST_EXAMPLES)
    file(GLOB_RECURSE EXAMPLE_FILES "${CMAKE_CURRENT_SOURCE_DIR}/../Src/Examples/*.txt")
    foreach(EXAMPLE_FILE IN LISTS EXAMPLE_FILES)
        if(EXAMPLE_FILE MATCHES "/Hardware/|/VIN Example |/Application Layer callback.txt$")
            continue()
        endif()
        file(RELATIVE_PATH EXAMPLE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../Src/Examples" "${EXAMPLE_FILE}")
        string(MAKE_C_IDENTIFIER "example_${EXAMPLE_PATH}" EXAMPLE_TARGET)
        set(EXAMPLE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/examples/${EXAMPLE_TARGET}")
        file(MAKE_DIRECTORY "${EXAMPLE_DIRECTORY}")
        configure_file("${EXAMPLE_FILE}" "${EXAMPLE_DIRECTORY}/main.c" COPYONLY)
        add_executable(${EXAMPLE_TARGET} "${EXAMPLE_DIRECTORY}/main.c")
        target_link_libraries(${EXAMPLE_TARGET} PRIVATE feedback_backend opensaej1939)
        if(NOT EXAMPLE_PATH STREQUAL "Open SAE J1939/Main.txt")
            add_test(NAME ${EXAMPLE_TARGET} COMMAND ${EXAMPLE_TARGET})
            set_tests_properties(${EXAMPLE_TARGET} PROPERTIES
                WORKING_DIRECTORY "${EXAMPLE_DIRECTORY}" TIMEOUT 10)
        endif()
    endforeach()
endif()

find_package(Boost REQUIRED filesystem)
find_package(jsoncpp CONFIG REQUIRED)

set(AIR_CLI_TARGET air-cli)
add_library(${AIR_CLI_TARGET}
    cli/AdminIPCClient.cpp
    cli/AdminInspectors.cpp
    cli/AdminIPCProtocol.cpp
    cli/AdminIPCServer.cpp
    cli/FallbackInspectors.cpp
    cli/InspectApplication.cpp
    cli/InspectConfig.cpp
    cli/InspectRenderer.cpp
    cli/InspectTypes.cpp)
target_include_directories(${AIR_CLI_TARGET} PUBLIC ${CMAKE_SOURCE_DIR})
target_link_libraries(${AIR_CLI_TARGET} PUBLIC ${COMMAND_HELPER_LIB} ${INIT_LIB}
    ${PBFT_INIT_LIB} JsonCpp::JsonCpp)

aux_source_directory(. SRC_LIST)

set(BINARY_NAME fisco-bcos)
add_executable(${BINARY_NAME} ${SRC_LIST})
target_link_libraries(${BINARY_NAME} PUBLIC ${INIT_LIB} ${PBFT_INIT_LIB} ${COMMAND_HELPER_LIB}
    ${GATEWAY_TARGET} ${AIR_CLI_TARGET})
set_target_properties(${BINARY_NAME} PROPERTIES UNITY_BUILD "ON")

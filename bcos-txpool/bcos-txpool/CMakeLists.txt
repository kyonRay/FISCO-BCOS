file(GLOB_RECURSE SRC_LIST "*.cpp")
file(GLOB_RECURSE HEADERS "*.h")
aux_source_directory(. SRC_LIST)
include_directories(.)

# generate sync protobuf files

set(MESSAGES_PROTOS TxsSync.proto)
set(SYNC_PROTO_GENERATE_DIR ${SYNC_PROTO_GENERATE_BASE_DIR}/${SYNC_PROTO_SUB_DIR})
include(GenerateSources)
protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS ${MESSAGES_PROTOS} ${PROTO_PATH} "sync/protocol/proto" ${SYNC_PROTO_GENERATE_BASE_DIR} ${SYNC_PROTO_GENERATE_DIR})

aux_source_directory(./txpool SRC_LIST)
include_directories(./txpool)
aux_source_directory(./txpool/storage SRC_LIST)
include_directories(./txpool/storage)
aux_source_directory(./txpool/validator SRC_LIST)
include_directories(./txpool/validator)

aux_source_directory(./sync/ SRC_LIST)
include_directories(./sync/)

aux_source_directory(./sync/protocol/PB SRC_LIST)
include_directories(./sync/protocol/PB)

add_library(${TXPOOL_TARGET} ${SRC_LIST} ${PROTO_SRCS} ${HEADERS} ${PROTO_HDRS})

find_package(TBB CONFIG REQUIRED)
find_package(Protobuf CONFIG REQUIRED)
target_include_directories(${TXPOOL_TARGET} PUBLIC .)
target_include_directories(${TXPOOL_TARGET} PUBLIC ${CMAKE_CURRENT_BINARY_DIR}/bcos-txpool)
target_link_libraries(${TXPOOL_TARGET} PUBLIC ${LEDGER_TARGET} TBB::tbb protobuf::libprotobuf bcos-protocol ${UTILITIES_TARGET} ${TOOL_TARGET})
set_target_properties(${TXPOOL_TARGET} PROPERTIES UNITY_BUILD "ON")
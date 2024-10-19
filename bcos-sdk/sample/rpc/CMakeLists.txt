set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DSTATICLIB")

file(GLOB SRC_LIST "*.cpp")
file(GLOB HEADERS "*.h")

add_executable(rpc ${SRC_LIST} ${HEADERS})
target_link_libraries(rpc PUBLIC ${BCOS_CPP_SDK_TARGET})
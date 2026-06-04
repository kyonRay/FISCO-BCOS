# OP-Stack L2 Solidity contracts. Built with the Foundry `forge` toolchain,
# not the C++ build. Only wired in when WITH_L2_CONTRACTS=ON (default OFF).
find_program(FORGE forge REQUIRED)

add_custom_target(l2-contracts-build
    COMMAND ${FORGE} build --root ${CMAKE_CURRENT_SOURCE_DIR}
    COMMAND ${FORGE} build --root ${CMAKE_CURRENT_SOURCE_DIR} --contracts vendor/op-fork/src
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Building OP-Stack L2 Solidity contracts via forge")

add_custom_target(l2-contracts-test
    COMMAND ${FORGE} test -vvv --root ${CMAKE_CURRENT_SOURCE_DIR}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    DEPENDS l2-contracts-build
    COMMENT "Running L2 Solidity Foundry tests")

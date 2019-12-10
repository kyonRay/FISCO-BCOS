#!/bin/bash

export FISCO_BCOS_TEST_PATH=/FISCO-BCOS/test/data 
/FISCO-BCOS/build/bin/test-fisco-bcos --run_test=pbftConsensusTest

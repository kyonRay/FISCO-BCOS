#!/bin/bash

if [ -e nodes ]; then
    bash ./nodes/127.0.0.1/stop_all.sh
    rm -rf nodes
fi


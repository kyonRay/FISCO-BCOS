#!/bin/bash

if [ -e nodes ]; then
    bash ./nodes/127.0.0.1/stop_all.sh
    rm -rf ./nodes
fi
bash build_chain.sh -e ../build/bin/fisco-bcos -T -l "127.0.0.1:4"
bash nodes/127.0.0.1/start_all.sh
ps -ef | grep -v grep | grep fisco-bcos
if [ ! -e console ]; then
    if [ ! -e console.tar.gz ]; then
        bash <(curl -s https://raw.githubusercontent.com/FISCO-BCOS/console/master/tools/download_console.sh)
    else
        tar fxz ./console.tar.gz
    fi
    cp -n console/conf/applicationContext-sample.xml console/conf/applicationContext.xml
fi
cp nodes/127.0.0.1/sdk/* console/conf/
cd ./console && bash start.sh

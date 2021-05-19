#!/bin/bash

if [ ! -d MiniJson ]; then
    git clone https://github.com/zsmj2017/MiniJson
    cd MiniJson
    cmake .
    make -j
fi

g++ ../http.cpp demo.cpp MiniJson/Source/libJson.a -I.. -I MiniJson/Source/include -pthread -o tinyhttp_demo

#!/usr/bin/bash

cmake .. \
    -DRETDEC_TESTS=on \
    -DCMAKE_BUILD_TYPE=$BUILD_TYPE \
    -DRETDEC_DEV_TOOLS=ON \
    -DCMAKE_INSTALL_PREFIX=install \

cmake --build . -j $NUMBER_OF_PROCESSORS --config $BUILD_TYPE --target install -- -m

#!/usr/bin/bash

cmake .. \
	-DCMAKE_BUILD_TYPE=$BUILD_TYPE \
	-DRETDEC_TESTS=on \
	-DRETDEC_DEV_TOOLS=ON \
	-DCMAKE_INSTALL_PREFIX=install

make -j$(nproc) install

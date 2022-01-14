#!/usr/bin/bash

export PATH="/usr/local/opt/openssl@1.1/bin:$PATH"
export OPENSSL_ROOT_DIR="/usr/local/opt/openssl@1.1/"
cmake .. \
	-DRETDEC_TESTS=on \
	-DCMAKE_BUILD_TYPE=$BUILD_TYPE \
	-DRETDEC_DEV_TOOLS=ON \
	-DCMAKE_INSTALL_PREFIX=install \

make -j$(sysctl -n hw.ncpu) install

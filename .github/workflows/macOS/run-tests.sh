#!/usr/bin/bash

cat <<EOF > $BUILD_DIR/retdec-regression-tests-framework/config_local.ini
[runner]
; Path to the extracted Clang package containing subdirectories such as bin, include, lib, share.
clang_dir = $BUILD_DIR/clang
; Path to the cloned repository containing regression tests.
tests_root_dir = $BUILD_DIR/retdec-regression-tests
; Path to the RetDec's installation directory.
retdec_install_dir = $BUILD_DIR/install
EOF

cd "$BUILD_DIR/retdec-regression-tests-framework"

python3 -m venv .venv

. .venv/bin/activate
pip3 install -r requirements.txt

python3 ./runner.py

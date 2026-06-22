#!/usr/bin/env bash
#
# Copyright (c) 2015-2026 The Bitcoin Core developers
# Copyright (c) 2026 The Kvanta5 Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

export LC_ALL=C.UTF-8

export CMAKE_GENERATOR="Ninja"
export KVANTA5_CONFIG="-DBUILD_FOR_FUZZING=ON"
export CI_OS_NAME="macos"
export NO_DEPENDS=1
export OSX_SDK=""
export RUN_UNIT_TESTS=false
export RUN_FUNCTIONAL_TESTS=false
export RUN_FUZZ_TESTS=true
export GOAL="all"

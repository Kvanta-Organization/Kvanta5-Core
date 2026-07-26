# Copyright (c) 2026 The Kvanta5 Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://opensource.org/license/mit/.

include_guard(GLOBAL)

find_package(Threads REQUIRED)

find_package(lz4 1.10.0 EXACT CONFIG REQUIRED)
find_package(zstd 1.5.7 EXACT CONFIG REQUIRED)

# RocksDB 10.10.1 exports references to these target names, while the pinned
# upstream codec packages export LZ4::lz4_static and
# zstd::libzstd_static.
if(NOT TARGET lz4::lz4)
  add_library(lz4::lz4 INTERFACE IMPORTED GLOBAL)
  set_target_properties(lz4::lz4 PROPERTIES
    INTERFACE_LINK_LIBRARIES "LZ4::lz4_static"
  )
endif()

if(NOT TARGET zstd::zstd)
  add_library(zstd::zstd INTERFACE IMPORTED GLOBAL)
  set_target_properties(zstd::zstd PROPERTIES
    INTERFACE_LINK_LIBRARIES "zstd::libzstd_static"
  )
endif()

find_package(RocksDB 10.10.1 EXACT CONFIG REQUIRED)

add_library(kvanta5_rocksdb INTERFACE)

target_link_libraries(kvanta5_rocksdb
  INTERFACE
    RocksDB::rocksdb
    Threads::Threads
    ${CMAKE_DL_LIBS}
)

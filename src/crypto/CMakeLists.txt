# Copyright (c) 2023-present The Kvanta5 Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://opensource.org/license/mit/.

add_library(kvanta5_crypto STATIC EXCLUDE_FROM_ALL
  aes.cpp
  chacha20.cpp
  chacha20poly1305.cpp
  hex_base.cpp
  hkdf_sha256_32.cpp
  hmac_sha256.cpp
  hmac_sha512.cpp
  muhash.cpp
  poly1305.cpp
  ripemd160.cpp
  sha1.cpp
  sha256.cpp
  sha256_sse4.cpp
  sha3.cpp
  sha512.cpp
  siphash.cpp
  ../support/cleanse.cpp
)

add_library(kvanta5_mldsa87 STATIC EXCLUDE_FROM_ALL
  pq/mldsa87/mldsa87.cpp
  pq/mldsa87/upstream/ref/fips202.c
  pq/mldsa87/upstream/ref/ntt.c
  pq/mldsa87/upstream/ref/packing.c
  pq/mldsa87/upstream/ref/poly.c
  pq/mldsa87/upstream/ref/polyvec.c
  pq/mldsa87/upstream/ref/randombytes.c
  pq/mldsa87/upstream/ref/reduce.c
  pq/mldsa87/upstream/ref/rounding.c
  pq/mldsa87/upstream/ref/sign.c
  pq/mldsa87/upstream/ref/symmetric-shake.c
)

target_include_directories(kvanta5_mldsa87
  PRIVATE
    ${PROJECT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/pq/mldsa87/upstream/ref
)

target_compile_definitions(kvanta5_mldsa87
  PRIVATE
    DILITHIUM_MODE=5
)

target_link_libraries(kvanta5_mldsa87
  PRIVATE
    core_interface
)

target_link_libraries(kvanta5_crypto
  PRIVATE
    core_interface
)

if(HAVE_SSE41)
  target_compile_definitions(kvanta5_crypto PRIVATE ENABLE_SSE41)
  target_sources(kvanta5_crypto PRIVATE sha256_sse41.cpp)
  set_property(SOURCE sha256_sse41.cpp PROPERTY
    COMPILE_OPTIONS ${SSE41_CXXFLAGS}
  )
endif()

if(HAVE_AVX2)
  target_compile_definitions(kvanta5_crypto PRIVATE ENABLE_AVX2)
  target_sources(kvanta5_crypto PRIVATE sha256_avx2.cpp)
  set_property(SOURCE sha256_avx2.cpp PROPERTY
    COMPILE_OPTIONS ${AVX2_CXXFLAGS}
  )
endif()

if(HAVE_SSE41 AND HAVE_X86_SHANI)
  target_compile_definitions(kvanta5_crypto PRIVATE ENABLE_SSE41 ENABLE_X86_SHANI)
  target_sources(kvanta5_crypto PRIVATE sha256_x86_shani.cpp)
  set_property(SOURCE sha256_x86_shani.cpp PROPERTY
    COMPILE_OPTIONS ${X86_SHANI_CXXFLAGS}
  )
endif()

if(HAVE_ARM_SHANI)
  target_compile_definitions(kvanta5_crypto PRIVATE ENABLE_ARM_SHANI)
  target_sources(kvanta5_crypto PRIVATE sha256_arm_shani.cpp)
  set_property(SOURCE sha256_arm_shani.cpp PROPERTY
    COMPILE_OPTIONS ${ARM_SHANI_CXXFLAGS}
  )
endif()

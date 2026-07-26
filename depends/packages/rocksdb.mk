package=rocksdb

$(package)_version=10.10.1
$(package)_download_path=https://github.com/facebook/rocksdb/archive/refs/tags/
$(package)_file_name=v$($(package)_version).tar.gz
$(package)_sha256_hash=df2ff348f3fac8578fd4b727eee7267aaf90cd403c99b55e898d1db63fa8cff5
$(package)_dependencies=lz4 zstd
$(package)_build_subdir=build

define $(package)_set_vars
  $(package)_config_opts=-DCMAKE_BUILD_TYPE=None
  $(package)_config_opts+=-DCMAKE_CXX_STANDARD=20
  $(package)_config_opts+=-DCMAKE_CXX_STANDARD_REQUIRED=ON
  $(package)_config_opts+=-DCMAKE_CXX_EXTENSIONS=OFF
  $(package)_config_opts+=-DCMAKE_POSITION_INDEPENDENT_CODE=ON

  $(package)_config_opts+=-DROCKSDB_BUILD_SHARED=OFF

  $(package)_config_opts+=-DWITH_LZ4=ON
  $(package)_config_opts+=-DWITH_ZSTD=ON
  $(package)_config_opts+=-DWITH_SNAPPY=OFF
  $(package)_config_opts+=-DWITH_ZLIB=OFF
  $(package)_config_opts+=-DWITH_BZ2=OFF
  $(package)_config_opts+=-DWITH_XPRESS=OFF

  $(package)_config_opts+=-Dlz4_ROOT_DIR=$(host_prefix)
  $(package)_config_opts+=-Dlz4_INCLUDE_DIRS=$(host_prefix)/include
  $(package)_config_opts+=-Dlz4_LIBRARIES=$(host_prefix)/lib/liblz4.a

  $(package)_config_opts+=-Dzstd_ROOT_DIR=$(host_prefix)
  $(package)_config_opts+=-Dzstd_INCLUDE_DIRS=$(host_prefix)/include
  $(package)_config_opts+=-Dzstd_LIBRARIES=$(host_prefix)/lib/libzstd.a

  $(package)_config_opts+=-DWITH_GFLAGS=OFF
  $(package)_config_opts+=-DWITH_TESTS=OFF
  $(package)_config_opts+=-DWITH_BENCHMARK_TOOLS=OFF
  $(package)_config_opts+=-DWITH_TOOLS=OFF

  $(package)_config_opts+=-DWITH_JEMALLOC=OFF
  $(package)_config_opts+=-DWITH_LIBURING=OFF
  $(package)_config_opts+=-DWITH_NUMA=OFF
  $(package)_config_opts+=-DWITH_TBB=OFF

  $(package)_config_opts+=-DUSE_RTTI=OFF
  $(package)_config_opts+=-DPORTABLE=ON
  $(package)_config_opts+=-DFAIL_ON_WARNINGS=OFF

  $(package)_config_opts_mingw32+=-DROCKSDB_INSTALL_ON_WINDOWS=ON

  $(package)_cxxflags+=-fdebug-prefix-map=$($(package)_extract_dir)=/usr
  $(package)_cxxflags+=-fmacro-prefix-map=$($(package)_extract_dir)=/usr
endef

define $(package)_config_cmds
  $($(package)_cmake) -S .. -B .
endef

define $(package)_build_cmds
  $(MAKE)
endef

define $(package)_stage_cmds
  $(MAKE) DESTDIR=$($(package)_staging_dir) install
endef

define $(package)_postprocess_cmds
  rm -rf bin share
endef

package=lz4

$(package)_version=1.10.0
$(package)_download_path=https://github.com/lz4/lz4/archive/refs/tags/
$(package)_file_name=v$($(package)_version).tar.gz
$(package)_sha256_hash=537512904744b35e232912055ccf8ec66d768639ff3abe5788d90d792ec5f48b
$(package)_build_subdir=build/cmake/build

define $(package)_set_vars
  $(package)_config_opts=-DCMAKE_BUILD_TYPE=None
  $(package)_config_opts+=-DBUILD_SHARED_LIBS=OFF
  $(package)_config_opts+=-DBUILD_STATIC_LIBS=ON
  $(package)_config_opts+=-DLZ4_BUILD_CLI=OFF
  $(package)_config_opts+=-DLZ4_POSITION_INDEPENDENT_LIB=ON
  $(package)_cflags+=-fdebug-prefix-map=$($(package)_extract_dir)=/usr
  $(package)_cflags+=-fmacro-prefix-map=$($(package)_extract_dir)=/usr
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

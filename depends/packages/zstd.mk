package=zstd

$(package)_version=1.5.7
$(package)_download_path=https://github.com/facebook/zstd/archive/refs/tags/
$(package)_file_name=v$($(package)_version).tar.gz
$(package)_sha256_hash=37d7284556b20954e56e1ca85b80226768902e2edabd3b649e9e72c0c9012ee3
$(package)_build_subdir=build/cmake/build

define $(package)_set_vars
  $(package)_config_opts=-DCMAKE_BUILD_TYPE=None
  $(package)_config_opts+=-DZSTD_BUILD_SHARED=OFF
  $(package)_config_opts+=-DZSTD_BUILD_STATIC=ON
  $(package)_config_opts+=-DZSTD_BUILD_PROGRAMS=OFF
  $(package)_config_opts+=-DZSTD_BUILD_TESTS=OFF
  $(package)_config_opts+=-DZSTD_BUILD_CONTRIB=OFF
  $(package)_config_opts+=-DZSTD_BUILD_COMPRESSION=ON
  $(package)_config_opts+=-DZSTD_BUILD_DECOMPRESSION=ON
  $(package)_config_opts+=-DZSTD_BUILD_DICTBUILDER=ON
  $(package)_config_opts+=-DZSTD_BUILD_DEPRECATED=OFF
  $(package)_config_opts+=-DZSTD_LEGACY_SUPPORT=OFF
  $(package)_config_opts+=-DZSTD_MULTITHREAD_SUPPORT=OFF
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

package=czmq
$(package)_version=4.0.1
$(package)_download_path=https://github.com/zeromq/czmq/releases/download/v$($(package)_version)/
$(package)_file_name=$(package)-$($(package)_version).tar.gz
$(package)_sha256_hash=0fc7294d983df7c2d6dc9b28ad7cd970377d25b33103aa82932bdb7fa6207215
$(package)_dependencies=zeromq

define $(package)_set_vars
$(package)_config_opts=--disable-shared --without-test_zgossip --without-makecert
$(package)_config_opts_mingw32=--enable-mingw
$(package)_config_opts_linux=--with-pic
endef

define $(package)_config_cmds
  $($(package)_autoconf)
endef

define $(package)_build_cmds
  $(MAKE)
endef

define $(package)_stage_cmds
  $(MAKE) DESTDIR=$($(package)_staging_dir) install
endef


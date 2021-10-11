project('tiu', 'c',
	version : '0.0.1',
	license : 'LGPLv2.1+',
	default_options : [
		'buildtype=debugoptimized',
		'default_library=shared',
		'b_pie=true',
		'warning_level=3',
        	'c_std=gnu99',
		'prefix=/usr',
		'sysconfdir=/etc',
		'localstatedir=/var',
		'werror=true',
	],
	meson_version : '>= 0.49.0',
)

cc = meson.get_compiler('c')
pkg = import('pkgconfig')
inc = include_directories('include')

add_project_arguments(['-D_GNU_SOURCE=1',
		       '-DXTSTRINGDEFINES',
		       '-D_FORTIFY_SOURCE=2'], language : 'c')

possible_cc_flags = [
		  '-fstack-protector-strong',
		  '-funwind-tables',
		  '-fasynchronous-unwind-tables',
		  '-fstack-clash-protection',
		  '-Werror=return-type',
		  '-flto=8',
		  '-Wbad-function-cast',
		  '-Wcast-align',
		  '-Wcast-qual',
		  '-Winline',
		  '-Wmissing-declarations',
		  '-Wmissing-prototypes',
		  '-Wnested-externs',
		  '-Wshadow',
		  '-Wstrict-prototypes',
		  '-Wundef',
		  ]
add_project_arguments(cc.get_supported_arguments(possible_cc_flags), language : 'c')

libtiu_src = files(
  'lib/extract_image.c',
  'lib/casync_extract.c',
  'lib/casync_make.c',
  'lib/check_archive.c',
  'lib/create_images.c',
  'lib/dm.c',
  'lib/install.c',
  'lib/mount_archive.c',
  'lib/network.c',
  'lib/rm_rf.c',
  'lib/tiu_errors.c',
  'lib/update.c',
  'lib/variables.c',
  'lib/verity_hash.c',
  'lib/workdir.c',
)

tiu_src = files(
  'src/tiu.c',
)

mapfile = 'lib/libtiu.map'
version_flag = '-Wl,--version-script,@0@/@1@'.format(meson.current_source_dir(), mapfile)

openssl_dep = dependency('libcrypto')
gio_dep = dependency('gio-2.0')
gio_unix_dep = dependency('gio-unix-2.0')
libeconf_dep = dependency('libeconf')
libcurl_dep = dependency('libcurl')

lib = library(
  'tiu',
  libtiu_src,
  include_directories : inc,
  install : true,
  link_args : version_flag,
  link_depends : mapfile,
  version : meson.project_version(),
  soversion : '0',
  dependencies : [gio_dep, gio_unix_dep, libeconf_dep, openssl_dep,
		libcurl_dep, ],
)

install_headers('include/tiu.h')

pkg.generate(
  lib,
  name : 'libtiu',
  description : 'library to manage image snapshots',
  version : meson.project_version(),
)

libtiu_dep = declare_dependency(
  link_with : lib,
  include_directories : inc,
  dependencies : gio_dep,
)

tiu = executable(
  'tiu',
  tiu_src,
  dependencies : libtiu_dep,
  install : true,
)

tools = files(
  'tools/finish',
  'tools/setup-disk',
  'tools/setup-root',
  'tools/setup-bootloader',
  'tools/setup-usr-snapper',
  'tools/populate-etc',
)

install_data(
  tools,
  install_dir : join_paths(get_option('libexecdir'), 'tiu'),
)

# Unit tests
#subdir('tests')

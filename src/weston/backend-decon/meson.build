# Meson build file for Weston DECON backend

project('weston-backend-decon', 'c',
  version: '1.0.0',
  default_options: [
    'c_std=gnu99',
    'warning_level=2',
  ],
)

cc = meson.get_compiler('c')

dep_libweston = dependency('libweston-14', version: '>= 14.0.0')
dep_pixman = dependency('pixman-1')
dep_wayland_server = dependency('wayland-server')
dep_drm = dependency('libdrm')

backend_decon_srcs = [
  'decon.c',
  '../../compositor/decon_backend.c',
]

shared_module(
  'decon-backend',
  backend_decon_srcs,
  include_directories: include_directories('.', '../../compositor'),
  dependencies: [
    dep_libweston,
    dep_pixman,
    dep_wayland_server,
    dep_drm,
  ],
  name_prefix: '',
  install: true,
  install_dir: dep_libweston.get_variable(pkgconfig: 'libdir') / 'libweston-14',
)

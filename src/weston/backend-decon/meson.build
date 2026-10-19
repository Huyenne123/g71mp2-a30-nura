if not get_option('backend-decon')
	subdir_done()
endif

config_h.set('BUILD_DECON_COMPOSITOR', '1')

srcs_decon = [
	'decon.c',
	'decon_backend.c',
	presentation_time_server_protocol_h,
]

dep_hwcomposer = dependency('hwcomposer-egl', required: false)
c_args_decon = [ '-DBUILD_DECON_INPUT=1' ]
if dep_hwcomposer.found()
	c_args_decon += [ '-DENABLE_DECON_GL=1' ]
endif

plugin_decon = shared_library(
	'decon-backend',
	srcs_decon,
	include_directories: [ common_inc, include_directories('.') ],
	c_args: c_args_decon,
	dependencies: [
		dep_libweston_private,
		dep_libdrm_headers,
		dep_pixman,
		dep_libinput_backend,
		dependency('libudev', version: '>= 136'),
		dep_hwcomposer,
	],
	name_prefix: '',
	install: true,
	install_dir: dir_module_libweston,
)
env_modmap += 'decon-backend.so=@0@;'.format(plugin_decon.full_path())
install_headers(backend_decon_h, subdir: dir_include_libweston_install)

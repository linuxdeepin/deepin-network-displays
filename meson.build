project('deepin-network-displays', 'c',
  version: '0.90.5',
  meson_version: '>= 0.46.1',
)

gnome = import('gnome')
i18n = import('i18n')

datadir = get_option('datadir')
icondir = join_paths(datadir, 'icons')

config_h = configuration_data()
config_h.set_quoted('PACKAGE_VERSION', meson.project_version())
config_h.set_quoted('GETTEXT_PACKAGE', 'deepin-network-displays')
config_h.set_quoted('LOCALEDIR', join_paths(get_option('prefix'), get_option('localedir')))
configure_file(
  output: 'deepin-network-displays-config.h',
  configuration: config_h,
)
add_project_arguments([
  '-I' + meson.build_root(),
  '-DG_LOG_DOMAIN="Dnd"',
], language: 'c')

enable_firewalld_zone = get_option('firewalld_zone')

subdir('src')
subdir('po')

meson.add_install_script('build-aux/meson/postinstall.py')
install_subdir('misc/lib',install_dir:'/')
install_subdir('misc/usr',install_dir:'/')
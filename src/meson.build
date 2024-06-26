
subdir('wfd')

deepin_nd_sources = [
  'main.c',
  'nd-window.c',
  'nd-codec-install.c',
  'nd-firewalld.c',
  'nd-sink-list.c',
  'nd-sink-row.c',
  'nd-sink.c',
  'nd-provider.c',
  'nd-meta-sink.c',
  'nd-meta-provider.c',
  'nd-wfd-mice-sink.c',
  'nd-wfd-mice-provider.c',
  'nd-wfd-p2p-sink.c',
  'nd-wfd-p2p-provider.c',
  'nd-nm-device-registry.c',
  'nd-dummy-provider.c',
  'nd-dummy-wfd-sink.c',
  'nd-screencast-portal.c',
  'nd-pulseaudio.c',
  'nd-dbus-manager.c',
  'nd-dbus-sink.c',
]

enum_headers = files('nd-sink.h')

deepin_nd_sources += gnome.mkenums_simple(
  'nd-enum-types',
  sources: enum_headers,
)

deepin_nd_deps = [
  dependency('avahi-client'),
  dependency('avahi-gobject'),
  dependency('gio-2.0', version: '>= 2.50'),
  dependency('gstreamer-1.0', version: '>= 1.14'),
  dependency('gstreamer-pbutils-1.0', version: '>= 1.14'),
  dependency('gstreamer-plugins-base-1.0'),
  dependency('gstreamer-rtsp-server-1.0'),
  dependency('gtk+-3.0', version: '>= 3.22'),
  dependency('libnm', version: '>= 1.15'),
  dependency('libpulse-mainloop-glib'),
]

deepin_nd_deps += wfd_server_deps

deepin_nd_sources += gnome.compile_resources('deepin-nd-resources',
  'gnome-network-displays.gresource.xml',
  c_name: 'gnome_screencast'
)

executable('deepin-network-display-daemon',
  deepin_nd_sources,
  dependencies: deepin_nd_deps,
  install: true,
  link_with: wfd_server,
)

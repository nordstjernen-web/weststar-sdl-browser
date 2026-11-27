# libgobject, the files the browser reaches (see ../meson.build).

gobject_visibility_h = custom_target(
  output: 'gobject-visibility.h',
  command: [python, gen_visibility_macros, glib_version, 'visibility-macros',
            'GOBJECT', '@OUTPUT@'],
)

gobject_inc = include_directories('.')

# GObject's last-resort signal marshaller calls through libffi. Every
# signal reached here (GObject's own "notify") names its marshaller, so
# that code is never run: ns-ffi/ffi.h declares libffi's few names and
# aborts if one is called, and libffi is not needed.
gobject_lib = static_library('gobject-2.0',
  files(
    'gatomicarray.c', 'gboxed.c', 'gclosure.c', 'genums.c', 'gmarshal.c',
    'gobject.c', 'gparam.c', 'gparamspecs.c', 'gsignal.c', 'gtype.c',
    'gtypeplugin.c', 'gvalue.c', 'gvaluearray.c', 'gvaluetransform.c',
    'gvaluetypes.c',
  ) + [gobject_visibility_h],
  c_args: glib_build_args + [
    '-DG_LOG_DOMAIN="GLib-GObject"',
    '-DGOBJECT_COMPILATION',
  ],
  include_directories: [include_directories('../ns-ffi'), root_inc, glib_inc,
                        gobject_inc],
  dependencies: glib_dep,
  pic: true,
  install: false,
)

gobject_dep = declare_dependency(
  link_with: gobject_lib,
  sources: [gobject_visibility_h],
  include_directories: [root_inc, gobject_inc],
  dependencies: glib_dep,
  version: glib_version,
)

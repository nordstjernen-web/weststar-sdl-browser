# Native replacement for Mbed TLS's CMake build, as Weststar uses it: only
# the crypto library (libmbedcrypto) as one static library, with the stock
# include/mbedtls/mbedtls_config.h. TLS and X.509 are left out; HTTPS goes
# through libcurl and whatever TLS library it was built with.
project('mbedtls', 'c', version: '3.6.8', license: 'Apache-2.0 OR GPL-2.0-or-later',
        meson_version: '>=1.0')

cc = meson.get_compiler('c')

# The CMake build lists library/*.c by hand; take every file but the TLS,
# X.509 and PKCS#7 ones, so a release bump needs no file list update.
list_sources = '''
import pathlib, sys
lib = pathlib.Path(sys.argv[1]) / "library"
skip = ("ssl_", "x509", "mps_")
skip_files = {"debug.c", "net_sockets.c", "pkcs7.c"}
files = [p for p in sorted(lib.glob("*.c"))
         if not p.name.startswith(skip) and p.name not in skip_files]
sys.stdout.write("\n".join("library/" + p.name for p in files))
'''
python = find_program('python3', 'python')
srcs = run_command(python, '-c', list_sources, meson.current_source_dir(),
                   check: true).stdout().strip().split('\n')

deps = []
if host_machine.system() == 'windows'
  # entropy_poll.c draws from BCryptGenRandom.
  deps += cc.find_library('bcrypt')
endif

# Built outside link-time optimisation: under it GCC reports its own false
# positives in Mbed TLS (-Wstringop-overflow in bignum_core.c) at the final
# link, where -w no longer reaches them.
mbedcrypto_lib = static_library('mbedcrypto', srcs,
  c_args: ['-w'] + cc.get_supported_arguments('-fno-lto'),
  include_directories: include_directories('include', 'library'),
  dependencies: deps,
  pic: true,
  install: false)

mbedcrypto_dep = declare_dependency(link_with: mbedcrypto_lib,
  include_directories: include_directories('include', is_system: true),
  dependencies: deps,
  version: meson.project_version())
meson.override_dependency('mbedcrypto', mbedcrypto_dep)

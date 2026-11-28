# Native replacement for Mbed TLS's CMake build, as Weststar uses it: the
# crypto library (libmbedcrypto) as one static library, with the stock
# include/mbedtls/mbedtls_config.h, for Web Crypto. The X.509 and TLS
# libraries are built only when something links them: libcurl's Mbed TLS
# backend in the Windows XP build (subprojects/curl.wrap), where HTTPS
# cannot go through the system's TLS.
project('mbedtls', 'c', version: '3.6.8', license: 'Apache-2.0 OR GPL-2.0-or-later',
        meson_version: '>=1.0')

cc = meson.get_compiler('c')

# The CMake build lists library/*.c by hand; take them by prefix, as it
# splits them, so a release bump needs no file list update.
list_sources = '''
import pathlib, sys
lib = pathlib.Path(sys.argv[1]) / "library"
part = sys.argv[2]
tls_prefix = ("ssl_", "mps_")
tls_files = {"debug.c", "net_sockets.c"}
def which(p):
    if p.name.startswith("x509") or p.name == "pkcs7.c":
        return "x509"
    if p.name.startswith(tls_prefix) or p.name in tls_files:
        return "tls"
    return "crypto"
files = [p for p in sorted(lib.glob("*.c")) if which(p) == part]
sys.stdout.write("\n".join("library/" + p.name for p in files))
'''
python = find_program('python3', 'python')
srcs = {}
foreach part : ['crypto', 'x509', 'tls']
  srcs += {part: run_command(python, '-c', list_sources,
                             meson.current_source_dir(), part,
                             check: true).stdout().strip().split('\n')}
endforeach

deps = []
config_args = []
xp_args = []
if host_machine.system() == 'windows'
  win32_winnt = cc.get_define('_WIN32_WINNT', prefix: '#include <windows.h>')
  if win32_winnt != '' and cc.compute_int(win32_winnt) < 0x600
    # Windows XP has no BCryptGenRandom, which entropy_poll.c draws from:
    # entropy comes from RtlGenRandom instead (ns-entropy-winxp.c), as
    # Mbed TLS's hardware entropy source. The consumers of the headers see
    # the same configuration.
    config_args += ['-DMBEDTLS_NO_PLATFORM_ENTROPY',
                    '-DMBEDTLS_ENTROPY_HARDWARE_ALT']
    srcs += {'crypto': srcs['crypto'] + ['ns-entropy-winxp.c']}
    deps += cc.find_library('advapi32')
    # Nor vsnprintf_s (ns-winxp.h).
    xp_args = ['-include', meson.current_source_dir() / 'ns-winxp.h']
  else
    deps += cc.find_library('bcrypt')
  endif
endif

# Built outside link-time optimisation: under it GCC reports its own false
# positives in Mbed TLS (-Wstringop-overflow in bignum_core.c) at the final
# link, where -w no longer reaches them.
lib_args = ['-w'] + config_args + xp_args + cc.get_supported_arguments('-fno-lto')
lib_inc = include_directories('include', 'library')
public_inc = include_directories('include', is_system: true)

mbedcrypto_lib = static_library('mbedcrypto', srcs['crypto'],
  c_args: lib_args,
  include_directories: lib_inc,
  dependencies: deps,
  pic: true,
  install: false)

mbedcrypto_dep = declare_dependency(link_with: mbedcrypto_lib,
  include_directories: public_inc,
  compile_args: config_args,
  dependencies: deps,
  version: meson.project_version())
meson.override_dependency('mbedcrypto', mbedcrypto_dep)

mbedx509_lib = static_library('mbedx509', srcs['x509'],
  c_args: lib_args,
  include_directories: lib_inc,
  link_with: mbedcrypto_lib,
  pic: true,
  build_by_default: false,
  install: false)

tls_deps = deps
if host_machine.system() == 'windows'
  tls_deps += cc.find_library('ws2_32')
endif
mbedtls_lib = static_library('mbedtls', srcs['tls'],
  c_args: lib_args,
  include_directories: lib_inc,
  link_with: [mbedx509_lib, mbedcrypto_lib],
  dependencies: tls_deps,
  pic: true,
  build_by_default: false,
  install: false)

mbedtls_dep = declare_dependency(
  link_with: [mbedtls_lib, mbedx509_lib, mbedcrypto_lib],
  include_directories: public_inc,
  compile_args: config_args,
  dependencies: tls_deps,
  version: meson.project_version())
meson.override_dependency('mbedtls', mbedtls_dep)

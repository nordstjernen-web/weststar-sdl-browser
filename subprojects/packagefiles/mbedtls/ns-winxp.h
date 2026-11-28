/* Included before every file of Mbed TLS built for Windows XP (see
 * meson.build). Where the C library defines _TRUNCATE, platform.c formats
 * with vsnprintf_s, which XP's msvcrt.dll lacks; without it, with
 * vsnprintf and its own termination of the string. */
#include <stdlib.h>
#undef _TRUNCATE

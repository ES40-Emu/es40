#                                               -*- Autoconf -*-
################################################################################
# ES40 Emulator.
# Copyright (C) 2007-2008 by the ES40 Emulator Project
#
# WWW    : https://github.com/ES40-Emu/es40
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 2
# of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
#
# Although this is not required, the author would appreciate being notified of,
# and receiving any modifications you may make to the source code that might serve
# the general public.
#
################################################################################
#
# $Id$
#
# X-1.3      Camiel Vanderhoeven                      31-MAY-2008
#      Add parts of Poco.
#
# X-1.2      Camiel Vanderhoeven                      04-APR-2008
#      Version number set to "0.18+"
#
# X-1.1	     Camiel Vanderhoeven                      20-MAR-2008
#      File Created.
#
################################################################################
#
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.71])
AC_INIT([es40],[0.91],[https://github.com/ES40-Emu/es40/issues/new],[],[https://github.com/ES40-Emu/es40/issues])
AC_CONFIG_SRCDIR([src/system/Flash.cpp])
AC_CONFIG_MACRO_DIRS([m4])
AC_CONFIG_HEADERS([src/config.h])
AM_INIT_AUTOMAKE([foreign subdir-objects])
AC_CANONICAL_HOST

AC_ARG_VAR([ES40_GIT_COMMIT], [Git commit to embed in ES40 binaries])
AS_IF([test -z "$ES40_GIT_COMMIT"], [
  AS_IF([test -n "$GITHUB_SHA"], [
    ES40_GIT_COMMIT="$GITHUB_SHA"
  ], [
    ES40_GIT_COMMIT=`git -C "$srcdir" rev-parse HEAD 2>/dev/null`
  ])
])
AS_IF([test -z "$ES40_GIT_COMMIT"], [ES40_GIT_COMMIT=unknown])
AC_DEFINE_UNQUOTED([ES40_GIT_COMMIT], ["$ES40_GIT_COMMIT"],
  [Git commit from which this binary was built.])

# Checks for programs.
AC_PROG_CXX
AC_PROG_CC
AC_PROG_RANLIB

# Allow enabling JIT for builds.
AC_ARG_ENABLE([asmjit],
  [AS_HELP_STRING([--enable-asmjit],[Enable AsmJit support])],
  [enable_asmjit=yes],
  [enable_asmjit=no])

# We need C++17; AsmJit (v1.23+) needs C++20, matching its cxx_std_20 requirement in CMake.
AS_IF([test "x$enable_asmjit" = "xyes"],
  [AX_CXX_COMPILE_STDCXX([20], [ext], [mandatory])],
  [AX_CXX_COMPILE_STDCXX([17], [ext], [mandatory])])

case "$host_os" in
  darwin*)
    # Xcode 16+ marks sprintf as deprecated and the legacy codebase still uses
    # it in debug/error macros. Keep source unchanged and make macOS builds
    # tolerate these deprecation warnings while we modernize incrementally.
    CXXFLAGS="$CXXFLAGS -Wno-deprecated-declarations"

    # Work around duplicate "thread-local wrapper routine" symbols produced
    # by Apple clang/ld for inline thread_local header variables. Making
    # symbol visibility hidden keeps these internal wrappers out of the global
    # link namespace without touching runtime/source files.
    CXXFLAGS="$CXXFLAGS -fvisibility=hidden -fvisibility-inlines-hidden"

    # Keep CI output focused on actionable issues on macOS where clang can
    # emit noisy override-style warnings for legacy code.
    CXXFLAGS="$CXXFLAGS -Wno-inconsistent-missing-override"
    ;;
esac

# CPU baseline (x86 only): AVX2 (default, Haswell+) for performance, or SSE2 for older-host
# compatibility. Mirrors the Visual Studio "- SSE2" configs and the CMake ES40_ARCH option.
# The JIT emits its own x86 and is unaffected; this governs the host C++.
AC_ARG_WITH([arch],
  [AS_HELP_STRING([--with-arch=avx2|sse2],[x86 CPU baseline: avx2 (default, Haswell+) or sse2 (older-host compat)])],
  [],
  [with_arch=avx2])

case "$host_cpu" in
  x86_64|amd64|i?86)
    case "$with_arch" in
      avx2) CXXFLAGS="$CXXFLAGS -mavx2 -mfma"; CFLAGS="$CFLAGS -mavx2 -mfma" ;;
      sse2) CXXFLAGS="$CXXFLAGS -msse2"; CFLAGS="$CFLAGS -msse2" ;;
      *)    AC_MSG_ERROR([--with-arch must be avx2 or sse2]) ;;
    esac
    ;;
esac

# Checks for header files.
m4_warn([obsolete],
[The preprocessor macro 'STDC_HEADERS' is obsolete.
  Except in unusual embedded environments, you can safely include all
  C89 headers unconditionally.])dnl
# Autoupdate added the next two lines to ensure that your configure
# script's behavior did not change.  They are probably safe to remove.
AC_CHECK_INCLUDES_DEFAULT
AC_PROG_EGREP

AC_HEADER_SYS_WAIT
AC_CHECK_HEADERS([arpa/inet.h arpa/telnet.h ctype.h errno.h fcntl.h in.h inet.h inttypes.h malloc.h netinet/in.h process.h pthread.h signal.h socket.h stdint.h stdlib.h string.h sys/param.h sys/socket.h sys/time.h unistd.h windows.h winsock2.h ws2tcpip.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_HEADER_STDBOOL
AC_C_CONST
AC_C_INLINE
AC_TYPE_INT16_T
AC_TYPE_INT32_T
AC_TYPE_INT64_T
AC_TYPE_INT8_T
AC_TYPE_PID_T
AC_TYPE_SIZE_T
AC_TYPE_SSIZE_T
m4_warn([obsolete],
[Update your code to rely only on HAVE_SYS_TIME_H,
then remove this warning and the obsolete code below it.
All current systems provide time.h; it need not be checked for.
Not all systems provide sys/time.h, but those that do, all allow
you to include it and time.h simultaneously.])dnl
AC_CHECK_HEADERS_ONCE([sys/time.h])
# Obsolete code to be removed.
if test $ac_cv_header_sys_time_h = yes; then
  AC_DEFINE([TIME_WITH_SYS_TIME],[1],[Define to 1 if you can safely include both <sys/time.h>
	     and <time.h>.  This macro is obsolete.])
fi
# End of obsolete code.

AC_STRUCT_TM
AC_TYPE_UINT16_T
AC_TYPE_UINT32_T
AC_TYPE_UINT64_T
AC_TYPE_UINT8_T

# Checks for library functions.
AC_FUNC_ERROR_AT_LINE
AC_FUNC_FORK
AC_FUNC_MALLOC
AC_FUNC_REALLOC
AC_FUNC_SELECT_ARGTYPES
m4_warn([obsolete],
[your code may safely assume C89 semantics that RETSIGTYPE is void.
Remove this warning and the 'AC_CACHE_CHECK' when you adjust the code.])dnl
AC_CACHE_CHECK([return type of signal handlers],[ac_cv_type_signal],[AC_COMPILE_IFELSE(
[AC_LANG_PROGRAM([#include <sys/types.h>
#include <signal.h>
],
		 [return *(signal (0, 0)) (0) == 1;])],
		   [ac_cv_type_signal=int],
		   [ac_cv_type_signal=void])])
AC_DEFINE_UNQUOTED([RETSIGTYPE],[$ac_cv_type_signal],[Define as the return type of signal handlers
		    ('int' or 'void').])

AC_CHECK_FUNCS([alarm atexit fopen fopen64 fseek fseeko fseeko64 _fseeki64 ftell ftello ftello64 _ftelli64 gmtime_s inet_aton isblank memset pow select socket sqrt strcasecmp _stricmp strchr strdup _strdup strncasecmp _stricasecmp strspn])

# Check for pthreads
AX_PTHREAD
CXXFLAGS="$CXXFLAGS $PTHREAD_CFLAGS"
LDFLAGS="$LDFLAGS $PTHREAD_LDFLAGS"
LIBS="$LIBS $PTHREAD_LIBS"

# Allow disabling PCAP for no-network builds.
AC_ARG_ENABLE([pcap],
  [AS_HELP_STRING([--disable-pcap],[Disable pcap support])],
  [],
  [enable_pcap=yes])

# Allow disabling TAP support (Linux/FreeBSD/NetBSD only) independently of pcap.
AC_ARG_ENABLE([tap],
  [AS_HELP_STRING([--disable-tap],[Disable TAP device networking (Linux/FreeBSD/NetBSD only)])],
  [],
  [enable_tap=yes])

# Allow disabling vmnet support (macOS only) independently of pcap.
AC_ARG_ENABLE([vmnet],
  [AS_HELP_STRING([--disable-vmnet],[Disable vmnet networking (macOS only)])],
  [],
  [enable_vmnet=yes])

have_pcap=no
AS_IF([test "x$enable_pcap" != "xno"], [
  case "$host_os" in
    darwin*)
      # On macOS, use our own AM_PATH_PCAP (m4/libpcap.m4)
      AM_PATH_PCAP(
        [
          AC_DEFINE([HAVE_PCAP],[1],[Use PCAP])
          CXXFLAGS="$CXXFLAGS $PCAP_CFLAGS -DHAVE_PCAP"
          LIBS="$LIBS $PCAP_LIBS"
          have_pcap=yes
        ],:)
      ;;
    *)
      PKG_CHECK_MODULES([PCAP],[libpcap >= 0.8],
        [
          AC_DEFINE([HAVE_PCAP],[1],[Use PCAP])
          CXXFLAGS="$CXXFLAGS $PCAP_CFLAGS -DHAVE_PCAP"
          LIBS="$LIBS $PCAP_LIBS"
          have_pcap=yes
        ],
        [:])
      ;;
  esac
])

# TAP device support: on Linux, FreeBSD and NetBSD, a TAP-based backend is
# available even without libpcap. Opening an existing device is always
# possible with just a file descriptor and (on Linux only) an ioctl; no
# extra library is required, so this only needs a host_os check.
have_tap=no
AS_IF([test "x$enable_tap" != "xno"], [
  case "$host_os" in
    linux*|freebsd*|kfreebsd*-gnu|netbsd*)
      AC_DEFINE([HAVE_TAP_NET],[1],[Use TAP device networking])
      CXXFLAGS="$CXXFLAGS -DHAVE_TAP_NET"
      have_tap=yes
      ;;
    *)
      ;;
  esac
])

# vmnet support: on macOS, the vmnet framework backend is
# available even without libpcap.
have_vmnet=no
AS_IF([test "x$enable_vmnet" != "xno"], [
  case "$host_os" in
    darwin*)
      AC_DEFINE([HAVE_VMNET],[1],[Use vmnet networking])
      CXXFLAGS="$CXXFLAGS -DHAVE_VMNET"
      LIBS="$LIBS -framework vmnet"
      have_vmnet=yes
      ;;
    *)
      ;;
  esac
])

AM_CONDITIONAL([HAVE_VMNET], [test "x$have_vmnet" = "xyes"])

AS_IF([test "x$enable_pcap" != "xno" -a "x$have_pcap" = "xno" \
       -a "x$have_tap" = "xno" -a "x$have_vmnet" = "xno"], [
  AC_MSG_FAILURE([libpcap >= 0.8 is required on this platform unless --disable-pcap is used (or, on Linux/FreeBSD/NetBSD, TAP support can be used instead: don't pass --disable-tap)])
])

# Use our own method to search for X11
AM_PATH_X11([], [:])

if test "x$X11_CFLAGS" != xnope && test "x$X11_LIBS" != xnope; then
  CPPFLAGS="$CPPFLAGS $X11_CFLAGS"
  CXXFLAGS="$CXXFLAGS -DHAVE_X11"
  LIBS="$LIBS $X11_LIBS"
fi

# Allow disabling SDL for no-screen builds.
AC_ARG_ENABLE([sdl],
  [AS_HELP_STRING([--disable-sdl],[Disable SDL support])],
  [],
  [enable_sdl=yes])

AS_IF([test "x$enable_sdl" != "xno"], [
  PKG_CHECK_MODULES([SDL],[sdl3 >= 3.2.0],
    [
      AC_DEFINE([HAVE_SDL],[1],[Use SDL])
      CXXFLAGS="$CXXFLAGS $SDL_CFLAGS -DHAVE_SDL"
      LIBS="$LIBS $SDL_LIBS"
    ],
    [
      AC_MSG_FAILURE([sdl3 >= 3.2.0 is required unless --disable-sdl is used])
    ])
])

AM_CONDITIONAL([ENABLE_ASMJIT], [test "x$enable_asmjit" = "xyes"])

# JIT host architecture: x86-64 gets the full codegen backend; ARM64 builds the JIT
# scaffolding (the engine compiles but emits no code yet, so every block falls back to
# the interpreter). Any other host can't build the engine at all.
AS_IF([test "x$enable_asmjit" = "xyes"], [
  case "$host_cpu" in
    x86_64|amd64|aarch64|arm64) ;;
    *) AC_MSG_ERROR([--enable-asmjit requires an x86-64 or ARM64 host (host_cpu=$host_cpu)]) ;;
  esac
])
AM_CONDITIONAL([ES40_JIT_HOST_A64],
  [test "x$host_cpu" = "xaarch64" || test "x$host_cpu" = "xarm64"])

# The debugger + lockstep programs rebuild the whole source tree per program;
# --disable-tools skips them. es40 and es40_cfg (small) always build.
AC_ARG_ENABLE([tools],
  [AS_HELP_STRING([--disable-tools],
    [skip the auxiliary programs (es40_idb, es40_lss, es40_lsm)])],
  [],
  [enable_tools=yes])
AM_CONDITIONAL([ENABLE_TOOLS], [test "x$enable_tools" != "xno"])

# Keep the setuid vmnet helper isolated from the emulator's optional
# SDL, X11, pcap, and threading libraries.
ES40_LIBS="$LIBS"
AC_SUBST([ES40_LIBS])
LIBS=

# es40_cfg_tui, the ncurses configuration editor: needs ncurses plus its form,
# menu and panel libraries. Checked after LIBS is cleared so es40 never links them.
AC_ARG_ENABLE([tui],
  [AS_HELP_STRING([--enable-tui@<:@=yes|no|auto@:>@],
    [build es40_cfg_tui, the ncurses configuration editor (default: auto)])],
  [],
  [enable_tui=auto])

have_tui=no
AS_IF([test "x$enable_tui" != "xno"], [
  PKG_CHECK_MODULES([TUI], [ncurses form menu panel], [have_tui=yes], [
    # macOS ships ncurses without .pc files; fall back to header and link checks.
    have_tui=yes
    TUI_CFLAGS=
    TUI_LIBS=
    AC_CHECK_HEADERS([ncurses.h form.h menu.h panel.h], [], [have_tui=no])
    AS_IF([test "x$have_tui" = "xyes"], [
      AC_CHECK_LIB([ncurses], [initscr], [TUI_LIBS="-lncurses"], [have_tui=no])
    ])
    AS_IF([test "x$have_tui" = "xyes"], [
      AC_CHECK_LIB([panel], [new_panel], [TUI_LIBS="-lpanel $TUI_LIBS"], [have_tui=no], [$TUI_LIBS])
      AC_CHECK_LIB([menu], [new_menu], [TUI_LIBS="-lmenu $TUI_LIBS"], [have_tui=no], [$TUI_LIBS])
      AC_CHECK_LIB([form], [new_form], [TUI_LIBS="-lform $TUI_LIBS"], [have_tui=no], [$TUI_LIBS])
    ])
  ])
  AS_IF([test "x$enable_tui" = "xyes" -a "x$have_tui" = "xno"], [
    AC_MSG_FAILURE([--enable-tui requires ncurses with the form, menu and panel libraries])
  ])
])
AC_SUBST([TUI_CFLAGS])
AC_SUBST([TUI_LIBS])
AM_CONDITIONAL([BUILD_TUI], [test "x$have_tui" = "xyes"])

# Generate the various makefiles.
AC_CONFIG_FILES([Makefile doc/Makefile m4/Makefile src/Makefile])
AC_OUTPUT

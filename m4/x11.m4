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
# X-1.1	     Camiel Vanderhoeven                      20-MAR-2008
#      File Created.
#
################################################################################
#
# Check if X11 is installed, and configure paths

dnl AM_PATH_X11([ACTION-IF-FOUND [, ACTION-IF-NOT-FOUND]])
dnl test for X11, and define X11_CFLAGS and X11_LIBS
dnl

AC_DEFUN([AM_PATH_X11], [
  AC_PATH_XTRA
  X11_CFLAGS=""
  X11_LIBS=""
  have_x11=no

  AS_IF([test "x$no_x" != "xyes"], [
    X11_CFLAGS="$X_CFLAGS"
    X11_LIBS="$X_LIBS $X_PRE_LIBS -lX11 $X_EXTRA_LIBS"
    es40_x11_save_CPPFLAGS="$CPPFLAGS"
    es40_x11_save_LIBS="$LIBS"
    CPPFLAGS="$CPPFLAGS $X11_CFLAGS"
    LIBS="$X11_LIBS $LIBS"

    AC_MSG_CHECKING([whether X11 headers and libraries are usable])
    AC_LINK_IFELSE([
      AC_LANG_PROGRAM([[#include <X11/Xlib.h>]], [[
        Display *display = XOpenDisplay(0);
        if (display) XCloseDisplay(display);
      ]])
    ], [have_x11=yes], [have_x11=no])
    AC_MSG_RESULT([$have_x11])

    AS_IF([test "x$have_x11" = "xyes"], [
      LIBS="$X_LIBS -lXss -lXext $X_PRE_LIBS -lX11 $X_EXTRA_LIBS $es40_x11_save_LIBS"
      AC_MSG_CHECKING([whether X11 screen saver suspension is usable])
      AC_LANG_PUSH([C++])
      AC_LINK_IFELSE([
        AC_LANG_PROGRAM([[
          #include <X11/Xlib.h>
          #include <X11/extensions/scrnsaver.h>
        ]], [[
          Display *display = 0;
          int event, error, major, minor;
          XScreenSaverQueryExtension(display, &event, &error);
          XScreenSaverQueryVersion(display, &major, &minor);
          XScreenSaverSuspend(display, True);
        ]])
      ], [have_x11_xss=yes], [have_x11_xss=no])
      AC_LANG_POP([C++])
      AC_MSG_RESULT([$have_x11_xss])
      AS_IF([test "x$have_x11_xss" = "xyes"], [
        AC_DEFINE([HAVE_X11_XSS], [1], [Use X11 screen saver suspension])
        X11_LIBS="$X_LIBS -lXss -lXext $X_PRE_LIBS -lX11 $X_EXTRA_LIBS"
      ])
    ])

    CPPFLAGS="$es40_x11_save_CPPFLAGS"
    LIBS="$es40_x11_save_LIBS"
  ])

  AC_MSG_CHECKING([whether to use X11])
  AC_MSG_RESULT([$have_x11])
  AS_IF([test "x$have_x11" = "xyes"], [
    m4_default([$1], [:])
  ], [
    X11_CFLAGS=""
    X11_LIBS=""
    m4_default([$2], [:])
  ])
  AC_SUBST([X11_CFLAGS])
  AC_SUBST([X11_LIBS])
])

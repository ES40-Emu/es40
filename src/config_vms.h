/* ES40 Emulator.
 * Copyright (C) 2007-2008 by the ES40 Emulator Project
 *
 * WWW    : https://github.com/ES40-Emu/es40
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 * Although this is not required, the author would appreciate being notified of,
 * and receiving any modifications you may make to the source code that might serve
 * the general public.
 *
 */

 /**
  * \file
  * Contains configuration definitions (similar to produced by the configure
  * script on other OS'es) to build ES40 emulator on OpenVMS.
  *
  * $Id$
  *
  * X-1.2        Camiel Vanderhoeven                             04-APR-2008
  *      Version number set to "0.18+"
  *
  * X-1.1        Camiel Vanderhoeven                             31-MAR-2008
  *      File creation.
  **/

  /* Version number of package */
#define VERSION "0.94"

/* Rust is unavailable on OpenVMS; command-line defines cannot enable SLIRP. */
#undef HAVE_SLIRP

/* Define to 1 if you have the `alarm' function. */
#define HAVE_ALARM 1

/* Define to 1 if you have the <arpa/inet.h> header file. */
//#define HAVE_ARPA_INET_H 1

//#define HAVE_ARPA_TELNET_H 1

/* Define to 1 if you have the `atexit' function. */
#define HAVE_ATEXIT 1

#define HAVE_CTYPE_H 1

#define HAVE_ERRNO_H 1

/* Define to 1 if you have the <fcntl.h> header file. */
#define HAVE_FCNTL_H 1

#define HAVE_FOPEN 1

// #define HAVE_FOPEN64 1

#define HAVE_FTELL 1

#define HAVE_FTELLO 1

// #define HAVE_FTELLO64 1

// #define HAVE__FTELLI64 1

#define HAVE_FSEEK 1

#define HAVE_FSEEKO 1

// #define HAVE_FSEEKO64 1

// #define HAVE__FSEEKI64 1

/* Define to 1 if you have the `fork' function. */
#define HAVE_FORK 1

/* Defint to 1 if you have the 'gmtime_s' function. */
//#define HAVE_GMTIME_S 1

/* Define to 1 if you have the <in.h> header file. */
#define HAVE_IN_H 1

/* Define to 1 if you have the <inet.h> header file. */
#define HAVE_INET_H 1

#define HAVE_INET_ATON 1

/* Define to 1 if you have the <inttypes.h> header file. */
//#define HAVE_INTTYPES_H 1

/* Define to 1 if you have the 'isblank' function. */
#define HAVE_ISBLANK 1

/* Define to 1 if your system has a GNU libc compatible `malloc' function, and
   to 0 otherwise. */
#define HAVE_MALLOC 1

   /* Define to 1 if you have the <malloc.h> header file. */
   //#define HAVE_MALLOC_H 1

   /* Define to 1 if you have the <memory.h> header file. */
#define HAVE_MEMORY_H 1

/* Define to 1 if you have the `memset' function. */
#define HAVE_MEMSET 1

/* Define to 1 if you have the <netinet/in.h> header file. */
//#define HAVE_NETINET_IN_H 1

/* Define to 1 if you have the `pow' function. */
#define HAVE_POW 1

/* Define to 1 if you have the <process.h> header file. */
//#define HAVE_PROCESS_H 1

/* Define to 1 if you have the <pthread.h> header file. */
//#define HAVE_PTHREAD_H 1

/* Define to 1 if your system has a GNU libc compatible `realloc' function,
   and to 0 otherwise. */
#define HAVE_REALLOC 1

   /* Define to 1 if you have the `select' function. */
#define HAVE_SELECT 1

/* Define to 1 if you have the <signal.h> header file. */
#define HAVE_SIGNAL_H 1

/* Define to 1 if you have the `socket' function. */
#define HAVE_SOCKET 1

/* Define to 1 if you have the <socket.h> header file. */
#define HAVE_SOCKET_H 1

/* Define to 1 if you have the `sqrt' function. */
#define HAVE_SQRT 1

/* Define to 1 if stdbool.h conforms to C99. */
#define HAVE_STDBOOL_H 1

/* Define to 1 if you have the <stdint.h> header file. */
//#define HAVE_STDINT_H 1

/* Define to 1 if you have the <stdlib.h> header file. */
#define HAVE_STDLIB_H 1

/* Define to 1 if you have the `strcasecmp' function. */
#define HAVE_STRCASECMP 1

/* Define to 1 if you have the '_stricmp' function. */
//#define HAVE__STRICMP 1

/* Define to 1 if you have the `strchr' function. */
#define HAVE_STRCHR 1

/* Define to 1 if you have the `strdup' function. */
#define HAVE_STRDUP 1

/* Definr to 1 if you have the '_strdup' function. */
#define HAVE__STRDUP 1

/* Define to 1 if you have the <strings.h> header file. */
#define HAVE_STRINGS_H 1

/* Define to 1 if you have the <string.h> header file. */
#define HAVE_STRING_H 1

/* Define to 1 if you have the `strncasecmp' function. */
#define HAVE_STRNCASECMP 1

/* Define to 1 if you have the '_strnicmp' function. */
//#define HAVE__STRNICMP 1

/* Define to 1 if you have the `strspn' function. */
#define HAVE_STRSPN 1

/* Define to 1 if you have the <sys/param.h> header file. */
#define HAVE_SYS_PARAM_H 1

/* Define to 1 if you have the <sys/select.h> header file. */
//#define HAVE_SYS_SELECT_H 1

/* Define to 1 if you have the <sys/socket.h> header file. */
//#define HAVE_SYS_SOCKET_H 1

/* Define to 1 if you have the <sys/stat.h> header file. */
#define HAVE_SYS_STAT_H 1

/* Define to 1 if you have the <sys/time.h> header file. */
#define HAVE_SYS_TIME_H 1

/* Define to 1 if you have the <sys/types.h> header file. */
#define HAVE_SYS_TYPES_H 1

/* Define to 1 if you have <sys/wait.h> that is POSIX.1 compatible. */
//#define HAVE_SYS_WAIT_H 1

/* Define to 1 if you have the <unistd.h> header file. */
//#define HAVE_UNISTD_H 1

/* Define to 1 if you have the `vfork' function. */
#define HAVE_VFORK 1

/* Define to 1 if you have the <vfork.h> header file. */
/* #undef HAVE_VFORK_H */

/* Define to 1 if you have the <windows.h> header file. */
//#define HAVE_WINDOWS_H 1

//#define HAVE_WINSOCK2_H 1

//#define HAVE_WS2TCPIP_H 1

/* Define to 1 if `fork' works. */
#define HAVE_WORKING_FORK 1

/* Define to 1 if `vfork' works. */
#define HAVE_WORKING_VFORK 1

/* Define to 1 if the system has the type `_Bool'. */
#define HAVE__BOOL 1

/* Name of package */
#define PACKAGE "es40"

/* Define to the address where bug reports for this package should be sent. */
#define PACKAGE_BUGREPORT "iamcamiel@gmail.com"

/* Define to the full name of this package. */
#define PACKAGE_NAME "es40"

/* Define to the full name and version of this package. */
#define PACKAGE_STRING "es40 " VERSION

/* Define to the one symbol short name of this package. */
#define PACKAGE_TARNAME "es40"

/* Define to the version of this package. */
#define PACKAGE_VERSION VERSION

/* Define as the return type of signal handlers (`int' or `void'). */
#define RETSIGTYPE void

/* Define to the type of arg 1 for `select'. */
#define SELECT_TYPE_ARG1 int

/* Define to the type of args 2, 3 and 4 for `select'. */
#define SELECT_TYPE_ARG234 (fd_set *)

/* Define to the type of arg 5 for `select'. */
#define SELECT_TYPE_ARG5 (struct timeval *)

/* Define to 1 if you have the ANSI C header files. */
#define STDC_HEADERS 1

/* Define to 1 if you can safely include both <sys/time.h> and <time.h>. */
#define TIME_WITH_SYS_TIME 1

/* Define to 1 if your <sys/time.h> declares `struct tm'. */
/* #undef TM_IN_SYS_TIME */

/* Define for Solaris 2.5.1 so the uint32_t typedef from <sys/synch.h>,
   <pthread.h>, or <semaphore.h> is not used. If the typedef was allowed, the
   #define below would cause a syntax error. */
   /* #undef _UINT32_T */

   /* Define for Solaris 2.5.1 so the uint64_t typedef from <sys/synch.h>,
      <pthread.h>, or <semaphore.h> is not used. If the typedef was allowed, the
      #define below would cause a syntax error. */
      /* #undef _UINT64_T */

      /* Define for Solaris 2.5.1 so the uint8_t typedef from <sys/synch.h>,
         <pthread.h>, or <semaphore.h> is not used. If the typedef was allowed, the
         #define below would cause a syntax error. */
         /* #undef _UINT8_T */

         /* Define to empty if `const' does not conform to ANSI C. */
         /* #undef const */

         /* Define to `__inline__' or `__inline' if that's what the C compiler
            calls it, or to nothing if 'inline' is not supported under any name.  */
#ifndef __cplusplus
            /* #undef inline */
#endif

/* Define to the type of a signed integer type of width exactly 16 bits if
   such a type exists and the standard includes do not define it. */
typedef signed __int16 int16_t;

/* Define to the type of a signed integer type of width exactly 32 bits if
   such a type exists and the standard includes do not define it. */
typedef signed __int32 int32_t;

/* Define to the type of a signed integer type of width exactly 64 bits if
   such a type exists and the standard includes do not define it. */
typedef signed __int64 int64_t;

/* Define to the type of a signed integer type of width exactly 8 bits if such
   a type exists and the standard includes do not define it. */
typedef signed __int8 int8_t;

/* Define to rpl_malloc if the replacement function should be used. */
/* #undef malloc */

/* Define to `int' if <sys/types.h> does not define. */
/* #undef pid_t */

/* Define to rpl_realloc if the replacement function should be used. */
/* #undef realloc */

/* Define to `unsigned int' if <sys/types.h> does not define. */
/* #undef size_t */

/* Define to `int' if <sys/types.h> does not define. */
/* #undef ssize_t */

/* Define to the type of an unsigned integer type of width exactly 16 bits if
   such a type exists and the standard includes do not define it. */
typedef unsigned __int16 uint16_t;

/* Define to the type of an unsigned integer type of width exactly 32 bits if
   such a type exists and the standard includes do not define it. */
typedef unsigned __int32 uint32_t;

/* Define to the type of an unsigned integer type of width exactly 64 bits if
   such a type exists and the standard includes do not define it. */
typedef unsigned __int64 uint64_t;

/* Define to the type of an unsigned integer type of width exactly 8 bits if
   such a type exists and the standard includes do not define it. */
typedef unsigned __int8 uint8_t;

/* Define as `fork' if `vfork' does not work. */
/* #undef vfork */


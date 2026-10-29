cmake_minimum_required(VERSION 3.24)

project(ES40 VERSION 0.93.2 LANGUAGES C CXX)

# Single-config generators default to an empty build type. Fix that up. 
get_property(ES40_MULTI_CONFIG GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if (NOT ES40_MULTI_CONFIG AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type (Debug, Release, RelWithDebInfo, MinSizeRel)" FORCE)
endif()

option(ES40_DISABLE_SDL "Disable SDL support" OFF)
option(ES40_DISABLE_X11 "Disable the native X11 GUI" OFF)
option(ES40_DISABLE_NETWORKING "Disable PCap/networking for networkless builds" OFF)
option(ES40_DISABLE_TAP "Disable TAP networking even on platforms that support it (Linux/FreeBSD/NetBSD)" OFF)
option(ES40_DISABLE_VMNET "Disable vmnet networking on macOS" OFF)
option(ES40_DISABLE_SLIRP "Disable the Rust userspace (slirp) network backend" OFF)
option(ES40_DISABLE_LSS_LSM "Disable LSS/LSM (lockstep) builds." OFF)
option(ES40_DISABLE_IDB "Disable IDB (built-in debugger) builds." OFF)
option(ES40_DISABLE_ASMJIT "Disable ASMJIT" ON)
option(ES40_ENABLE_CFG_TUI "Enable build of es40_cfg_tui" ON)
option(ES40_ENABLE_CFG_SDL "Enable build of es40_cfg_sdl (requires SDL)" ON)
# The GTK 3 editor is the native desktop frontend on Linux/BSD; elsewhere it is opt-in.
if (UNIX AND NOT APPLE)
    set(ES40_CFG_GTK_DEFAULT ON)
else()
    set(ES40_CFG_GTK_DEFAULT OFF)
endif()
option(ES40_ENABLE_CFG_GTK "Enable build of es40_cfg_gtk (requires GTK 3)" ${ES40_CFG_GTK_DEFAULT})

# CPU baseline: AVX2 (default, Haswell+) for performance, or SSE2 for older-host compatibility.
# Applied on x86 only (no-op on ARM / Apple Silicon). The JIT emits its own x86 and is unaffected;
# this governs the host C++ (interpreter, devices, GUI). Mirrors the VS "- SSE2" configs.
set(ES40_ARCH "AVX2" CACHE STRING "x86 CPU baseline: AVX2 (default) or SSE2 (older-host compat)")
set_property(CACHE ES40_ARCH PROPERTY STRINGS AVX2 SSE2)
if (CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|amd64|i[3-6]86|x86")
    if (MSVC)
        if (ES40_ARCH STREQUAL "AVX2")
            add_compile_options(/arch:AVX2)
        endif()   # SSE2 is the MSVC x64 default
    else()
        if (ES40_ARCH STREQUAL "AVX2")
            add_compile_options(-mavx2 -mfma)
        else()
            add_compile_options(-msse2)
        endif()
    endif()
endif()

# Only used for PCap/NPCap at the moment.
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/extras/cmake")

if (NOT ES40_DISABLE_SDL)
    if (NOT SDL3_DIR)
        set(SDL3_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../SDL3/cmake")
    endif()
    find_package(SDL3 3.2.0 REQUIRED)
endif()

# X11 is an optional native frontend, independent of SDL. Require both its
# headers and library before exposing the backend to any configuration tool.
set(HAVE_X11 FALSE)
if (NOT WIN32 AND NOT ES40_DISABLE_X11)
    find_package(X11 QUIET)
    if (X11_FOUND)
        set(HAVE_X11 TRUE)
    endif()
endif()

set(ES40_HAVE_PCAP FALSE)
set(ES40_HAVE_TAP FALSE)
set(ES40_HAVE_SLIRP FALSE)

# OpenVMS has no Rust compiler. Check the compiler as well as the system name
# so defining __VMS cannot enable a Cargo build on a differently named host.
include(CheckCXXSymbolExists)
check_cxx_symbol_exists(__VMS "" ES40_COMPILER_IS_VMS)
set(ES40_HOST_IS_VMS FALSE)
if (ES40_COMPILER_IS_VMS OR CMAKE_SYSTEM_NAME MATCHES "^(OpenVMS|VMS)$")
    set(ES40_HOST_IS_VMS TRUE)
endif()

# The Rust userspace ("slirp") backend needs no host libraries, only cargo.
if (NOT ES40_HOST_IS_VMS AND NOT ES40_DISABLE_NETWORKING AND NOT ES40_DISABLE_SLIRP)
    find_program(CARGO_EXECUTABLE cargo)
    if (CARGO_EXECUTABLE)
        set(ES40_HAVE_SLIRP TRUE)
    else()
        message(WARNING "cargo not found: the Rust \"slirp\" network backend will not be built. Configure with -DES40_DISABLE_SLIRP=ON to silence this warning.")
    endif()
endif()

if (NOT ES40_DISABLE_NETWORKING)
    if (WIN32)
        #if (NOT NPCAP_DIR)
        #    set(NPCAP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../NPCAP")
        #endif()
        #find_package(NPCap REQUIRED)
        set(ES40_HAVE_PCAP TRUE)
    else()
        # pcap is optional on Unix-like hosts as long as TAP support covers
        # the platform; it's still tried first since it also works for
        # host OSes/setups TAP doesn't (e.g. wireless adapters, or hosts
        # where the user doesn't want to grant CAP_NET_ADMIN).
        find_package(PCap)
        if (PCap_FOUND)
            set(ES40_HAVE_PCAP TRUE)
        endif()

        if (NOT ES40_DISABLE_TAP AND (CMAKE_SYSTEM_NAME STREQUAL "Linux" OR
                                       CMAKE_SYSTEM_NAME STREQUAL "FreeBSD" OR
                                       CMAKE_SYSTEM_NAME STREQUAL "NetBSD"))
            set(ES40_HAVE_TAP TRUE)
        endif()

        if (NOT ES40_DISABLE_VMNET AND CMAKE_SYSTEM_NAME STREQUAL "Darwin")
            set(ES40_HAVE_VMNET TRUE)
        endif()

        if (NOT ES40_HAVE_PCAP AND NOT ES40_HAVE_TAP AND NOT ES40_HAVE_VMNET AND NOT ES40_HAVE_SLIRP)
            message(FATAL_ERROR "Networking was requested (ES40_DISABLE_NETWORKING=OFF) but neither libpcap nor TAP nor vmnet nor slirp support is available for this platform/build. Install libpcap, or configure with -DES40_DISABLE_NETWORKING=ON to build without networking.")
        endif()
    endif()
endif()

# --- Rust userspace ("slirp") backend -------------------------------------
#
# The backend itself is the Rust static library in src/network/slirp-rs,
# built out-of-band by cargo. CMake only knows how to invoke cargo and where
# the resulting archive lands; the C++ side sees the extern "C" API declared
# in NetworkSlirp.cpp.
if (ES40_HAVE_SLIRP)
    set(ES40_SLIRP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/network/slirp-rs")
    if (WIN32)
        set(ES40_SLIRP_ARTIFACT "${ES40_SLIRP_DIR}/target/release/es40_slirp.lib")
    else()
        set(ES40_SLIRP_ARTIFACT "${ES40_SLIRP_DIR}/target/release/libes40_slirp.a")
    endif()

    add_custom_target(es40_slirp_rust ALL
        COMMAND ${CARGO_EXECUTABLE} build --release
        WORKING_DIRECTORY "${ES40_SLIRP_DIR}"
        BYPRODUCTS "${ES40_SLIRP_ARTIFACT}"
        COMMENT "Building the Rust slirp network backend"
        VERBATIM)

    add_library(es40_slirp STATIC IMPORTED GLOBAL)
    set_target_properties(es40_slirp PROPERTIES
        IMPORTED_LOCATION "${ES40_SLIRP_ARTIFACT}")

    set(ES40_SLIRP_LINK_LIBRARIES es40_slirp)
    if (WIN32)
        # Native artifacts the Rust static library needs (see
        # `cargo rustc -- --print native-static-libs`).
        list(APPEND ES40_SLIRP_LINK_LIBRARIES ws2_32 dbghelp iphlpapi userenv ntdll)
    else()
        find_package(Threads REQUIRED)
        list(APPEND ES40_SLIRP_LINK_LIBRARIES Threads::Threads ${CMAKE_DL_LIBS})
        find_library(ES40_SLIRP_M_LIB m)
        if (ES40_SLIRP_M_LIB)
            list(APPEND ES40_SLIRP_LINK_LIBRARIES ${ES40_SLIRP_M_LIB})
        endif()
    endif()
endif()

if (NOT WIN32)
    include(CheckSymbolExists)
    include(CheckIncludeFile)
    include(CheckLibraryExists)
    include(CheckTypeSize)
    include(CheckIncludeFiles)

    check_symbol_exists(alarm "unistd.h" HAVE_ALARM)
    check_include_file("arpa/inet.h" HAVE_ARPA_INET_H)
    check_include_file("arpa/telnet.h" HAVE_ARPA_TELNET_H)
    check_symbol_exists(atexit "stdlib.h" HAVE_ATEXIT)
    check_include_file("ctype.h" HAVE_CTYPE_H)
    set(HAVE_CXX17 1)
    check_include_file("errno.h" HAVE_ERRNO_H)
    check_include_file("fcntl.h" HAVE_FCNTL_H)
    check_symbol_exists(fopen "stdio.h" HAVE_FOPEN)
    check_symbol_exists(fopen64 "stdio.h" HAVE_FOPEN64)
    check_symbol_exists(fork "unistd.h" HAVE_FORK)
    check_symbol_exists(fseek "stdio.h" HAVE_FSEEK)
    check_symbol_exists(fseeko "stdio.h" HAVE_FSEEKO)
    check_symbol_exists(fseeko64 "stdio.h" HAVE_FSEEKO64)
    check_symbol_exists(ftell "stdio.h" HAVE_FTELL)
    check_symbol_exists(ftello "stdio.h" HAVE_FTELLO)
    check_symbol_exists(ftello64 "stdio.h" HAVE_FTELLO64)
    check_symbol_exists(gmtime_s "time.h" HAVE_GMTIME_S)
    check_symbol_exists(inet_aton "arpa/inet.h" HAVE_INET_ATON)
    check_include_file("inet.h" HAVE_INET_H)
    check_include_file("inttypes.h" HAVE_INTTYPES_H)
    check_include_file("in.h" HAVE_IN_H)
    check_symbol_exists(isblank "ctype.h" HAVE_ISBLANK)
    check_symbol_exists(localtime_s "time.h" HAVE_LOCALTIME_S)
    check_symbol_exists(malloc "stdlib.h" HAVE_MALLOC)
    check_include_file("malloc.h" HAVE_MALLOC_H)
    check_include_file("memory.h" HAVE_MEMORY_H)
    check_symbol_exists(memset "string.h" HAVE_MEMSET)
    check_include_file("netinet/in.h" HAVE_NETINET_IN_H)
    check_symbol_exists(pow "math.h" HAVE_POW)
    check_include_file("process.h" HAVE_PROCESS_H)
    check_library_exists(pthread pthread_self "" HAVE_PTHREAD)
    check_include_file("pthread.h" HAVE_PTHREAD_H)
    check_symbol_exists(PTHREAD_PRIO_INHERIT "pthread.h" HAVE_PTHREAD_PRIO_INHERIT)
    check_symbol_exists(realloc "stdlib.h" HAVE_REALLOC)
    if (NOT ES40_DISABLE_SDL)
        set(HAVE_SDL 1)
    endif()
    check_symbol_exists(select "sys/select.h" HAVE_SELECT)
    check_include_file("signal.h" HAVE_SIGNAL_H)
    check_symbol_exists(socket "sys/socket.h" HAVE_SOCKET)
    check_include_file("socket.h" HAVE_SOCKET_H)
    check_symbol_exists(sqrt "math.h" HAVE_SQRT)
    check_include_file("stdbool.h" HAVE_STDBOOL_H)
    check_include_file("stdint.h" HAVE_STDINT_H)
    check_include_file("stdio.h" HAVE_STDIO_H)
    check_include_file("stdlib.h" HAVE_STDLIB_H)
    check_symbol_exists(strcasecmp "string.h" HAVE_STRCASECMP)
    check_symbol_exists(strchr "string.h" HAVE_STRCHR)
    check_symbol_exists(strdup "string.h" HAVE_STRDUP)
    check_include_file("stddef.h" HAVE_STDDEF_H)
    check_include_file("strings.h" HAVE_STRINGS_H)
    check_include_file("string.h" HAVE_STRING_H)
    check_symbol_exists(strncasecmp "string.h" HAVE_STRNCASECMP)
    check_symbol_exists(strspn "string.h" HAVE_STRSPN)
    check_include_file("sys/param.h" HAVE_SYS_PARAM_H)
    check_include_file("sys/select.h" HAVE_SYS_SELECT_H)
    check_include_file("sys/socket.h" HAVE_SYS_SOCKET_H)
    check_include_file("sys/stat.h" HAVE_SYS_STAT_H)
    check_include_file("sys/time.h" HAVE_SYS_TIME_H)
    check_include_file("sys/types.h" HAVE_SYS_TYPES_H)
    check_include_file("sys/wait.h" HAVE_SYS_WAIT_H)
    check_include_file("unistd.h" HAVE_UNISTD_H)
    check_symbol_exists(vfork "unistd.h" HAVE_VFORK)
    check_include_file("vfork.h" HAVE_VFORK_H)
    set(HAVE_WORKING_FORK ${HAVE_FORK})
    set(HAVE_WORKING_VFORK ${HAVE_VFORK})
    check_type_size(_Bool _BOOL)
    check_symbol_exists(_fseeki64 "stdio.h" HAVE__FSEEKI64)
    check_symbol_exists(_ftelli64 "stdio.h" HAVE__FTELLI64)
    check_symbol_exists(_strdup "string.h" HAVE__STRDUP)
    check_symbol_exists(_stricasecmp "string.h" HAVE__STRICASECMP)
    check_symbol_exists(_stricmp "string.h" HAVE__STRICMP)

    set(PACKAGE \"${PROJECT_NAME}\")
    set(PACKAGE_BUGREPORT "\"https://github.com/ES40-Emu/es40/issues/new\"")
    string(TOLOWER ${PROJECT_NAME} PACKAGE_NAME_L)
    set(PACKAGE_NAME \"${PACKAGE_NAME_L}\")
    set(PACKAGE_VERSION \"${PROJECT_VERSION}\")
    set(PACKAGE_STRING "\"${PACKAGE_NAME_L} ${PROJECT_VERSION}\"")
    set(PACKAGE_TARNAME \"${PACKAGE_NAME_L}.tar.gz\")
    set(PACKAGE_URL "\"https://github.com/ES40-Emu/es40/issues\"")
    set(VERSION \"${PROJECT_VERSION}\")

    set(RETSIGTYPE "void")
    set(SELECT_TYPE_ARG1 "int")
    set(SELECT_TYPE_ARG234 "(fd_set *)")
    set(SELECT_TYPE_ARG5 "(struct timeval *)")
    set(STDC_HEADERS 1)
    check_include_files("time.h;sys/time.h" TIME_WITH_SYS_TIME)
    set(HAVE_SYS_TIME ${TM_IN_SYS_TIME})

    configure_file(src/config.h.cmake src/config.h)
    include_directories(${CMAKE_CURRENT_BINARY_DIR}/src)
    add_compile_definitions(HAVE_CONFIG_H)
endif()

############################# ES40-CFG #############################

set(ES40_CFG_SOURCES)
set(ES40_CFG_LIBRARIES)
set(ES40_CFG_DEFINITIONS)

list(APPEND ES40_CFG_SOURCES
    src/config/es40-cfg.cpp
    src/base/Exception.cpp
    )

if (WIN32)
    list(APPEND ES40_CFG_LIBRARIES winmm ws2_32)
endif()

if (NOT ES40_DISABLE_NETWORKING)
    if (ES40_HAVE_PCAP)
        list(APPEND ES40_CFG_DEFINITIONS HAVE_PCAP)
        if (WIN32)
            #list(APPEND ES40_CFG_LIBRARIES NPCap::NPCap)
        else()
            list(APPEND ES40_CFG_LIBRARIES PCap::PCap)
        endif()
    endif()
    if (ES40_HAVE_TAP)
        list(APPEND ES40_CFG_DEFINITIONS HAVE_TAP_NET)
    endif()
    if (ES40_HAVE_SLIRP)
        list(APPEND ES40_CFG_DEFINITIONS HAVE_SLIRP)
    endif()
endif()

if (NOT ES40_DISABLE_SDL)
    list(APPEND ES40_CFG_DEFINITIONS HAVE_SDL)
endif()

if (HAVE_X11)
    list(APPEND ES40_CFG_DEFINITIONS HAVE_X11)
endif()

add_executable(es40-cfg 
    ${ES40_CFG_SOURCES}
    )

target_include_directories(es40-cfg PUBLIC 
    src/
    )

target_link_libraries(es40-cfg PRIVATE 
    ${ES40_CFG_LIBRARIES}
    )

target_compile_definitions(es40-cfg PUBLIC ${ES40_CFG_DEFINITIONS})

set_property(TARGET es40-cfg PROPERTY CXX_STANDARD 17)

######################## Configuration editors ########################

# GTK 3 is located through pkg-config. A missing SDK only skips the GTK editor so
# headless and minimal builds keep working without extra configuration.
set(ES40_HAVE_GTK3 OFF)
if (ES40_ENABLE_CFG_GTK)
    find_package(PkgConfig)
    if (PkgConfig_FOUND)
        pkg_check_modules(GTK3 IMPORTED_TARGET gtk+-3.0>=3.22)
    endif()
    if (GTK3_FOUND)
        set(ES40_HAVE_GTK3 ON)
    else()
        message(WARNING "GTK 3 development files not found: es40_cfg_gtk will not be built. Configure with -DES40_ENABLE_CFG_GTK=OFF to silence this warning.")
    endif()
endif()

# The option/workflow library has no curses or SDL dependency. All editors
# inherit exactly the same feature defines and configuration implementation.
if (WIN32 OR ES40_ENABLE_CFG_TUI OR ES40_HAVE_GTK3 OR (ES40_ENABLE_CFG_SDL AND NOT ES40_DISABLE_SDL))
    add_library(es40_config_editor STATIC
        src/config/ConfigEditor.cpp src/config/ConfigDocument.cpp src/config/Configurator.cpp src/base/Exception.cpp)
    target_include_directories(es40_config_editor PUBLIC src/)
    target_compile_features(es40_config_editor PUBLIC cxx_std_17)
    target_compile_definitions(es40_config_editor PUBLIC CONFIGURATION_ONLY ${ES40_CFG_DEFINITIONS})
    target_link_libraries(es40_config_editor PUBLIC ${ES40_CFG_LIBRARIES})
    if (NOT ES40_DISABLE_ASMJIT)
        target_compile_definitions(es40_config_editor PUBLIC ES40_JIT)
    endif()
    if (ES40_HAVE_VMNET)
        target_compile_definitions(es40_config_editor PUBLIC HAVE_VMNET)
    endif()
    if (MSVC)
        target_compile_definitions(es40_config_editor PUBLIC _HAS_STD_BYTE=0 _CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_DEPRECATE)
    endif()
    if (WIN32 AND ES40_HAVE_PCAP)
        find_path(ES40_CFG_PCAP_INCLUDE pcap/pcap.h HINTS "${NPCAP_DIR}/include" "C:/dev/npcap-sdk/include" REQUIRED)
        find_library(ES40_CFG_PCAP_LIBRARY wpcap HINTS "${NPCAP_DIR}/lib/x64" "C:/dev/npcap-sdk/lib/x64" REQUIRED)
        target_include_directories(es40_config_editor PRIVATE "${ES40_CFG_PCAP_INCLUDE}")
        target_link_libraries(es40_config_editor PUBLIC "${ES40_CFG_PCAP_LIBRARY}")
    endif()
endif()

if (WIN32)
    add_executable(es40_cfg_win32 src/config/es40-cfg-win32.cpp)
    set_target_properties(es40_cfg_win32 PROPERTIES OUTPUT_NAME "es40-cfg-win32")
    target_compile_definitions(es40_cfg_win32 PRIVATE WINVER=0x0601 _WIN32_WINNT=0x0601)
    target_link_libraries(es40_cfg_win32 PRIVATE es40_config_editor
        user32 gdi32 comctl32 shell32)
    if (MSVC AND ES40_HAVE_PCAP)
        target_link_libraries(es40_cfg_win32 PRIVATE delayimp)
        target_link_options(es40_cfg_win32 PRIVATE "/DELAYLOAD:wpcap.dll")
    endif()
endif()

if (ES40_ENABLE_CFG_TUI)
    set(CURSES_NEED_NCURSES ON)
    find_package(Curses REQUIRED)
    find_library(CURSES_MENU_LIBRARY NAMES menu REQUIRED)
    find_library(CURSES_FORM_LIBRARY NAMES form REQUIRED)
    find_library(CURSES_PANEL_LIBRARY NAMES panel REQUIRED)
    add_executable(es40_cfg_tui src/config/es40-cfg-tui.cpp)
    target_include_directories(es40_cfg_tui PRIVATE ${CURSES_INCLUDE_DIRS})
    target_link_libraries(es40_cfg_tui PRIVATE es40_config_editor ${CURSES_LIBRARIES}
        ${CURSES_MENU_LIBRARY} ${CURSES_FORM_LIBRARY} ${CURSES_PANEL_LIBRARY})
endif()

if (ES40_ENABLE_CFG_SDL AND NOT ES40_DISABLE_SDL)
    add_executable(es40_cfg_sdl src/config/es40-cfg-sdl.cpp)
    target_link_libraries(es40_cfg_sdl PRIVATE es40_config_editor SDL3::SDL3)
endif()

if (ES40_HAVE_GTK3)
    add_executable(es40_cfg_gtk src/config/es40-cfg-gtk.cpp)
    target_link_libraries(es40_cfg_gtk PRIVATE es40_config_editor PkgConfig::GTK3)
endif()

############################### ES40 ###############################

set(ES40_SOURCES)
set(ES40_INCLUDES)
set(ES40_LIBRARIES)
set(ES40_DEFINITIONS)

set(ES40_GIT_COMMIT "" CACHE STRING "Git commit to embed in ES40 binaries")
if (NOT ES40_GIT_COMMIT)
    if (DEFINED ENV{GITHUB_SHA} AND NOT "$ENV{GITHUB_SHA}" STREQUAL "")
        set(ES40_GIT_COMMIT "$ENV{GITHUB_SHA}")
    else()
        find_program(ES40_GIT_EXECUTABLE git)
        if (ES40_GIT_EXECUTABLE)
            execute_process(
                COMMAND "${ES40_GIT_EXECUTABLE}" rev-parse HEAD
                WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
                RESULT_VARIABLE ES40_GIT_RESULT
                OUTPUT_VARIABLE ES40_GIT_COMMIT
                OUTPUT_STRIP_TRAILING_WHITESPACE
                ERROR_QUIET
            )
        endif()
    endif()
endif()
if (NOT ES40_GIT_COMMIT OR (DEFINED ES40_GIT_RESULT AND NOT ES40_GIT_RESULT EQUAL 0))
    set(ES40_GIT_COMMIT "unknown")
endif()
list(APPEND ES40_DEFINITIONS "ES40_GIT_COMMIT=\"${ES40_GIT_COMMIT}\"")

list(APPEND ES40_SOURCES
    src/base/Exception.cpp
    src/base/ThreadTLS.cpp
	src/gui/gui.cpp
    src/gui/keymap.cpp
    src/gui/scancodes.cpp
    src/chipset/AliM1543C_ide.cpp
    src/chipset/AliM1543C_pmu.cpp
    src/chipset/AliM1543C_usb.cpp
    src/chipset/AliM1543C.cpp
    src/CPU/AlphaCPU_ieeefloat.cpp
    src/CPU/AlphaCPU_vaxfloat.cpp
    src/CPU/AlphaCPU_vmspal.cpp
    src/CPU/AlphaCPU.cpp
    src/CPU/Ev68IPR.cpp
    src/CPU/AlphaReset.h
    src/CPU/Ev68Reset.h
    src/CPU/Ev68Reset.cpp
    src/CPU/AlphaInstructionFetch.h
    src/CPU/AlphaInstructionSource.h
    src/CPU/Ev68InstructionFetch.h
    src/CPU/Ev68InstructionFetch.cpp
    src/CPU/AlphaMMU.h
    src/CPU/Ev68MMU.h
    src/CPU/Ev68MMU.cpp
    src/CPU/AlphaHWLoad.h
    src/CPU/Ev68HWLoad.h
    src/CPU/AlphaHWStore.h
    src/CPU/Ev68HWStore.h
    src/AlphaSim.cpp
    # src/graphics/Cirrus.cpp
    src/config/Configurator.cpp
    src/network/DEC21143.cpp
    src/storage/Disk.cpp
    src/storage/DiskController.cpp
    src/storage/DiskDevice.cpp
    src/storage/DiskFile.cpp
    src/storage/DiskFileQcow2.cpp
    src/storage/DiskRam.cpp
    src/chipset/DMA.cpp
    src/dox.cpp
    src/system/DPR.cpp
    src/es40_debug.cpp
    src/audio/ES1370.cpp
    src/audio/ES1371.cpp
    src/audio/HostAudio.cpp
    src/audio/HostAudioDirectSound.cpp
    src/audio/HostAudioNull.cpp
    src/audio/HostAudioSDL.cpp
    src/network/Ethernet.cpp
    src/system/Flash.cpp
    src/storage/FloppyController.cpp
    src/graphics/GloriaSynergy.cpp
    src/system/i2c_spd.cpp
    src/graphics/ibm8514a.cpp
    src/io/Keyboard.cpp
    src/network/lockstep.cpp
    src/audio/MPU401.cpp
    src/network/NetworkBackend.cpp
    src/network/NetworkPcap.cpp
    src/network/NetworkTap.cpp
    src/network/NetworkVmnet.cpp
    src/network/NetworkVmnetIPC.cpp
    src/emu/PCIDevice.cpp
    src/graphics/Permedia2.cpp
    src/io/Port80.cpp
    src/graphics/PowerStorm3xx.cpp
    src/graphics/Radeon7500.cpp
    src/graphics/REALimage2100.cpp
    src/graphics/RV200.cpp
    src/graphics/S3Trio64.cpp
    src/storage/SCSIBus.cpp
    src/storage/SCSIDevice.cpp
    src/io/Serial.cpp
    src/StdAfx.cpp
    src/storage/LSI53C1020.cpp
    src/storage/Sym53C810.cpp
    src/storage/Sym53C895.cpp
    src/system/System.cpp
    src/system/System_es40.cpp
    src/system/System_tsunami.cpp
    src/emu/SystemComponent.cpp
    src/CPU/TraceEngine.cpp
    src/graphics/VGA.cpp
    src/CPU/jit/jitengine.cpp
    src/CPU/jit/jitengine_x64.cpp
    src/CPU/jit/jitengine_a64.cpp
    )

if (WIN32)
    list(APPEND ES40_LIBRARIES ws2_32 dsound dxguid)
endif()

if (NOT ES40_DISABLE_SDL)
    list(APPEND ES40_DEFINITIONS HAVE_SDL)
    list(APPEND ES40_LIBRARIES SDL3::SDL3)

    list(APPEND ES40_SOURCES
        src/gui/sdl.cpp
        src/gui/sdl_media.cpp
        src/gui/sdl_media.h
        )

endif()

if (HAVE_X11)
    list(APPEND ES40_DEFINITIONS HAVE_X11)
    list(APPEND ES40_SOURCES src/gui/gui_x11.cpp src/gui/x11_dispatch.h)
    list(APPEND ES40_LIBRARIES X11::X11)
endif()

# The native Win32 GUI is independent of SDL, and defines the win32 plugin
# that Configurator always refers to under _WIN32, so it must be built on
# Windows even when SDL is disabled.
if (WIN32)
    list(APPEND ES40_SOURCES
        src/gui/gui_win32.cpp
        src/gui/win32_media.cpp
        src/gui/win32_media.h
        src/gui/win32_library.cpp
        src/gui/win32_library.h
        src/gui/win32_manager.cpp
        src/gui/win32_manager.h
        src/gui/win32_session.cpp
        src/gui/win32_session.h
        )
    list(APPEND ES40_LIBRARIES winmm comdlg32 comctl32 shell32 ole32 uuid)
endif()

if (NOT ES40_DISABLE_NETWORKING)
    if (ES40_HAVE_PCAP)
        list(APPEND ES40_DEFINITIONS HAVE_PCAP)
        if (WIN32)
            # list(APPEND ES40_LIBRARIES NPCap::NPCap)
        else()
            list(APPEND ES40_LIBRARIES PCap::PCap)
        endif()
    endif()
    if (ES40_HAVE_TAP)
        list(APPEND ES40_DEFINITIONS HAVE_TAP_NET)
    endif()
    if (ES40_HAVE_VMNET)
        list(APPEND ES40_DEFINITIONS HAVE_VMNET)
        list(APPEND ES40_LIBRARIES $<LINK_LIBRARY:FRAMEWORK,vmnet>)
    endif()
    if (ES40_HAVE_SLIRP)
        list(APPEND ES40_SOURCES src/network/NetworkSlirp.cpp)
        list(APPEND ES40_DEFINITIONS HAVE_SLIRP)
        list(APPEND ES40_LIBRARIES ${ES40_SLIRP_LINK_LIBRARIES})
    endif()
endif()

if (NOT WIN32)
    set(THREADS_PREFER_PTHREAD_FLAG ON)
    find_package(Threads REQUIRED)
    list(APPEND ES40_LIBRARIES Threads::Threads)
endif()

list(APPEND ES40_INCLUDES
    ${CMAKE_CURRENT_BINARY_DIR}/src
    src/
    src/emu
    src/base
    src/gui
    )

add_executable(es40 ${ES40_SOURCES})
if (WIN32)
    add_dependencies(es40 es40_cfg_win32)
endif()
# The Rust slirp static library must exist before any target links against it.
if (ES40_HAVE_SLIRP)
    add_dependencies(es40 es40_slirp_rust)
endif()
# The GPL/LGPL texts and third-party license notices required with the es40 binaries.
add_custom_command(TARGET es40 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/COPYING
        ${CMAKE_CURRENT_SOURCE_DIR}/COPYING.LGPL
        ${CMAKE_CURRENT_SOURCE_DIR}/src/es40-NOTICES.txt $<TARGET_FILE_DIR:es40>)
if (NOT ES40_DISABLE_IDB)
    add_executable(es40_idb ${ES40_SOURCES})
    if (ES40_HAVE_SLIRP)
        add_dependencies(es40_idb es40_slirp_rust)
    endif()
endif()
if (NOT ES40_DISABLE_LSS_LSM)
    add_executable(es40_lss ${ES40_SOURCES})
    add_executable(es40_lsm ${ES40_SOURCES})
    if (ES40_HAVE_SLIRP)
        add_dependencies(es40_lss es40_slirp_rust)
        add_dependencies(es40_lsm es40_slirp_rust)
    endif()
endif()

if (NOT ES40_DISABLE_ASMJIT)
    set(ASMJIT_NO_INSTALL ON)
    set(ASMJIT_STATIC ON)
    # Build only the backend matching the target: x86-64 is the real codegen host; ARM64
    # (incl. Apple Silicon) builds the JIT scaffolding, which always falls back to the
    # interpreter (no a64 code is emitted yet -- see src/CPU/jit/jitengine_a64.cpp).
    set(ASMJIT_NO_FOREIGN ON)
    add_subdirectory(third_party/asmjit)
    list(APPEND ES40_DEFINITIONS ES40_JIT)
    target_include_directories(es40 PRIVATE ${CMAKE_SOURCE_DIR}/third_party/asmjit)
    target_link_libraries(es40 PUBLIC asmjit)
endif()

target_include_directories(es40 PUBLIC ${ES40_INCLUDES})
if (NOT ES40_DISABLE_IDB)
    target_include_directories(es40_idb PUBLIC ${ES40_INCLUDES})
endif()
if (NOT ES40_DISABLE_LSS_LSM)
    target_include_directories(es40_lss PUBLIC ${ES40_INCLUDES})
    target_include_directories(es40_lsm PUBLIC ${ES40_INCLUDES})
    target_include_directories(es40_lss PRIVATE ${CMAKE_SOURCE_DIR}/third_party/asmjit)
    target_include_directories(es40_lsm PRIVATE ${CMAKE_SOURCE_DIR}/third_party/asmjit)
    if (NOT ES40_DISABLE_ASMJIT)
        target_link_libraries(es40_lss PUBLIC asmjit)
        target_link_libraries(es40_lsm PUBLIC asmjit)
    endif()
endif()

target_link_libraries(es40 PUBLIC ${ES40_LIBRARIES})
if (NOT ES40_DISABLE_IDB)
    target_link_libraries(es40_idb PUBLIC ${ES40_LIBRARIES})
    if (NOT ES40_DISABLE_ASMJIT)
        target_include_directories(es40_idb PRIVATE ${CMAKE_SOURCE_DIR}/third_party/asmjit)
    endif()
endif()
if (NOT ES40_DISABLE_LSS_LSM)
    target_link_libraries(es40_lss PUBLIC ${ES40_LIBRARIES})
    target_link_libraries(es40_lsm PUBLIC ${ES40_LIBRARIES})
endif()

if (ES40_HAVE_VMNET)
    # Keep the setuid vmnet process isolated from ES40's other dependencies.
    add_executable(es40_vmnet_helper
        src/network/es40_vmnet_helper.cpp)
    target_compile_definitions(es40_vmnet_helper PRIVATE HAVE_VMNET)
    target_link_libraries(es40_vmnet_helper PRIVATE
        $<LINK_LIBRARY:FRAMEWORK,vmnet>)
    set_property(TARGET es40_vmnet_helper PROPERTY CXX_STANDARD 17)
    add_dependencies(es40 es40_vmnet_helper)
endif()

target_compile_definitions(es40 PUBLIC ${ES40_DEFINITIONS})
if (NOT ES40_DISABLE_IDB)
    target_compile_definitions(es40_idb PUBLIC ${ES40_DEFINITIONS} IDB)
    if (NOT ES40_DISABLE_ASMJIT)
        target_link_libraries(es40_idb PUBLIC asmjit)
    endif()
endif()
if (NOT ES40_DISABLE_LSS_LSM)
    target_compile_definitions(es40_lss PUBLIC ${ES40_DEFINITIONS} IDB LS_SLAVE)
    target_compile_definitions(es40_lsm PUBLIC ${ES40_DEFINITIONS} IDB LS_MASTER)
endif()

set_property(TARGET es40 PROPERTY CXX_STANDARD 17)
if (NOT ES40_DISABLE_IDB)
    set_property(TARGET es40_idb PROPERTY CXX_STANDARD 17)
endif()
if (NOT ES40_DISABLE_LSS_LSM)
    set_property(TARGET es40_lss PROPERTY CXX_STANDARD 17)
    set_property(TARGET es40_lsm PROPERTY CXX_STANDARD 17)
endif()

############################ Install and package ############################

# `cmake --install build` installs the emulator and whichever editors and tools
# were built. `cpack -G DEB` in the build directory (or `cmake --build build
# --target package`) produces a Debian package on Linux; TGZ works everywhere.
include(GNUInstallDirs)
set(CMAKE_INSTALL_DOCDIR "${CMAKE_INSTALL_DATAROOTDIR}/doc/es40") # package name, not the project name
install(TARGETS es40 es40-cfg RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
foreach(optional_target es40_cfg_tui es40_cfg_sdl es40_cfg_gtk es40_cfg_win32 es40_idb es40_lss es40_lsm)
    if (TARGET ${optional_target})
        install(TARGETS ${optional_target} RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    endif()
endforeach()
install(FILES COPYING COPYING.LGPL README.md src/es40-NOTICES.txt DESTINATION ${CMAKE_INSTALL_DOCDIR})
install(FILES src/es40.cfg DESTINATION ${CMAKE_INSTALL_DOCDIR}/examples)
if (TARGET es40_cfg_gtk)
    install(FILES extras/packaging/es40-cfg-gtk.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
endif()

set(ES40_PACKAGE_MAINTAINER "ES40 Emulator Project <https://github.com/ES40-Emu/es40>"
    CACHE STRING "Maintainer recorded in generated packages")
set(CPACK_PACKAGE_NAME "es40")
set(CPACK_PACKAGE_VENDOR "ES40 Emulator Project")
set(CPACK_PACKAGE_CONTACT "${ES40_PACKAGE_MAINTAINER}")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/ES40-Emu/es40")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "AlphaServer ES40 emulator")
# The CPU baseline is part of the package identity: an AVX2 build dies with
# "Illegal instruction" on older hosts, so the file name and description say
# which one this is. Build with -DES40_ARCH=SSE2 for a package that runs anywhere.
if (CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|amd64|i[3-6]86|x86")
    string(TOLOWER "-${ES40_ARCH}" ES40_PACKAGE_SUFFIX)
    set(ES40_PACKAGE_BASELINE "
Built for the ${ES40_ARCH} x86 CPU baseline (ES40_ARCH=${ES40_ARCH}).")
else()
    set(ES40_PACKAGE_SUFFIX "")
    set(ES40_PACKAGE_BASELINE "")
endif()
set(CPACK_PACKAGE_DESCRIPTION "ES40 emulates a Compaq/HP AlphaServer ES40 so that OpenVMS, Tru64 and other
Alpha operating systems run on ordinary hardware. The package also contains the
configuration tools: es40-cfg (guided setup) and the es40_cfg_* editors.${ES40_PACKAGE_BASELINE}")
set(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}_${CPACK_PACKAGE_VERSION}_${CMAKE_SYSTEM_PROCESSOR}${ES40_PACKAGE_SUFFIX}")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/COPYING")
set(CPACK_RESOURCE_FILE_README "${CMAKE_CURRENT_SOURCE_DIR}/README.md")
set(CPACK_STRIP_FILES ON)
set(CPACK_SOURCE_IGNORE_FILES "/[.]git/" "/build/" "/docs/")
if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(CPACK_GENERATOR "DEB;TGZ")
    set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
    set(CPACK_DEBIAN_PACKAGE_SECTION "otherosfs")
    set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
    set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "${CPACK_PACKAGE_HOMEPAGE_URL}")
    # dpkg-shlibdeps (package dpkg-dev) derives the Depends line from the linked libraries.
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
    # postinst grants es40 the raw-socket capability so pcap and tap networking
    # work without root; setcap comes from libcap2-bin.
    set(CPACK_DEBIAN_PACKAGE_DEPENDS "libcap2-bin")
    # vgabios: a free S3-compatible video BIOS the editor offers by default.
    # 7zip: lets the editor import firmware straight from the HP update CD .iso or a .zip.
    set(CPACK_DEBIAN_PACKAGE_RECOMMENDS "vgabios, 7zip | p7zip-full")
    set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA "${CMAKE_CURRENT_SOURCE_DIR}/extras/packaging/postinst")
    set(CPACK_DEBIAN_PACKAGE_CONTROL_STRICT_PERMISSION TRUE)
elseif (WIN32)
    set(CPACK_GENERATOR "ZIP")
else()
    set(CPACK_GENERATOR "TGZ")
endif()
include(CPack)

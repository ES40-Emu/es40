cmake_minimum_required(VERSION 3.24)

project(ES40 VERSION 0.86.1 LANGUAGES C CXX)

# Single-config generators default to an empty build type. Fix that up. 
get_property(ES40_MULTI_CONFIG GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if (NOT ES40_MULTI_CONFIG AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type (Debug, Release, RelWithDebInfo, MinSizeRel)" FORCE)
endif()

option(ES40_DISABLE_SDL "Disable SDL for headless builds" OFF)
option(ES40_DISABLE_NETWORKING "Disable PCap/networking for networkless builds" OFF)
option(ES40_DISABLE_TAP "Disable TAP networking even on platforms that support it (Linux/FreeBSD/NetBSD)" OFF)
option(ES40_DISABLE_VMNET "Disable vmnet networking on macOS" OFF)
option(ES40_DISABLE_LSS_LSM "Disable LSS/LSM (lockstep) builds." OFF)
option(ES40_DISABLE_IDB "Disable IDB (built-in debugger) builds." OFF)
option(ES40_DISABLE_ASMJIT "Disable ASMJIT" ON)
option(ES40_ENABLE_CFG_TUI "Enable build of es40_cfg_tui" ON)

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

set(ES40_HAVE_PCAP FALSE)
set(ES40_HAVE_TAP FALSE)

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

        if (NOT ES40_HAVE_PCAP AND NOT ES40_HAVE_TAP AND NOT ES40_HAVE_VMNET)
            message(FATAL_ERROR "Networking was requested (ES40_DISABLE_NETWORKING=OFF) but neither libpcap nor TAP nor vmnet support is available for this platform/build. Install libpcap, or configure with -DES40_DISABLE_NETWORKING=ON to build without networking.")
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
    check_include_file("X11/X.h" HAVE_X11)

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
    src/es40-cfg.cpp
    src/base/Exception.cpp
    )

if (WIN32)
    list(APPEND ES40_CFG_LIBRARIES winmm)
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
endif()

if (NOT ES40_DISABLE_SDL)
    list(APPEND ES40_CFG_DEFINITIONS HAVE_SDL)
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

########################### ES40_CFG_TUI ###########################

if (ES40_ENABLE_CFG_TUI)
	set(ES40_CFG_TUI_SOURCES)
	set(ES40_CFG_TUI_DEFINITIONS)
	set(ES40_CFG_TUI_LIBRARIES)
	
	list(APPEND ES40_CFG_TUI_SOURCES
	    src/es40-cfg-tui.cpp
		src/Configurator.cpp
	    src/base/Exception.cpp
	    )

	list(APPEND ES40_CFG_TUI_DEFINITIONS
		CONFIGURATION_ONLY
	    )

	if (NOT ES40_DISABLE_ASMJIT)
		list(APPEND ES40_CFG_TUI_DEFINITIONS ES40_JIT)
	endif()

	if (NOT ES40_DISABLE_NETWORKING)
		if (ES40_HAVE_PCAP)
			list(APPEND ES40_CFG_TUI_DEFINITIONS HAVE_PCAP)
			if (WIN32)
				#list(APPEND ES40_CFG_TUI_LIBRARIES NPCap::NPCap)
			else()
				list(APPEND ES40_CFG_TUI_LIBRARIES PCap::PCap)
			endif()
		endif()
		if (ES40_HAVE_TAP)
			list(APPEND ES40_CFG_TUI_DEFINITIONS HAVE_TAP_NET)
		endif()
		if (CMAKE_SYSTEM_NAME STREQUAL "Darwin")
			list(APPEND ES40_CFG_TUI_DEFINITIONS HAVE_VMNET)
	        list(APPEND ES40_CFG_TUI_LIBRARIES $<LINK_LIBRARY:FRAMEWORK,vmnet>)
		endif()
	endif()

	if (NOT ES40_DISABLE_SDL)
		list(APPEND ES40_CFG_TUI_DEFINITIONS HAVE_SDL)
	endif()

	if (WIN32)
	    list(APPEND ES40_CFG_TUI_LIBRARIES winmm)
	endif()

	# Curses config
	# see https://cmake.org/cmake/help/v3.24/module/FindCurses.html
	# modeled after https://runebook.dev/en/docs/cmake/module/findcurses
    set(CURSES_NEED_NCURSES ON)
	find_package(Curses REQUIRED)

	# Manually find the required sub-libraries
	find_library(CURSES_MENU_LIBRARY NAMES menu)
	find_library(CURSES_FORM_LIBRARY NAMES form)
	find_library(CURSES_PANEL_LIBRARY NAMES panel)
	
	# Collect all Curses-related libraries
	set(ALL_CURSES_LIBS 
	    ${CURSES_LIBRARIES} 
	    ${CURSES_MENU_LIBRARY} 
	    ${CURSES_FORM_LIBRARY}
	    ${CURSES_PANEL_LIBRARY}
	)

	# Ensure the sub-libraries were found
	if(NOT CURSES_MENU_LIBRARY OR NOT CURSES_FORM_LIBRARY OR NOT CURSES_PANEL_LIBRARY)
	    message(FATAL_ERROR "Missing Curses sub-libraries (menu/form/panel).")
	endif()

	# Manually create the Curses::Curses INTERFACE imported target
	if(CURSES_FOUND AND NOT TARGET Curses::Curses)
	    # The INTERFACE library holds the properties needed for other targets to link against
	    add_library(Curses::Curses INTERFACE IMPORTED)
	    
	    # Set the properties
	    set_target_properties(Curses::Curses PROPERTIES
	        INTERFACE_LINK_LIBRARIES "${ALL_CURSES_LIBS}"
	        INTERFACE_INCLUDE_DIRECTORIES "${CURSES_INCLUDE_DIRS}"
	    )
	endif()
	
	list(APPEND ES40_CFG_TUI_LIBRARIES Curses::Curses)

	add_executable(es40_cfg_tui ${ES40_CFG_TUI_SOURCES})
	
	target_include_directories(es40_cfg_tui PUBLIC src/)
	target_compile_definitions(es40_cfg_tui PUBLIC ${ES40_CFG_TUI_DEFINITIONS})
	target_link_libraries(es40_cfg_tui PUBLIC ${ES40_CFG_TUI_LIBRARIES})
	
	set_property(TARGET es40_cfg_tui PROPERTY CXX_STANDARD 17)
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
    src/AliM1543C_ide.cpp
    src/AliM1543C_pmu.cpp
    src/AliM1543C_usb.cpp
    src/AliM1543C.cpp
    src/AlphaCPU_ieeefloat.cpp
    src/AlphaCPU_vaxfloat.cpp
    src/AlphaCPU_vmspal.cpp
    src/AlphaCPU.cpp
    src/AlphaSim.cpp
    # src/Cirrus.cpp
    src/Configurator.cpp
    src/DEC21143.cpp
    src/Disk.cpp
    src/DiskController.cpp
    src/DiskDevice.cpp
    src/DiskFile.cpp
    src/DiskRam.cpp
    src/DMA.cpp
    src/dox.cpp
    src/DPR.cpp
    src/es40_debug.cpp
    src/ES1370.cpp
    src/network/Ethernet.cpp
    src/Flash.cpp
    src/FloppyController.cpp
    src/i2c_spd.cpp
    src/ibm8514a.cpp
    src/Keyboard.cpp
    src/network/lockstep.cpp
    src/MPU401.cpp
    src/network/NetworkBackend.cpp
    src/network/NetworkPcap.cpp
    src/network/NetworkTap.cpp
    src/network/NetworkVmnet.cpp
    src/network/NetworkVmnetIPC.cpp
    src/PCIDevice.cpp
    src/Port80.cpp
    src/S3Trio64.cpp
    src/SCSIBus.cpp
    src/SCSIDevice.cpp
    src/Serial.cpp
    src/StdAfx.cpp
    src/LSI53C1020.cpp
    src/Sym53C810.cpp
    src/Sym53C895.cpp
    src/System.cpp
    src/SystemComponent.cpp
    src/TraceEngine.cpp
    src/VGA.cpp
    src/jit/jitengine.cpp
    src/jit/jitengine_x64.cpp
    src/jit/jitengine_a64.cpp
    )

if (WIN32)
    list(APPEND ES40_LIBRARIES ws2_32)
endif()

if (NOT ES40_DISABLE_SDL)
    list(APPEND ES40_DEFINITIONS HAVE_SDL)
    list(APPEND ES40_LIBRARIES SDL3::SDL3)

    list(APPEND ES40_SOURCES
        src/gui/sdl.cpp
        src/gui/sdl_media.cpp
        src/gui/sdl_media.h
        )
    
    if (WIN32)
        list(APPEND ES40_SOURCES
            src/gui/gui_win32.cpp
            )
        list(APPEND ES40_LIBRARIES winmm)
    else()
        list(APPEND ES40_SOURCES
            src/gui/gui_x11.cpp
            )
    endif()
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
if (NOT ES40_DISABLE_IDB)
    add_executable(es40_idb ${ES40_SOURCES})
endif()
if (NOT ES40_DISABLE_LSS_LSM)
    add_executable(es40_lss ${ES40_SOURCES})
    add_executable(es40_lsm ${ES40_SOURCES})
endif()

if (NOT ES40_DISABLE_ASMJIT)
    set(ASMJIT_NO_INSTALL ON)
    # Build only the backend matching the target: x86-64 is the real codegen host; ARM64
    # (incl. Apple Silicon) builds the JIT scaffolding, which always falls back to the
    # interpreter (no a64 code is emitted yet -- see src/jit/jitengine_a64.cpp).
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

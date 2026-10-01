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
# X-1.6      Caolan McNamara                          12-JUN-2008
#      Fixes to build on Fedora 9 / gcc 4.3.0.
#
# X-1.5      Camiel Vanderhoeven                      31-MAY-2008
#      Add parts of Poco.
#
# X-1.4      Camiel Vanderhoeven                      04-APR-2008
#      Added comments.
#
# X-1.3      Camiel Vanderhoeven                      29-MAR-2008
#      Added es40_cfg.
#
# X-1.1	     Camiel Vanderhoeven                      20-MAR-2008
#      File Created.
#
################################################################################

bin_PROGRAMS = es40 es40_cfg
if ENABLE_TOOLS
bin_PROGRAMS += es40_idb es40_lss es40_lsm
endif
if BUILD_TUI
bin_PROGRAMS += es40_cfg_tui
endif

# Third-party license notices required with the es40 binaries.
dist_doc_DATA = es40-NOTICES.txt

# vmnet needs root, but AppKit refuses to run in a setuid process so
# small helper to work with vmnet directly - OS X only. 
if HAVE_VMNET
bin_PROGRAMS += es40_vmnet_helper
es40_vmnet_helper_SOURCES = network/es40_vmnet_helper.cpp network/NetworkVmnetProto.h
es40_vmnet_helper_LDFLAGS = -framework vmnet
EXTRA_es40_DEPENDENCIES = es40_vmnet_helper$(EXEEXT)

install-exec-hook:
	@helper="$(DESTDIR)$(bindir)/es40_vmnet_helper"; \
	if chown root:wheel "$$helper" && chmod 4755 "$$helper"; then \
	  echo "es40_vmnet_helper installed setuid root"; \
	else \
	  echo "WARNING: could not make $$helper setuid root;"; \
	  echo "         vmnet networking needs it."; \
	  echo "Run: sudo chown root:wheel $$helper; sudo chmod 4755 $$helper"; \
	fi
endif

AM_CPPFLAGS = -I$(top_srcdir)/src/emu

es40_LDADD = $(ASMJIT_LDADD) $(ES40_LIBS)
es40_cfg_LDADD = $(ES40_LIBS)
es40_idb_LDADD = $(ASMJIT_LDADD) $(ES40_LIBS)
es40_lss_LDADD = $(ASMJIT_LDADD) $(ES40_LIBS)
es40_lsm_LDADD = $(ASMJIT_LDADD) $(ES40_LIBS)

#       graphics/Cirrus.cpp  - readd this to the sources line below when it's fixed

es40_SOURCES = AliM1543C.cpp \
       AliM1543C_ide.cpp \
       AliM1543C_usb.cpp \
       AliM1543C_pmu.cpp \
       AlphaCPU.cpp \
       AlphaCPU_ieeefloat.cpp \
       AlphaCPU_vaxfloat.cpp \
       AlphaCPU_vmspal.cpp \
       AlphaSim.cpp \
       Configurator.cpp \
       DEC21143.cpp \
       Disk.cpp \
       DiskController.cpp \
       DiskDevice.cpp \
       DiskFile.cpp \
       DiskRam.cpp \
       graphics/ibm8514a.cpp \
       DMA.cpp \
       DPR.cpp \
       es40_debug.cpp \
       ES1370.cpp \
       network/Ethernet.cpp \
       Flash.cpp \
       FloppyController.cpp \
       Keyboard.cpp \
       network/lockstep.cpp \
       network/NetworkBackend.cpp \
       network/NetworkPcap.cpp \
       network/NetworkTap.cpp \
       network/NetworkVmnet.cpp \
       network/NetworkVmnetIPC.cpp \
       network/NetworkVmnetIPC.h \
       PCIDevice.cpp \
       Port80.cpp \
       graphics/S3Trio64.cpp \
       graphics/GloriaSynergy.cpp \
       graphics/GloriaSynergy.h \
       graphics/Permedia2.cpp \
       graphics/Permedia2.h \
       graphics/Radeon7500.cpp \
       graphics/Radeon7500.h \
       graphics/RV200.cpp \
       graphics/RV200.h \
       graphics/PowerStorm3xx.cpp \
       graphics/PowerStorm3xx.h \
       graphics/REALimage2100.cpp \
       graphics/REALimage2100.h \
       SCSIBus.cpp \
       SCSIDevice.cpp \
       Serial.cpp \
       SerialListenAddress.h \
       StdAfx.cpp \
       LSI53C1020.cpp \
       Sym53C810.cpp \
       Sym53C895.cpp \
       SystemComponent.cpp \
       System.cpp \
       System_es40.cpp \
       System_tsunami.cpp \
       SnapshotFile.h \
       TraceEngine.cpp \
       graphics/VGA.cpp \
       i2c_spd.cpp \
	base/ThreadTLS.cpp \
       gui/gui.cpp \
       gui/gui_x11.cpp \
       gui/keymap.cpp \
       gui/scancodes.cpp \
       gui/sdl.cpp \
	gui/sdl_media.cpp \
	gui/sdl_media.h \
	base/Exception.cpp

if ENABLE_ASMJIT
include jit/asmjit.am
AM_CPPFLAGS += -I$(top_srcdir)/src/jit -DES40_JIT \
               -I$(top_srcdir)/third_party/asmjit
# release build needs this for ASMJIT, and that's mostly what we have, if we want 
# asmjit debug, change to ASMJIT_BUILD_DEBUG. 
AM_CPPFLAGS += -DASMJIT_BUILD_RELEASE
# Build only the host-matching asmjit backend: x86-64 is the real codegen target;
# ARM64 is scaffolding (a64 backend compiled for future use, no code emitted yet).
if ES40_JIT_HOST_A64
AM_CPPFLAGS += -DASMJIT_NO_X86
else
AM_CPPFLAGS += -DASMJIT_NO_AARCH64
endif

# Both backends are listed unconditionally; each is a no-op TU on the other host arch.
es40_SOURCES += jit/jitengine.cpp jit/jitengine_x64.cpp jit/jitengine_a64.cpp

ASMJIT_LDADD = libasmjit.a
endif

es40_idb_SOURCES = $(es40_SOURCES)
es40_lss_SOURCES = $(es40_SOURCES)
es40_lsm_SOURCES = $(es40_SOURCES)

es40_idb_CXXFLAGS = -DIDB
es40_lss_CXXFLAGS = -DIDB -DLS_SLAVE
es40_lsm_CXXFLAGS = -DIDB -DLS_MASTER

es40_cfg_SOURCES = es40-cfg.cpp \
    base/Exception.cpp

# CONFIGURATION_ONLY strips device code out of Configurator.cpp; the per-target
# flags also keep these objects separate from es40's Configurator.o.
es40_cfg_tui_CPPFLAGS = $(AM_CPPFLAGS) $(TUI_CFLAGS) -DCONFIGURATION_ONLY
es40_cfg_tui_LDADD = $(TUI_LIBS) $(PCAP_LIBS)
es40_cfg_tui_SOURCES = es40-cfg-tui.cpp \
    Configurator.cpp \
    base/Exception.cpp

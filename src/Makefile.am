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

# CPU headers are distributed with the emulator sources.
noinst_HEADERS = CPU/AlphaCPU.h \
       CPU/AlphaCPUProfile.h \
       CPU/idle_detect.h \
       CPU/AlphaPAL.h \
       CPU/Ev68PAL.h \
       CPU/AlphaIPR.h \
       CPU/Ev68IPR.h \
       CPU/AlphaMMU.h \
       CPU/Ev68MMU.h \
       CPU/cpu_arith.h \
       CPU/cpu_bwx.h \
       CPU/cpu_control.h \
       CPU/cpu_debug.h \
       CPU/cpu_defs.h \
       CPU/cpu_fp_branch.h \
       CPU/cpu_fp_memory.h \
       CPU/cpu_fp_operate.h \
       CPU/cpu_logical.h \
       CPU/cpu_memory.h \
       CPU/cpu_misc.h \
       CPU/cpu_mvi.h \
       CPU/cpu_pal.h \
       CPU/cpu_vax.h \
       CPU/diag_rpcc.h \
       CPU/TraceEngine.h \
       CPU/jit/jitengine.h \
       CPU/jit/jitengine_internal.h

noinst_HEADERS += audio/ES1370.h audio/MPU401.h

noinst_HEADERS += storage/Disk.h \
       storage/DiskController.h \
       storage/DiskDevice.h \
       storage/DiskFile.h \
       storage/DiskFileBinCue.h \
       storage/DiskRam.h \
       storage/FloppyController.h \
       storage/LSI53C1020.h \
       storage/SCSIBus.h \
       storage/SCSIDevice.h \
       storage/Sym53C810.h \
       storage/Sym53C895.h \
       chipset/AliM1543C.h \
       chipset/AliM1543C_ide.h \
       chipset/AliM1543C_pmu.h \
       chipset/AliM1543C_usb.h \
       chipset/DMA.h \
       system/DPR.h \
       system/Flash.h \
       system/System.h \
       system/i2c_spd.h \
       io/Keyboard.h \
       io/Port80.h \
       io/Serial.h \
       io/SerialListenAddress.h \
       config/Configurator.h \
       config/FreeTextQuestion.h \
       config/MultipleChoiceQuestion.h \
       config/NumberQuestion.h \
       config/Question.h \
       config/ShrinkingChoiceQuestion.h \
       network/DEC21143.h \
       network/DEC21143_mii.h \
       network/DEC21143_tulipreg.h \
       emu/SystemComponent.h \
       emu/PCIDevice.h \
       emu/address_map.h \
       emu/mame_shims.h \
       emu/attotime.h \
       emu/xtal.h \
       emu/coretmpl.h

es40_LDADD = $(ASMJIT_LDADD) $(ES40_LIBS)
es40_cfg_LDADD = $(ES40_LIBS)
es40_idb_LDADD = $(ASMJIT_LDADD) $(ES40_LIBS)
es40_lss_LDADD = $(ASMJIT_LDADD) $(ES40_LIBS)
es40_lsm_LDADD = $(ASMJIT_LDADD) $(ES40_LIBS)

#       graphics/Cirrus.cpp  - readd this to the sources line below when it's fixed

es40_SOURCES = chipset/AliM1543C.cpp \
       chipset/AliM1543C_ide.cpp \
       chipset/AliM1543C_usb.cpp \
       chipset/AliM1543C_pmu.cpp \
       CPU/AlphaCPU.cpp \
       CPU/Ev68IPR.cpp \
       CPU/Ev68MMU.cpp \
       CPU/AlphaCPU_ieeefloat.cpp \
       CPU/AlphaCPU_vaxfloat.cpp \
       CPU/AlphaCPU_vmspal.cpp \
       AlphaSim.cpp \
       config/Configurator.cpp \
       network/DEC21143.cpp \
       storage/Disk.cpp \
       storage/DiskController.cpp \
       storage/DiskDevice.cpp \
       storage/DiskFile.cpp \
       storage/DiskRam.cpp \
       graphics/ibm8514a.cpp \
       chipset/DMA.cpp \
       system/DPR.cpp \
       es40_debug.cpp \
       audio/ES1370.cpp \
       network/Ethernet.cpp \
       system/Flash.cpp \
       storage/FloppyController.cpp \
       io/Keyboard.cpp \
       network/lockstep.cpp \
       network/NetworkBackend.cpp \
       network/NetworkPcap.cpp \
       network/NetworkTap.cpp \
       network/NetworkVmnet.cpp \
       network/NetworkVmnetIPC.cpp \
       network/NetworkVmnetIPC.h \
       emu/PCIDevice.cpp \
       io/Port80.cpp \
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
       storage/SCSIBus.cpp \
       storage/SCSIDevice.cpp \
       io/Serial.cpp \
       io/SerialListenAddress.h \
       StdAfx.cpp \
       storage/LSI53C1020.cpp \
       storage/Sym53C810.cpp \
       storage/Sym53C895.cpp \
       emu/SystemComponent.cpp \
       system/System.cpp \
       system/System_es40.cpp \
       system/System_tsunami.cpp \
       SnapshotFile.h \
       CPU/TraceEngine.cpp \
       graphics/VGA.cpp \
       system/i2c_spd.cpp \
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
include CPU/jit/asmjit.am
AM_CPPFLAGS += -I$(top_srcdir)/src/CPU/jit -DES40_JIT \
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
es40_SOURCES += CPU/jit/jitengine.cpp CPU/jit/jitengine_x64.cpp CPU/jit/jitengine_a64.cpp

ASMJIT_LDADD = libasmjit.a
endif

es40_idb_SOURCES = $(es40_SOURCES)
es40_lss_SOURCES = $(es40_SOURCES)
es40_lsm_SOURCES = $(es40_SOURCES)

es40_idb_CXXFLAGS = -DIDB
es40_lss_CXXFLAGS = -DIDB -DLS_SLAVE
es40_lsm_CXXFLAGS = -DIDB -DLS_MASTER

es40_cfg_SOURCES = config/es40-cfg.cpp \
    base/Exception.cpp

# CONFIGURATION_ONLY strips device code out of config/Configurator.cpp; the per-target
# flags also keep these objects separate from es40's Configurator.o.
es40_cfg_tui_CPPFLAGS = $(AM_CPPFLAGS) $(TUI_CFLAGS) -DCONFIGURATION_ONLY
es40_cfg_tui_LDADD = $(TUI_LIBS) $(PCAP_LIBS)
es40_cfg_tui_SOURCES = config/es40-cfg-tui.cpp \
    config/Configurator.cpp \
    base/Exception.cpp

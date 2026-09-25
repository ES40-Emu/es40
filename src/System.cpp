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
  * Contains the code for the emulated Typhoon Chipset devices.
  *
  * $Id$
  *
  * X-1.81       Camiel Vanderhoeven                             12-JUN-2008
  *      Support to keep secondary CPUs waiting until activated from primary.
  *
  * X-1.78       Camiel Vanderhoeven                             02-JUN-2008
  *      Remove hard references to CPU 1 from decompression routine.
  *
  * X-1.77       Camiel Vanderhoeven                             31-MAY-2008
  *      Changes to include parts of Poco.
  *
  * X-1.76       Brian Wheeler                                   29-APR-2008
  *      Added memory map dumping and checking for overlapping memory ranges
  *      (enabled with DUMP_MEMMAP and CHECK_MEM_RANGES, respectively).
  *
  * X-1.75       Camiel Vanderhoeven                             26-MAR-2008
  *      Fix compiler warnings.
  *
  * X-1.74       Pepito Grillo                                   25-MAR-2008
  *      Fixed a typo.
  *
  * X-1.73       Camiel Vanderhoeven                             14-MAR-2008
  *      Formatting.
  *
  * X-1.72       Camiel Vanderhoeven                             14-MAR-2008
  *   1. More meaningful exceptions replace throwing (int) 1.
  *   2. U64 macro replaces X64 macro.
  *
  * X-1.71       Camiel Vanderhoeven                             13-MAR-2008
  *      Create init(), start_threads() and stop_threads() functions.
  *
  * X-1.70       Camiel Vanderhoeven                             11-MAR-2008
  *      Named, debuggable mutexes.
  *
  * X-1.68       Camiel Vanderhoeven                             05-MAR-2008
  *      Multi-threading version.
  *
  * X-1.67       Camiel Vanderhoeven                             04-MAR-2008
  *      Support some basic MP features. (CPUID read from C-Chip MISC
  *      register, inter-processor interrupts)
  *
  * X-1.66       Brian Wheeler                                   02-MAR-2008
  *      Allow large memory sizes (>1GB).
  *
  * X-1.65       Camiel Vanderhoeven                             02-MAR-2008
  *      Natural way to specify large numeric values ("10M") in the config file.
  *
  * X-1.64       Brian Wheeler                                   29-FEB-2008
  *      Do not generate unknown PCI 0 memory messages for legacy VGA
  *      memory region.
  *
  * X-1.63       Brian Wheeler                                   26-FEB-2008
  *      Support reading from Pchip TLBIV and TLBIA registers. (Which are
  *      supposed to be write-only!)
  *
  * X-1.62       David Leonard                                   20-FEB-2008
  *      Flush stdout during decompression progress.
  *
  * X-1.61       Camiel Vanderhoeven                             08-FEB-2008
  *      Show originating device name on memory errors.
  *
  * X-1.60       Camiel Vanderhoeven                             01-FEB-2008
  *      Avoid unnecessary shift-operations to calculate constant values.
  *
  * X-1.59       Camiel Vanderhoeven                             28-JAN-2008
  *      Avoid compiler warnings.
  *
  * X-1.58       Camiel Vanderhoeven                             25-JAN-2008
  *      Added option to disable the icache.
  *
  * X-1.57       Camiel Vanderhoeven                             19-JAN-2008
  *      Run CPU in a separate thread if CPU_THREADS is defined.
  *      NOTA BENE: This is very experimental, and has several problems.
  *
  * X-1.56       Camiel Vanderhoeven                             18-JAN-2008
  *      Process device interrupts after a 100-cpu-cycle delay.
  *
  * X-1.55       Camiel Vanderhoeven                             12-JAN-2008
  *      Comments.
  *
  * X-1.54       Camiel Vanderhoeven                             09-JAN-2008
  *      Let PtrToMemory return NULL when the address is out of range.
  *
  * X-1.53       Camiel Vanderhoeven                             08-JAN-2008
  *      Layout of comments.
  *
  * X-1.52       Camiel Vanderhoeven                             08-JAN-2008
  *      Split out chipset registers.
  *
  * X-1.51       Camiel Vanderhoeven                             07-JAN-2008
  *      Corrected error in last update; csr reg. 0x600, not 0600...
  *
  * X-1.50       Camiel Vanderhoeven                             07-JAN-2008
  *      DMA scatter/gather access. Split out some things.
  *
  * X-1.49       Camiel Vanderhoeven                             02-JAN-2008
  *      Cleanup.
  *
  * X-1.48       Camiel Vanderhoeven                             30-DEC-2007
  *      Comments.
  *
  * X-1.47       Camiel Vanderhoeven                             30-DEC-2007
  *      Fixed error in printf again.
  *
  * X-1.46       Camiel Vanderhoeven                             30-DEC-2007
  *      Fixed error in printf.
  *
  * X-1.45       Camiel Vanderhoeven                             30-DEC-2007
  *      Print file id on initialization.
  *
  * X-1.44       Camiel Vanderhoeven                             29-DEC-2007
  *      Fix memory-leak.
  *
  * X-1.43       Camiel Vanderhoeven                             28-DEC-2007
  *      Throw exceptions rather than just exiting when errors occur.
  *
  * X-1.42       Camiel Vanderhoeven                             28-DEC-2007
  *      Keep the compiler happy.
  *
  * X-1.41       Camiel Vanderhoeven                             20-DEC-2007
  *      Close files and free memory when the emulator shuts down.
  *
  * X-1.40       Camiel Vanderhoeven                             17-DEC-2007
  *      SaveState file format 2.1
  *
  * X-1.39       Camiel Vanderhoeven                             14-DEC-2007
  *      Commented out SRM IDE READ replacement; doesn't work with SCSI!
  *
  * X-1.38       Camiel Vanderhoeven                             10-DEC-2007
  *      Added get_cpu
  *
  * X-1.37       Camiel Vanderhoeven                             10-DEC-2007
  *      Use configurator.
  *
  * X-1.36       Camiel Vanderhoeven                             6-DEC-2007
  *      Report references to unused PCI space.
  *
  * X-1.35       Camiel Vanderhoeven                             2-DEC-2007
  *      Avoid misprobing of unused PCI configuration space.
  *
  * X-1.34       Camiel Vanderhoeven                             2-DEC-2007
  *      Added support for code profiling, and for direct operations on the
  *      Tsunami/Typhoon's interrupt registers.
  *
  * X-1.33       Brian Wheeler                                   1-DEC-2007
  *   1. Ignore address bits 35- 42 in the physical address; this is
  *      correct according to the Tsunami/Typhoon HRM; which states that
  *       "  The system address space is divided into two parts: system
  *        memory and PIO. This division is indicated by physical memory bit
  *        <43> = 1 for PIO accesses from the CPU [...] In general, bits
  *        <42:35> are don’t cares if bit <43> is asserted. [...] The
  *        Typhoon Cchip supports 32GB of system memory (35 bits total).  "
  *   2. Added support for Ctrl+C and panic.
  *
  * X-1.32       Camiel Vanderhoeven                             17-NOV-2007
  *      Use CHECK_ALLOCATION.
  *
  * X-1.31       Camiel Vanderhoeven                             16-NOV-2007
  *      Replaced PCI_ReadMem and PCI_WriteMem with PCI_Phys.
  *
  * X-1.30       Camiel Vanderhoeven                             05-NOV-2007
  *      Put slow-to-fast clock ratio into #define CLOCK_RATIO. Increased
  *      this to 100,000.
  *
  * X-1.29       Camiel Vanderhoeven                             18-APR-2007
  *      Decompressed ROM image is now identical between big- and small-
  *      endian platforms (put endian_64 around PALbase and PC).
  *
  * X-1.28       Camiel Vanderhoeven                             18-APR-2007
  *      Faster lockstep mechanism (send info 50 cpu cycles at a time)
  *
  * X-1.27       Camiel Vanderhoeven                             16-APR-2007
  *      Remove old address range if a new one is registered (same device/
  *      same index)
  *
  * X-1.26       Camiel Vanderhoeven                             16-APR-2007
  *      Allow configuration strings with spaces in them.
  *
  *
  * X-1.25       Camiel Vanderhoeven                             11-APR-2007
  *      Moved all data that should be saved to a state file to a structure
  *      "state".
  *
  * X-1.24	Camiel Vanderhoeven				10-APR-2007
  *	New mechanism for SRM replacements. Where these need to be executed,
  *	CSystem::LoadROM() puts a special opcode (a CALL_PAL instruction
  *	with an otherwise illegal operand of 0x01234xx) in memory.
  *	CAlphaCPU::DoClock() recognizes these opcodes and performs the SRM
  *	action.
  *
  * X-1.23       Camiel Vanderhoeven                             10-APR-2007
  *      Extended ROM-handling code to favor loading decompressed ROM code
  *      over loading compressed code, and to save decompressed ROM code
  *      during the first time the emulator is run.
  *
  * X-1.22       Camiel Vanderhoeven                             10-APR-2007
  *      Removed obsolete ROM-handling code.
  *
  * X-1.21       Brian Wheeler                                   31-MAR-2007
  *      Removed ; after #endif to avoid compiler warnings.
  *
  * X-1.20       Camiel Vanderhoeven                             26-MAR-2007
  *      Show references to unknown memory regions when DEBUG_UNKMEM is
  *	defined.
  *
  * X-1.19	Camiel Vanderhoeven				1-MAR-2007
  *	Changes for Solaris/SPARC port:
  *   a)	All $-signs in variable names are replaced with underscores.
  *   b) Some functions now get a const char * argument i.s.o. char * to avoid
  *	compiler warnings.
  *   c) If ALIGN_MEM_ACCESS is defined, memory accesses are checked for natural
  *	alignment. If access is not naturally aligned, it is performed one byte
  *	at a time.
  *   d) Accesses to main-memory are byte-swapped on a big-endian architecture.
  *	This is done through the endian_xx macro's, that differ according to
  *	the endianness of the architecture.
  *
  * X-1.18	Camiel Vanderhoeven				28-FEB-2007
  *	In the lockstep-versions of the emulator, perform lockstep
  *	synchronisation for every clock tick.
  *
  * X-1.17	Camiel Vanderhoeven				27-FEB-2007
  *	Removed an unreachable "return 0;" line.
  *
  * X-1.16	Camiel Vanderhoeven				18-FEB-2007
  *	Keep track of the cycle-counter in single-step mode (using the
  *	iSSCycles variable.
  *
  * X-1.15	Camiel Vanderhoeven				16-FEB-2007
  *   a) Provide slow and fast clocks for devices. Typical fast-clocked
  *	devices are the CPU(s); most other devices that need a clock should
  *	probably be slow clock devices.
  *   b) DoClock() was replaced with Run(), which runs until one of the
  *	connected devices returns something other than 0; and SingleStep().
  *   c) Corrected some signed/unsigned integer comparison warnings.
  *
  * X-1.14	Brian Wheeler					13-FEB-2007
  *   a) Corrected some typo's in printf statements.
  *   b) Fixed some compiler warnings (assignment inside if()).
  *
  * X-1.13	Camiel Vanderhoeven				12-FEB-2007
  *	Removed error messages when accessing unknown memory.
  *
  * X-1.12       Camiel Vanderhoeven                             12-FEB-2007
  *      Corrected a signed/unsigned integer comparison warning.
  *
  * X-1.11       Camiel Vanderhoeven                             9-FEB-2007
  *      Added comments.
  *
  * X-1.10	Brian Wheeler					7-FEB-2007
  *	Remove FindConfig function, and load configuration file from the
  *	constructor.
  *
  * X-1.9	Camiel Vanderhoeven				7-FEB-2007
  *   a)	CTraceEngine is no longer instantiated as a member of CSystem.
  *   b)	Calls to trace_dev now use the TRC_DEVx macro's.
  *
  * X-1.8	Camiel Vanderhoeven				3-FEB-2007
  *   a) Removed last conditional for supporting another system than an ES40
  *      (#ifdef DS15)
  *   b) FindConfig() now returns the default value rather than crashing
  *	when none of the standard configuration files can be found.
  *
  * X-1.7        Brian Wheeler                                   3-FEB-2007
  *      Formatting.
  *
  * X-1.6	Brian Wheeler					3-FEB-2007
  *	Replaced several 64-bit values in 0x... syntax with X64(...).
  *
  * X-1.5	Brian Wheeler					3-FEB-2007
  *	Added possibility to load a configuration file.
  *
  * X-1.4	Brian Wheeler					3-FEB-2007
  *	Replaced 1i64 with X64(1) in two instances.
  *
  * X-1.3        Brian Wheeler                                   3-FEB-2007
  *      Scanf and printf statements made compatible with Linux/GCC/glibc.
  *
  * X-1.2        Brian Wheeler                                   3-FEB-2007
  *      Includes are now case-correct (necessary on Linux)
  *
  * X-1.1        Camiel Vanderhoeven                             19-JAN-2007
  *      Initial version in CVS.
  **/
#define __STDC_FORMAT_MACROS 1
#include "StdAfx.h"
#include "AliM1543C.h"
#include "AliM1543C_pmu.h"
#include "System.h"
#include "PCIDevice.h"
#include "VGA.h"
#include "AlphaCPU.h"
#include "network/lockstep.h"
#include "DPR.h"
#include "Flash.h"
#include "SnapshotFile.h"
#include "gui/gui.h"

#include <ctype.h>
#include <stdlib.h>
#include <signal.h>
#include <memory>
#include <climits>
#include <new>
#include <stdexcept>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

#define CLOCK_RATIO 10000

#if defined(LS_MASTER) || defined(LS_SLAVE)
char    debug_string[10000] = "";
char* dbg_strptr = debug_string;
#endif

// A stack-local scope per call keeps recursive dispatch and other host threads
// independent without reading the clock for devices that do not need it.
thread_local CSystem::SDeviceAccessScope* CSystem::active_device_access = nullptr;

CSystem::SDeviceAccessScope::SDeviceAccessScope(const CSystem* owner)
  : system(owner), previous(active_device_access)
{
	active_device_access = this;
}

CSystem::SDeviceAccessScope::~SDeviceAccessScope()
{
	active_device_access = previous;
}

std::chrono::steady_clock::time_point CSystem::device_access_time() const
{
	SDeviceAccessScope* scope = active_device_access;
	if (scope && scope->system == this && scope->sampled)
		return scope->sample;
	const auto now = std::chrono::steady_clock::now();
	if (scope && scope->system == this)
	{
		scope->sample = now;
		scope->sampled = true;
	}
	return now;
}

/**
 * Constructor.
 **/
CSystem::CSystem(CConfigurator* cfg)
{
	int i;

	if (theSystem != 0)
		FAILURE(Configuration, "More than one system");
	stop_on_decode_conflict = cfg->get_bool_value("debug.stop_on_decode_conflict", false);
	const std::string read_policy = cfg->get_text_value("debug.shared_read_policy", "stop");
	if (read_policy == "claimant")
	{
		shared_read_policy = SharedReadPolicy::Claimant;
		const char* id = cfg->get_text_value("debug.shared_read_claimant", "");
		if (sscanf(id, "%d:%d", &shared_read_hose, &shared_read_device) != 2)
			FAILURE(Configuration,
				"debug.shared_read_policy=claimant needs debug.shared_read_claimant=<hose>:<device>");
	}
	else if (read_policy == "and")
		shared_read_policy = SharedReadPolicy::And;
	else if (read_policy == "or")
		shared_read_policy = SharedReadPolicy::Or;
	else if (read_policy != "stop")
		FAILURE(Configuration, "debug.shared_read_policy must be stop, claimant, and or or");
	theSystem = this;
	myCfg = cfg;

	iNumComponents = 0;
	iNumMemories = 0;
	iNumCPUs = 0;
	iNumMemoryBits = (int)myCfg->get_num_value("memory.bits", false, 27);

	// 4 Typhoon arrays of at most 8GB (ASIZ 1010) each.
	if (iNumMemoryBits > 35)
		FAILURE(Configuration, "memory.bits > 35 (32GB) exceeds the 4-array Typhoon maximum");

	// initialize SPD data according to configured memory size
	const uint32_t total_mb = static_cast<uint32_t>((1ULL << iNumMemoryBits) >> 20);
	init_spd_from_config_mb(total_mb);

	//  iNumConfig = 0;
#if defined(IDB)
	iSingleStep = 0;
	iSSCycles = 0;
#endif
	for (i = 0; i < 4; i++)
		state.cchip.dim[i] = 0;
	state.cchip.drir = 0;
	state.cchip.misc = U64(0x0000000800000000);
	state.cchip.csc = U64(0x3142444014157803);

	state.dchip.drev = 0x01;
	state.dchip.dsc = 0x43;
	state.dchip.dsc2 = 0x03;
	state.dchip.str = 0x25;

	// initialize pchip data
	for (i = 0; i < 2; i++)
	{
		memset(&state.pchip[i], 0, sizeof(struct SSys_state::SSys_pchip));
		state.pchip[i].wsba[3] = 2;
	}

	state.pchip[0].pctl = U64(0x0000104401440081);
	state.pchip[1].pctl = U64(0x0000504401440081);

	state.tig.FwWrite = 0;
	state.tig.HaltA = 0;
	state.tig.HaltB = 0;
	state.tig.ModInfo = 0;
	memset(state.tig.ipcr, 0, sizeof(state.tig.ipcr));

	if (iNumMemoryBits > 30)
	{

		// size_t may not be big enough, and makes 2^31 negative, so the
		// alloc fails.  We're going to allocate the memory in
		//  2^(iNumMemoryBits-10) chunks of 2^10.
		CHECK_ALLOCATION(memory = calloc(1 << (iNumMemoryBits - 10), 1 << 10));
	}
	else
		CHECK_ALLOCATION(memory = calloc(1 << iNumMemoryBits, 1));

	// LL/SC ABA-guard buckets start at sequence 0, unlocked.
	for (u32 llb = 0; llb < kLLBuckets; llb++)
	{
		m_ll_seq[llb].store(0, std::memory_order_relaxed);
		m_ll_lk[llb].store(0, std::memory_order_relaxed);
	}
	memset(m_ll_seq_snap, 0, sizeof(m_ll_seq_snap));

	printf("%s(%s): $Id$\n",
		cfg->get_myName(), cfg->get_myValue());
}

/**
 * Destructor. Calls the destructors for registered devices, and
 * frees used memory.
 **/
CSystem::~CSystem()
{
	int i;

	printf("Freeing memory in use by system...\n");

	for (i = 0; i < iNumComponents; i++)
		if (acComponents[i])
			delete acComponents[i];

	asMemories.clear();

	free(memory);
}

/**
 * free memory, and allocate and clear new memory.
 **/
void CSystem::ResetMem(unsigned int membits)
{
	free(memory);
	iNumMemoryBits = membits;
	if (iNumMemoryBits > 30) {
		CHECK_ALLOCATION(memory = calloc((size_t)1 << (iNumMemoryBits - 10), 1 << 10));
	}
	else {
		CHECK_ALLOCATION(memory = calloc((size_t)1 << iNumMemoryBits, 1));
	}
}

/**
 * Register a device.
 **/
int CSystem::RegisterComponent(CSystemComponent* component)
{
	if (iNumComponents == INT_MAX)
		FAILURE(Configuration, "Too many system components");
	try
	{
		acComponents.push_back(component);
	}
	catch (const std::bad_alloc&)
	{
		FAILURE(OutOfMemory, "Unable to grow the system component registry");
	}
	catch (const std::length_error&)
	{
		FAILURE(OutOfMemory, "System component registry capacity exhausted");
	}
	iNumComponents++;
	return 0;
}

/**
 * Unregister a device (should only be needed if constructor of device fails)
 **/
void CSystem::UnregisterComponent(CSystemComponent* component)
{
	for (int i = 0; i < iNumComponents; i++)
	{
		if (acComponents[i] == component)
		{
			acComponents[i] = nullptr;
			break;
		}
	}
}

bool CSystem::has_vga_device(const CVGA* exclude) const noexcept
{
	for (const CSystemComponent* component : acComponents)
	{
		const auto* card = dynamic_cast<const CVGA*>(component);
		if (card && card != exclude)
			return true;
	}
	return false;
}

const CVGA* CSystem::get_sole_vga_device() const noexcept
{
	const CVGA* console = nullptr;
	for (const CSystemComponent* component : acComponents)
	{
		const auto* card = dynamic_cast<const CVGA*>(component);
		if (!card)
			continue;
		if (console)
			return nullptr;
		console = card;
	}
	return console;
}

std::vector<CSystem::SDisplayOutput> CSystem::get_display_outputs() const
{
	std::vector<SDisplayOutput> outputs;
	std::unordered_map<const bx_gui_c*, SDisplayOutput> displays;
	for (int i = 0; i < iNumComponents; ++i)
	{
		const CSystemComponent* component = acComponents[i];
		const auto* provider = dynamic_cast<const CDisplayOutputProvider*>(component);
		if (!provider)
			continue;

		std::unordered_set<unsigned> output_ids;
		const std::size_t count = provider->output_count();
		for (std::size_t ordinal = 0; ordinal < count; ++ordinal)
		{
			const CDisplayOutput* output = provider->output_at(ordinal);
			if (!output)
				FAILURE_2(Runtime, "Missing display output %zu for %s", ordinal,
					component->devid_string);
			if (!output_ids.insert(output->id()).second)
				FAILURE_2(Runtime, "Duplicate display output %u for %s", output->id(),
					component->devid_string);
			const SDisplayOutput binding = { component, output };
			const auto inserted = displays.emplace(&output->display(), binding);
			if (!inserted.second)
			{
				const auto& previous = inserted.first->second;
				FAILURE_4(Runtime, "Display outputs %s/%u and %s/%u share a GUI",
					previous.component->devid_string, previous.output->id(),
					component->devid_string, output->id());
			}
			outputs.push_back(binding);
		}
	}
	return outputs;
}

/**
 * Get the number of bits that corresponds to the amount of RAM installed.
 * (e.g. 28 = 256 MB, 29 = 512 MB, 30 = 1 GB)
 **/
unsigned int CSystem::get_memory_bits()
{
	return iNumMemoryBits;
}

/**
 * Obtain a pointer to system memory.
 **/
char* CSystem::PtrToMem(u64 address)
{
	if (address >> iNumMemoryBits) // Non Memory
		return 0;

	return (char*)memory + address;
}

/**
 * Register a device as being a CPU. Return the CPU number.
 **/
int CSystem::RegisterCPU(class CAlphaCPU* cpu)
{
	if (iNumCPUs >= 4)
		return -1;
	acCPUs[iNumCPUs] = cpu;
	iNumCPUs++;
	return iNumCPUs - 1;
}

/**
 * Reserve a range of the 64-bit system address space for a device.
 **/
int CSystem::RegisterMemory(CSystemComponent* component, int index, u64 base,
	u64 length)
{
	std::lock_guard<std::recursive_mutex> bus_lock(device_bus_mutex);
	int                   i;

#if defined(CHECK_MEM_RANGES)
	for (i = 0; i < iNumMemories; i++)
	{
		if (component == asMemories[i]->component)
			continue;

		// check for overlaps
		if (base >= asMemories[i]->base &&
			base <= (asMemories[i]->base + asMemories[i]->length - 1)) {
			printf("WARNING: Start address for %s/%d (%016" PRIx64 "-%016" PRIx64 ")\n"
				"  is within memory range of %s/%d (%016" PRIx64 "-%016" PRIx64 ").\n",
				component->devid_string,
				index, base, base + length - 1,
				asMemories[i]->component->devid_string, asMemories[i]->index,
				asMemories[i]->base, asMemories[i]->base + asMemories[i]->length - 1);
		}

		if (base + length - 1 >= asMemories[i]->base &&
			base + length - 1 <= (asMemories[i]->base + asMemories[i]->length - 1)) {
			printf("WARNING: End address for %s/%d (%016" PRIx64 "-%016" PRIx64 ")\n"
				"  is within memory range of %s/%d (%016" PRIx64 "-%016" PRIx64 ").\n",
				component->devid_string,
				index, base, base + length - 1,
				asMemories[i]->component->devid_string, asMemories[i]->index,
				asMemories[i]->base, asMemories[i]->base + asMemories[i]->length - 1);
		}
	}
#endif //defined(CHECK_MEM_RANGES)

	for (i = 0; i < iNumMemories; i++)
	{
		if ((asMemories[i]->component == component) && (asMemories[i]->index == index))
		{
			asMemories[i]->base = base;
			asMemories[i]->length = length;
			recompute_range_overlaps();
			return 0;
		}
	}
	if (length == 0)
		return 0;

	if (iNumMemories == INT_MAX)
		FAILURE(Configuration, "Too many system memory ranges");
	try
	{
		asMemories.push_back(std::make_unique<SMemoryUser>(
			SMemoryUser{ component, index, base, length }));
	}
	catch (const std::bad_alloc&)
	{
		FAILURE(OutOfMemory, "Unable to grow the system memory-range registry");
	}
	catch (const std::length_error&)
	{
		FAILURE(OutOfMemory, "System memory-range registry capacity exhausted");
	}
	iNumMemories++;
	recompute_range_overlaps();
	return 0;
}

/*
 * AlphaBIOS does not create an EBDA.  Reserve one KiB at the conventional
 * memory boundary before an x86 option ROM reads BAR6.
 */
void CSystem::PrepareX86OptionROMMemory()
{
	static const u64 BDA_EBDA_SEGMENT = U64(0x0000040e);
	static const u64 BDA_MEMORY_SIZE = U64(0x00000413);
	static const u64 BDA_TIMER_TICKS = U64(0x0000046c);
	static const u32 ALPHABIOS_IVT_ENTRY = 0xfdec0008U;
	static const u32 ALPHABIOS_IVT_08 = 0xfdec0020U;
	static const u32 MAXIMUM_CONVENTIONAL_MEMORY_KB = 576U;
	static const u32 MINIMUM_CONVENTIONAL_MEMORY_KB = 128U;
	static const u32 EBDA_BLOCK_SIZE = 1024U;
	static const u32 EBDA_SIZE_KB = 1U;

	if (iNumMemoryBits < 20 ||
		ReadMem(0x0000, 32, nullptr) != ALPHABIOS_IVT_ENTRY ||
		ReadMem(0x0004, 32, nullptr) != ALPHABIOS_IVT_ENTRY ||
		ReadMem(0x0020, 32, nullptr) != ALPHABIOS_IVT_08)
		return;

	u32 conventional_memory_kb =
		(u32)ReadMem(BDA_MEMORY_SIZE, 16, nullptr);
	if (conventional_memory_kb < MINIMUM_CONVENTIONAL_MEMORY_KB ||
		conventional_memory_kb > MAXIMUM_CONVENTIONAL_MEMORY_KB)
		return;

	if (ReadMem(BDA_EBDA_SEGMENT, 16, nullptr) == 0)
	{
		const u64 ebda_address = (u64)conventional_memory_kb * 1024U;
		for (u64 address = ebda_address;
			address < ebda_address + EBDA_BLOCK_SIZE;
			++address)
		{
			if (ReadMem(address, 8, nullptr) != 0)
				return;
		}

		WriteMem(BDA_EBDA_SEGMENT, 16, ebda_address >> 4, nullptr);
		WriteMem(ebda_address, 8, EBDA_SIZE_KB, nullptr);
	}

	if (!m_x86_bios_clock_active.load(std::memory_order_acquire))
	{
		m_x86_bios_clock_initial_ticks =
			(u32)ReadMem(BDA_TIMER_TICKS, 32, nullptr);
		m_x86_bios_clock_elapsed_ticks = 0;
		m_x86_bios_clock_epoch = std::chrono::steady_clock::now();
		m_x86_bios_clock_active.store(true, std::memory_order_release);
	}
}

/* AlphaBIOS's x86 interpreter does not advance the 18.2 Hz BDA clock. */
void CSystem::UpdateX86BIOSClock()
{
	static const u64 BDA_TIMER_TICKS = U64(0x0000046c);
	static const u64 BDA_TIMER_ROLLOVER = U64(0x00000470);
	static const u32 ALPHABIOS_IVT_ENTRY = 0xfdec0008U;
	static const u32 BIOS_TICKS_PER_DAY = 0x001800b0U;

	if (!m_x86_bios_clock_active.load(std::memory_order_acquire))
		return;

	const u64 elapsed_ms = (u64)std::chrono::duration_cast<
		std::chrono::milliseconds>(std::chrono::steady_clock::now() -
			m_x86_bios_clock_epoch).count();
	const u64 elapsed_ticks = elapsed_ms * 182U / 10000U;
	if (elapsed_ticks == m_x86_bios_clock_elapsed_ticks)
		return;

	if (ReadMem(0x0000, 32, nullptr) != ALPHABIOS_IVT_ENTRY ||
		ReadMem(0x0004, 32, nullptr) != ALPHABIOS_IVT_ENTRY)
	{
		m_x86_bios_clock_active.store(false, std::memory_order_release);
		return;
	}

	const u64 previous_ticks = m_x86_bios_clock_initial_ticks +
		m_x86_bios_clock_elapsed_ticks;
	const u64 current_ticks = m_x86_bios_clock_initial_ticks + elapsed_ticks;
	WriteMem(BDA_TIMER_TICKS, 32, current_ticks % BIOS_TICKS_PER_DAY, nullptr);

	const u64 rollovers = current_ticks / BIOS_TICKS_PER_DAY -
		previous_ticks / BIOS_TICKS_PER_DAY;
	if (rollovers != 0)
	{
		const u32 rollover = (u32)ReadMem(BDA_TIMER_ROLLOVER, 8, nullptr);
		WriteMem(BDA_TIMER_ROLLOVER, 8, rollover + rollovers, nullptr);
	}
	m_x86_bios_clock_elapsed_ticks = elapsed_ticks;
}

int got_sigint = 0;

/**
 * Handle a SIGINT (CTRL-C) or SIGTERM by setting a flag that terminates the emulator.
 **/
void sigint_handler(int signum)
{
	got_sigint = 1;
}

/**
 * Run the system by clocking the CPU(s) and devices.
 **/
void CSystem::Run()
{
	int i;

	int k;


#if defined(DUMP_MEMMAP)
	printf("ES40 Memory Map\n");
	printf("Physical Address Size     Device/Index\n");
	printf("---------------- -------- -------------------------\n");
	for (i = 0; i < iNumMemories; i++) {
		printf("%016" PRIx64 " %8x %s/%d\n", asMemories[i]->base, asMemories[i]->length, asMemories[i]->component->devid_string, asMemories[i]->index);
	}
#endif //defined(DUMP_MEMMAP)




	/* catch CTRL-C and SIGTERM and shutdown gracefully */
	signal(SIGINT, &sigint_handler);
	signal(SIGTERM, &sigint_handler);

	CheckForShutdown();
	start_threads();

	for (k = 0;; k++)
	{
		if (got_sigint)
			FAILURE(Graceful, "CTRL-C or SIGTERM detected");

		CheckForShutdown();
		if (ProcessPendingReset())
			continue;


		CThread::sleep(100); // 100ms sleep
		CheckForShutdown();
		UpdateX86BIOSClock();
		for (i = 0; i < iNumComponents; i++)
			acComponents[i]->check_state();
#if !defined(HIDE_COUNTER)
#if defined(PROFILE)
		printf("%d | %016" PRIx64 " | %" PRId64 " profiled instructions.  \r", k,
			acCPUs[0]->get_pc(), profiled_insts);
#else //defined(PROFILE)
		printf("%d | %016" PRIx64 "\r", k, acCPUs[0]->get_pc());
#endif //defined(PROFILE)
#endif //defined(HIDE_COUNTER)
	}

	//  printf ("%%SYS-W-SHUTDOWN: CTRL-C or Device Failed\n");
	//  return 1;
}

void CSystem::RequestShutdown() noexcept
{
	m_shutdown_requested.store(true, std::memory_order_release);
}

bool CSystem::IsShutdownRequested() const noexcept
{
	return m_shutdown_requested.load(std::memory_order_acquire);
}

void CSystem::CheckForShutdown() const
{
	if (IsShutdownRequested())
		FAILURE(Graceful, "User requested shutdown");
}

// --- System reset support (firmware and host UI) ---------------------------

void CSystem::RequestSystemReset()
{
	m_reset_requested.store(true, std::memory_order_release);
}

bool CSystem::IsSystemResetRequested() const
{
	return m_reset_requested.load(std::memory_order_acquire);
}

bool CSystem::ProcessPendingReset()
{
	CheckForShutdown();
	if (!m_reset_requested.exchange(false, std::memory_order_acq_rel))
		return false;

	struct ResetInProgressGuard
	{
		CSystem* sys;
		explicit ResetInProgressGuard(CSystem* s) : sys(s) { sys->SetResetInProgress(true); }
		~ResetInProgressGuard() { sys->SetResetInProgress(false); }
	};

	printf("\n%%SYS-I-RESET: System reset requested.\n");
	if (theSROM)
		theSROM->FlushIfDirty();

	ResetInProgressGuard rip(this);
	stop_threads();
	{
		std::lock_guard<std::recursive_mutex> bus_lock(device_bus_mutex);
		ResetChipsetState();
		for (int dev = 0; dev < iNumComponents; dev++)
			acComponents[dev]->ResetPCI();
	}
	for (int cpu = 0; cpu < iNumCPUs; cpu++)
		acCPUs[cpu]->ResetForSystemReset();
	LoadROM();
	start_threads();
	return true;
}

void CSystem::ResetChipsetState()
{
	// Re-establish the same power-on defaults used in the constructor.
	m_x86_bios_clock_active.store(false, std::memory_order_release);
	state.cpu_lock_flags = 0;
	memset(state.cpu_lock_address, 0, sizeof(state.cpu_lock_address));
	memset(cpu_lock_value, 0, sizeof(cpu_lock_value));

	for (int i = 0; i < 4; i++)
		state.cchip.dim[i] = 0;
	state.cchip.drir = 0;
	state.cchip.misc = U64(0x0000000800000000);
	state.cchip.csc = U64(0x3142444014157803);

	state.dchip.drev = 0x01;
	state.dchip.dsc = 0x43;
	state.dchip.dsc2 = 0x03;
	state.dchip.str = 0x25;

	for (int i = 0; i < 2; i++)
	{
		memset(&state.pchip[i], 0, sizeof(struct SSys_state::SSys_pchip));
		state.pchip[i].wsba[3] = 2;
	}

	state.pchip[0].pctl = U64(0x0000104401440081);
	state.pchip[1].pctl = U64(0x0000504401440081);

	state.tig.FwWrite = 0;
	state.tig.HaltA = 0;
	state.tig.HaltB = 0;
	state.tig.ModInfo = 0;
	memset(state.tig.ipcr, 0, sizeof(state.tig.ipcr));

}

/**
 * Do one clock tick. The cpu(s) will execute one single instruction, and
 * some devices may be clocked.
 **/
int CSystem::SingleStep()
{
	CheckForShutdown();
	int i;
	int result;

	for (i = 0; i < iNumCPUs; i++)
		if (!acCPUs[i]->get_waiting())
			acCPUs[i]->execute();

	//  iSingleStep++;
#if defined(LS_MASTER) || defined(LS_SLAVE)
	if (!(iSingleStep % 50))
	{
		lockstep_sync_m2s("sync1");
		*dbg_strptr = '\0';
		lockstep_compare(debug_string);
		dbg_strptr = debug_string;
		*dbg_strptr = '\0';
	}
#endif //defined(LS_MASTER) || defined(LS_SLAVE)

	//  if (iSingleStep >= CLOCK_RATIO)
	//  {
	//     iSingleStep = 0;
	//     for(i=0;i<iNumSlowClocks;i++)
	//     {
	//        result = acSlowClocks[i]->DoClock();
	//      if (result)
	//        return result;
	//     }
	//#ifdef IDB
	//     iSSCycles++;
	//#if !defined(LS_SLAVE)
	//     if (bHashing)
	//#endif
	//       printf("%d | %016" PRIx64 "\r",iSSCycles,acCPUs[0]->get_pc());
	//#endif
	//  }
	for (i = 0; i < iNumComponents; i++)
		acComponents[i]->check_state();

	return 0;
}

#if defined(DEBUG_PORTACCESS)
u64 lastport;
#endif //defined(DEBUG_PORTACCESS)

// EV6/EV68 Dcache blocks are 64 bytes; LDx_L/STx_C monitor that cache line.
#define CPU_LOCK_MATCH_MASK U64(0x00000807ffffffc0)
#define CPU_LOCK_IO_MASK    U64(0x0000080000000000)

static inline bool cpu_lock_matches(u64 locked_address, u64 address)
{
	return !((locked_address ^ address) & CPU_LOCK_MATCH_MASK);
}

// --- Load-locked / store-conditional (HRM 4.2) -----------------------------
// Keep the original CAS-backed model for same-address LL/SC sequences, because
// it provides the emulator's MP atomicity. Some Alpha code stores conditionally
// to a different quadword in the same locked cache line; those must not compare
// against the value loaded from the LDx_L address.

void CSystem::cpu_lock(int cpuid, u64 address, u64 value)
{
	state.cpu_lock_address[cpuid] = address;
	cpu_lock_value[cpuid] = value;
	// ABA guard - snapshot the line's STx_C sequence. 
	m_ll_seq_snap[cpuid] =
		m_ll_seq[(u32)((address >> 6) & (kLLBuckets - 1))].load(std::memory_order_acquire);
	state.cpu_lock_flags |= (1 << cpuid);   // atomic fetch_or
}

bool CSystem::cpu_take_lock(int cpuid, u64 address, u64* expected, bool* same_address)
{
	// I/O-space conditional stores have no cache line to watch; treat as held.
	bool held = (address & CPU_LOCK_IO_MASK) ||
	            ((state.cpu_lock_flags.load() & (1 << cpuid)) &&
	             cpu_lock_matches(state.cpu_lock_address[cpuid], address));

	// STx_C always consumes this CPU's lock, success or fail.
	state.cpu_lock_flags &= ~(1 << cpuid);  // atomic fetch_and
	if (held)
	{
		*expected = cpu_lock_value[cpuid];
		*same_address = (state.cpu_lock_address[cpuid] == address);
	}
	return held;
}

/**
 * STx_C: consume the lock and perform the conditional store. 
 **/
u64 CSystem::cpu_stx_c(int cpuid, u64 phys, int size_bits, u64 value,
	char* dram, u64 dram_sz, CSystemComponent* source)
{
	u64  expected = 0;
	bool same_address = false;
	if (!cpu_take_lock(cpuid, phys, &expected, &same_address))
		return 0;

	if (phys >= dram_sz)
	{
		WriteMem(phys, size_bits, value, source);   // I/O-space conditional store
		return 1;
	}

	const u32 b = (u32)((phys >> 6) & (kLLBuckets - 1));
	while (m_ll_lk[b].exchange(1, std::memory_order_acquire))
		;
	u64 ok;
	if (m_ll_seq[b].load(std::memory_order_relaxed) != m_ll_seq_snap[cpuid])
		ok = 0;    // another STx_C hit this line since our LDx_L
	else if (same_address)
		ok = dram_cas(dram, phys, expected, value, size_bits) ? 1 : 0;
	else
	{
		// STx_C to a different quadword in the locked line: no value to compare.
		dram_write(dram, phys, size_bits, value);
		ok = 1;
	}
	if (ok)
		m_ll_seq[b].fetch_add(1, std::memory_order_relaxed);
	m_ll_lk[b].store(0, std::memory_order_release);
	return ok;
}

/**
 * Drop one CPU's load lock. Called when that CPU takes an exception or interrupt
 * (HRM 4.2.4: a pending STx_C must fail if an exception/interrupt intervened).
 **/
void CSystem::cpu_clear_lock(int cpuid)
{
	state.cpu_lock_flags &= ~(1 << cpuid);  // atomic fetch_and
}

// Geometry only; called under device_bus_mutex whenever a range or owner link
// changes. A bridge forwarding range always yields to its own bridge's
// handlers, so only the forwarding range itself is flagged for that overlap.
void CSystem::recompute_range_overlaps()
{
	auto yields_to = [](const SMemoryUser* specific, const SMemoryUser* fallback) {
		return fallback->component->memory_decode_fallback(fallback->index) &&
			fallback->component->memory_decode_owner() ==
			specific->component->memory_decode_owner();
	};
	for (int i = 0; i < iNumMemories; ++i)
		asMemories[i]->may_overlap = false;
	for (int i = 0; i < iNumMemories; ++i)
	{
		SMemoryUser* a = asMemories[i].get();
		if (!a->length)
			continue;
		for (int j = i + 1; j < iNumMemories; ++j)
		{
			SMemoryUser* b = asMemories[j].get();
			if (!b->length || a->base >= b->base + b->length || b->base >= a->base + a->length)
				continue;
			if (!yields_to(a, b))
				a->may_overlap = true;
			if (!yields_to(b, a))
				b->may_overlap = true;
		}
	}
}

// Called with device_bus_mutex held, before any handler runs. 
// Positive decode precedes subtractive decode. 
// A bridge forwarding claim yields to that bridge's specific handler. 
// Distinct remaining components are claimants.
void CSystem::collect_decode_claims(u64 address, int dsize, bool write,
	SDecodeClaims& claims, const CSystemComponent* source) const
{
	claims.count = 0;
	claims.subtractive = false;
	auto eligible = [&](const SMemoryUser* r) {
		return address >= r->base && address < r->base + r->length &&
			r->component->decodes_memory_access(r->index, address - r->base, dsize, write);
	};

	int first = 0;
	while (first < iNumMemories && !eligible(asMemories[first].get()))
		++first;
	if (first == iNumMemories)
		return;
	if (!asMemories[first]->may_overlap)
	{
		claims.range[claims.count++] = *asMemories[first];
		return;
	}

	int positive[SDecodeClaims::kMax * 4];
	int subtractive[SDecodeClaims::kMax * 4];
	int npos = 0, nsub = 0;
	for (int i = first; i < iNumMemories; ++i)
	{
		const SMemoryUser* r = asMemories[i].get();
		if (!eligible(r))
			continue;
		const bool sub = r->component->uses_subtractive_decode(r->index,
			address - r->base, dsize, write);
		int* list = sub ? subtractive : positive;
		int& n = sub ? nsub : npos;
		if (n == SDecodeClaims::kMax * 4)
		{
			SDecodeClaims partial;
			partial.subtractive = sub;
			for (int k = 0; k < SDecodeClaims::kMax; ++k)
				partial.range[partial.count++] = *asMemories[list[k]];
			report_decode_overlap(address, dsize, write, partial, source,
				"too many eligible ranges to resolve");
		}
		list[n++] = i;
	}
	const int* list = npos ? positive : subtractive;
	const int n = npos ? npos : nsub;
	claims.subtractive = npos == 0;

	for (int k = 0; k < n; ++k)
	{
		const SMemoryUser* r = asMemories[list[k]].get();
		if (r->component->memory_decode_fallback(r->index))
		{
			bool yields = false;
			for (int m = 0; m < n && !yields; ++m)
			{
				const SMemoryUser* o = asMemories[list[m]].get();
				yields = o->component->memory_decode_owner() == r->component->memory_decode_owner() &&
					!o->component->memory_decode_fallback(o->index);
			}
			if (yields)
				continue;
		}
		bool seen = false;
		for (int m = 0; m < claims.count && !seen; ++m)
			seen = claims.range[m].component == r->component;
		if (seen)
			continue; // same-component alias
		if (claims.count == SDecodeClaims::kMax)
			report_decode_overlap(address, dsize, write, claims, source,
				"too many claimants to resolve");
		claims.range[claims.count++] = *r;
	}
}

std::string CSystem::describe_claimant(const SMemoryUser& range, u64 address) const
{
	const SMemoryUser* r = &range;
	std::ostringstream s;
	s << r->component->devid_string;
	if (const auto* pci = dynamic_cast<const CPCIDevice*>(r->component))
	{
		s << " PCI " << std::dec << pci->pci_bus() << ':' << pci->pci_dev();
		if (r->index >= PCI_RANGE_BASE && r->index < PCI_RANGE_BASE + 64)
			s << '.' << (r->index - PCI_RANGE_BASE) / 8;
	}
	s << " range " << std::dec << r->index << " base 0x" << std::hex << r->base
		<< " length 0x" << r->length;
	const std::string context = r->component->describe_access_context(r->index,
		address - r->base);
	if (!context.empty())
		s << " [" << context << ']';
	return s.str();
}

static std::string describe_source(const CSystemComponent* source)
{
	std::ostringstream s;
	s << (source ? source->devid_string : "<none>");
	if (auto* cpu = dynamic_cast<const CAlphaCPU*>(source))
		s << " pc 0x" << std::hex << const_cast<CAlphaCPU*>(cpu)->get_clean_pc();
	return s.str();
}

void CSystem::print_pio_trace() const
{
	printf("Recent mapped PIO accesses (oldest first):\n");
	for (unsigned k = 0; k < kPioTraceSize; ++k)
	{
		const SPioTrace& t = pio_trace[(pio_trace_next + k) % kPioTraceSize];
		if (!t.dsize)
			continue;
		printf("  %c%-2d 0x%011" PRIx64 " data 0x%" PRIx64 " claimants %d\n",
			t.write ? 'W' : 'R', t.dsize, t.address, t.data, t.claims);
	}
}

void CSystem::note_pio_access(u64 address, int dsize, bool write, u64 data,
	int claims)
{
	pio_trace[pio_trace_next] = { address, data, dsize, claims, write };
	pio_trace_next = (pio_trace_next + 1) % kPioTraceSize;
}

void CSystem::report_decode_overlap(u64 address, int dsize, bool write,
	const SDecodeClaims& claims, const CSystemComponent* source,
	const char* reason, const std::string& detail) const
{
	print_pio_trace();
	std::ostringstream message;
	message << "Unresolved PCI decode overlap (" << reason << ") for "
		<< (write ? "write " : "read ") << std::dec << dsize << "-bit at 0x"
		<< std::hex << address << ", source " << describe_source(source)
		<< ", " << (claims.subtractive ? "subtractive" : "positive") << " decode";
	for (int k = 0; k < claims.count; ++k)
		message << "\n  " << describe_claimant(claims.range[k], address);
	if (!detail.empty())
		message << "\n  " << detail;
	// An emulator stop, not a PCI error or target arbitration rule.
	FAILURE(Runtime, message.str());
}

// Logs at occurrences 1, 2, 4, 8, ... of each distinct event.
void CSystem::log_shared_event(const std::string& key, const std::string& line)
{
	const u64 n = ++shared_event_counts[key];
	if ((n & (n - 1)) == 0)
		printf("%%PCI-I-SHARED: %s (occurrence %" PRIu64 ")\n", line.c_str(), n);
}

// Synchronous functional model for shared PCI I/O and memory: writes reach
// every claimant, reads must agree. This does not model electrical timing.
void CSystem::prepare_shared_access(u64 address, int dsize, bool write,
	const SDecodeClaims& claims, const CSystemComponent* source)
{
	if (stop_on_decode_conflict)
		report_decode_overlap(address, dsize, write, claims, source,
			"debug.stop_on_decode_conflict");
	if (claims.subtractive)
		report_decode_overlap(address, dsize, write, claims, source,
			"several subtractive responders");
	const bool valid_width = dsize == 8 || dsize == 16 || dsize == 32 || dsize == 64;
	auto within = [&](u64 base, u64 length) {
		return valid_width && address >= base && address < base + length &&
			(u64)(dsize / 8) <= base + length - address;
	};
	if (!within(U64(0x80000000000), U64(0x100000000)) &&
		!within(U64(0x80200000000), U64(0x100000000)) &&
		!within(U64(0x801fc000000), U64(0x2000000)) &&
		!within(U64(0x803fc000000), U64(0x2000000)))
		report_decode_overlap(address, dsize, write, claims, source,
			"shared completion is only modeled for PCI I/O and memory");

	std::ostringstream key, line;
	key << (write ? 'W' : 'R') << dsize << ':' << std::hex << address;
	for (int k = 0; k < claims.count; ++k)
		key << ':' << claims.range[k].component->devid_string;
	line << (write ? "write " : "read ") << std::dec << dsize << "-bit 0x"
		<< std::hex << address << " claimed by " << std::dec << claims.count
		<< " claimants";
	for (int k = 0; k < claims.count; ++k)
		line << (k ? ", " : " ") << claims.range[k].component->devid_string;
	log_shared_event(key.str(), line.str());
}

// Every claimant performs the read before one value is chosen. 
// Disagreement is undefined contention on real hardware.
u64 CSystem::resolve_shared_read(u64 address, int dsize,
	const SDecodeClaims& claims, const CSystemComponent* source)
{
	const u64 mask = dsize == 64 ? ~U64(0) : (U64(1) << dsize) - 1;
	u64 values[SDecodeClaims::kMax];
	std::string contexts[SDecodeClaims::kMax];
	bool agree = true;
	for (int k = 0; k < claims.count; ++k)
	{
		const SMemoryUser* r = &claims.range[k];
		// Capture register context before the read's own side effects.
		contexts[k] = r->component->describe_access_context(r->index,
			address - r->base);
		values[k] = r->component->ReadMem(r->index, address - r->base, dsize) & mask;
		agree &= values[k] == values[0];
	}
	if (agree)
		return values[0];

	u64 anded = mask, ored = 0, differ = 0;
	for (int k = 0; k < claims.count; ++k)
	{
		anded &= values[k];
		ored |= values[k];
		differ |= values[k] ^ values[0];
	}
	std::ostringstream key, detail;
	key << std::hex << address << ':' << dsize << ':' << differ;
	detail << "claimant values differ, mask 0x" << std::hex << differ
		<< ", policy ";
	switch (shared_read_policy)
	{
	case SharedReadPolicy::Stop: detail << "stop"; break;
	case SharedReadPolicy::Claimant: detail << "claimant " << std::dec
		<< shared_read_hose << ':' << shared_read_device; break;
	case SharedReadPolicy::And: detail << "and"; break;
	case SharedReadPolicy::Or: detail << "or"; break;
	}
	for (int k = 0; k < claims.count; ++k)
	{
		detail << "\n    " << claims.range[k].component->devid_string
			<< " returned 0x" << std::hex << values[k];
		if (!contexts[k].empty())
			detail << " [" << contexts[k] << ']';
		key << ':' << contexts[k];
	}
	if (shared_read_policy == SharedReadPolicy::Stop)
		report_decode_overlap(address, dsize, false, claims, source,
			"claimants returned different read data", detail.str());

	std::ostringstream line;
	line << "read " << std::dec << dsize << "-bit 0x" << std::hex << address
		<< " from " << describe_source(source) << ": " << detail.str();
	log_shared_event("contention:" + key.str(), line.str());

	switch (shared_read_policy)
	{
	case SharedReadPolicy::And: return anded;
	case SharedReadPolicy::Or: return ored;
	default: break;
	}
	for (int k = 0; k < claims.count; ++k)
	{
		const auto* pci = dynamic_cast<const CPCIDevice*>(
			claims.range[k].component);
		if (pci && pci->pci_bus() == shared_read_hose &&
			pci->pci_dev() == shared_read_device)
			return values[k];
	}
	report_decode_overlap(address, dsize, false, claims, source,
		"debug.shared_read_claimant is not among the claimants", detail.str());
}

void CSystem::dispatch_pci_io_write(u64 address, int dsize, u64 data,
	const SDecodeClaims& claims)
{
	const CSystemComponent::PciIoWrite write = {
		address >= U64(0x803fc000000) ? 1 : 0,
		(u32)(address & U64(0x1ffffff)), dsize, data
	};
	struct Observation { CSystemComponent* component; u64 captured; };
	std::vector<Observation> observers;

	for (CSystemComponent* component : acComponents)
	{
		if (!component)
			continue;
		bool skip = false;
		for (int k = 0; k < claims.count && !skip; ++k)
			skip = component->memory_decode_owner() ==
				claims.range[k].component->memory_decode_owner();
		for (const auto& observer : observers)
			skip |= observer.component == component;
		if (skip)
			continue;
		const u64 captured = component->capture_pci_io_write(write);
		if (captured)
			observers.push_back({ component, captured });
	}

	for (int k = 0; k < claims.count; ++k)
	{
		const SMemoryUser* r = &claims.range[k];
		r->component->WriteMem(r->index, address - r->base, dsize, data);
	}
	const auto dispatch = claims.count
		? CSystemComponent::PciIoWriteDispatch::MappedTargetReturned
		: CSystemComponent::PciIoWriteDispatch::NoMappedTarget;
	for (const auto& observer : observers)
		observer.component->observe_pci_io_write(write, observer.captured, dispatch);
}

/**
 * Write 8, 16, 32 or 64 bits to a system address: main memory here, PIO
 * space via pio_write() (System_tsunami.cpp).
 **/
void CSystem::WriteMem(u64 address, int dsize, u64 data, CSystemComponent* source)
{
	u64   a;
	u8* p;
#if defined(ALIGN_MEM_ACCESS)
	u64   t64;
	u32   t32;
	u16   t16;
#endif //defined(ALIGN_MEM_ACCESS)
	a = address & U64(0x00000807ffffffff);

	if (a >> iNumMemoryBits) // non-memory: chipset PIO decode
	{
		pio_write(a, dsize, data, source);
		return;
	}

	p = (u8*)memory + a;

	switch (dsize)
	{
	case 8:   *((u8*)p) = (u8)data; break;
	case 16:  *((u16*)p) = endian_16((u16)data); break;
	case 32:  *((u32*)p) = endian_32((u32)data); break;
	default:  *((u64*)p) = endian_64((u64)data);
	}
}

/**
 * Read 8, 16, 32 or 64 bits from a system address: main memory here, PIO
 * space via pio_read() (System_tsunami.cpp).
 **/
u64 CSystem::ReadMem(u64 address, int dsize, CSystemComponent* source)
{
	u64   a;
	u8* p;

	a = address & U64(0x00000807ffffffff);
	if (a >> iNumMemoryBits) // non-memory: chipset PIO decode
		return pio_read(a, dsize, source);

	p = (u8*)memory + a;

	switch (dsize)
	{
	case 8:   return *((u8*)p);
	case 16:  return endian_16(*((u16*)p));
	case 32:  return endian_32(*((u32*)p));
	default:  return endian_64(*((u64*)p));
	}
}

/**
 * Run one progress chunk of the SRM self-decompressor on CPU 0. Returns true
 * once the decompressor has jumped below 0x200000 (into the inflated console).
 **/
static bool srm_decomp_chunk(CSystem* sys, CAlphaCPU* cpu)
{
#ifdef ES40_JIT
	(void)sys;
	for (int i = 0; i < 90000; i++)
	{
		cpu->jit_step(2000);
		if (cpu->get_clean_pc() < U64(0x200000))
			return true;
	}
#else
	for (int i = 0; i < 1800000; i++)
	{
		sys->SingleStep();
		if (cpu->get_clean_pc() < U64(0x200000))
			return true;
	}
#endif
	return false;
}

/**
 * Load ROM contents from file. Decompress cl67srmrom.exe (or the boot
 * firmware partition from flash) into low memory.
 **/
int CSystem::LoadROM()
{
	FILE* f;
	char* buffer;
	int     i;
	int     j;
	u32     scratch;
	bool loadedFromFlash = false;

	// If flash.rom contains a partitioned ES40 image (CPQ header at the SRM
	// partition), execute its embedded self-decompressor to inflate the console
	// into low RAM just like the cl67srmrom.exe path would.
	if (theSROM && theSROM->HasBootFirmware())
	{
		printf("%%SYS-I-READFLASH: Reading boot ROM image from %s.\n",
			myCfg->get_text_value("rom.flash", "flash.rom"));

		const u8* flash = theSROM->GetFlashBytes();
		const u32 srm_off = 0x00010000;
		const u32 srm_len = 0x000E0000;

		printf("%%SYS-I-DECOMP: Decompressing SRM image from flash.\n0%%");
		fflush(stdout);

		// The SRM partition is wrapped in a 0x40-byte CPQ header. The
		// self-decompressing payload is not position independent and expects
		// to be loaded exactly like the cl67srmrom.exe path: payload at
		// 0x900000, PC=0x900001, PAL_BASE=0x900000.
		const u64 load_base = U64(0x0000000000900000);
		const u32 cpq_hdr_len = 0x40;

		memcpy(PtrToMem(load_base), flash + srm_off + cpq_hdr_len, srm_len - cpq_hdr_len);

		acCPUs[0]->set_pc(load_base | 1);
		acCPUs[0]->set_PAL_BASE(load_base);
		acCPUs[0]->enable_icache();

		bool decomp_ok = true;
		j = 0;
		while (acCPUs[0]->get_clean_pc() > U64(0x200000))
		{
			srm_decomp_chunk(this, acCPUs[0]);
			j++;
			if (j < 50)
			{
				printf("%d%%", j * 2);
				fflush(stdout);
			}
			else
			{
				printf(".");
				fflush(stdout);
			}
			if (j > 500)
			{
				printf("\n%%SYS-F-DECOMPFAIL: SRM decompressor did not return to low memory.\n");
				decomp_ok = false;
				break;
			}
		}
		printf("100%%\n");

		acCPUs[0]->restore_icache();

		if (decomp_ok)
		{
			for (i = 0; i < iNumCPUs; i++)
				acCPUs[i]->set_pc(acCPUs[0]->get_pc());
			for (i = 0; i < iNumCPUs; i++)
				acCPUs[i]->set_PAL_BASE(acCPUs[0]->get_pal_base());

			loadedFromFlash = true;
		}
	}

	if (!loadedFromFlash)
	{
		f = fopen(myCfg->get_text_value("rom.srm", "cl67srmrom.exe"), "rb");
		if (!f)
			FAILURE(Runtime, "No SRM ROM image found");
		printf("%%SYS-I-READROM: Reading original ROM image from %s.\n",
			myCfg->get_text_value("rom.srm", "cl67srmrom.exe"));
		for (i = 0; i < 0x240; i++)
		{
			if (feof(f))
				break;
			fread(&scratch, 1, 1, f);
		}

		if (feof(f))
			FAILURE(Runtime, "File is too short to be a SRM ROM image");
		buffer = PtrToMem(0x900000);
		while (!feof(f))
			fread(buffer++, 1, 1, f);
		fclose(f);

		printf("%%SYS-I-DECOMP: Decompressing ROM image.\n0%%");
		acCPUs[0]->set_pc(0x900001);
		acCPUs[0]->set_PAL_BASE(0x900000);
		acCPUs[0]->enable_icache();

		j = 0;
		while (acCPUs[0]->get_clean_pc() > 0x200000)
		{
			srm_decomp_chunk(this, acCPUs[0]);
			j++;
			if (((j % 5) == 0) && (j < 50))
				printf("%d%%", j * 2);
			else
				printf(".");
			fflush(stdout);
		}

		printf("100%%\n");
		acCPUs[0]->restore_icache();
	}

#if !defined(SRM_NO_SPEEDUPS) || !defined(SRM_NO_IDE)
	printf("%%SYM-I-PATCHROM: Patching ROM for speed.\n");
#endif
#if !defined(SRM_NO_SPEEDUPS)
	WriteMem(U64(0x14248), 32, 0xe7e00000, 0);  // e7e00000 = BEQ r31, +0
	WriteMem(U64(0x14288), 32, 0xe7e00000, 0);
	WriteMem(U64(0x142c8), 32, 0xe7e00000, 0);
	WriteMem(U64(0x68320), 32, 0xe7e00000, 0);
	WriteMem(U64(0x8bb78), 32, 0xe7e00000, 0);  // memory test (aa)
	WriteMem(U64(0x8bc0c), 32, 0xe7e00000, 0);  // memory test (bb)
	WriteMem(U64(0x8bc94), 32, 0xe7e00000, 0);  // memory test (00)

	//WriteMem(U64(0xb1158),32,0xe7e00000,0);   // CPU sync?
#endif
#ifdef ES40_JIT
	// The chunked jit_step drive overshoots the decompressor's exit by up to a
	// dispatch batch and may have compiled blocks over the patch sites above;
	// drop them so the patched bytes take effect.
	acCPUs[0]->flush_icache();
#endif
	printf("%%SYS-I-ROMLOADED: ROM Image loaded successfully!\n");
	return 0;
}

/**
 * Initialize all devices.
 **/
void CSystem::bind_isa_devices()
{
	CAliM1543C* bridge = nullptr;
	for (auto* component : acComponents)
		if (auto* candidate = dynamic_cast<CAliM1543C*>(component))
		{
			if (bridge && bridge != candidate)
				FAILURE(Configuration, "Multiple ISA bridges in one system");
			bridge = candidate;
		}
	CAliM1543C_pmu* pmu = nullptr;
	if (bridge)
		for (auto* component : acComponents)
			if (auto* candidate = dynamic_cast<CAliM1543C_pmu*>(component))
				if (candidate->pci_bus() == bridge->pci_bus())
				{
					if (pmu && pmu != candidate)
						FAILURE(Configuration, "Multiple PMU functions for one ISA bridge");
					pmu = candidate;
				}
	if (bridge)
		bridge->bind_pmu(pmu);
	for (auto* component : acComponents)
		if (component)
		{
			const auto* pci = dynamic_cast<const CPCIDevice*>(component);
			// Non-PCI endpoints currently have fixed hose-0 I/O registrations.
			component->bind_isa_bridge(bridge && (pci ?
				pci->pci_bus() == bridge->pci_bus() : bridge->pci_bus() == 0)
				? bridge : nullptr);
		}
}

void CSystem::init()
{
	SDeviceAccessScope time_scope(this);
	bind_isa_devices();
	{
		// Owner links are bound above, after the ranges were registered.
		std::lock_guard<std::recursive_mutex> bus_lock(device_bus_mutex);
		recompute_range_overlaps();
	}
	if (!bNativePal)
		printf("%%SYS-I-VMSPAL: running the optimized vmspal PALcode replacement routines on all CPUs (set palcode.vms.nohle=true for native PALcode).\n");
	for (int i = 0; i < iNumComponents; i++)
		if (acComponents[i])
			acComponents[i]->init();
	// Validate completed output bindings before any display workers start.
	for (const auto& binding : get_display_outputs())
	{
		if (!bx_gui || bx_gui->find_display_for_output(
			binding.component->get_device_path(), binding.output->id()) !=
			&binding.output->display())
			FAILURE_2(Configuration, "Display output %s/%u does not match its GUI binding",
				binding.component->devid_string, binding.output->id());
	}
}

void CSystem::start_threads()
{
	SDeviceAccessScope time_scope(this);
	int i;

	printf("Start threads:");
	for (i = 0; i < iNumComponents; i++) { // includes fix for IDB graphical window from axpbox commit 9ef3473
		if (!acComponents[i])
			continue;
#ifdef IDB
		// When running with IDB, the trace engine takes care of managing the CPU,
		// so its thread shouldn't be started.
		if (dynamic_cast<CAlphaCPU*>(acComponents[i]))
			continue;
#endif
		acComponents[i]->start_threads();
	}
	printf("\n");

	for (i = 0; i < iNumCPUs; i++)
		acCPUs[i]->release_threads();
}

void CSystem::stop_threads()
{
	printf("Stop threads:");
	// Quiesce guest PIO before freezing device clocks, regardless of config
	// order. Never hold the device-bus mutex while joining worker threads.
	for (auto* component : acComponents)
		if (auto* cpu = dynamic_cast<CAlphaCPU*>(component))
			cpu->stop_threads();
	SDeviceAccessScope time_scope(this);
	for (auto* component : acComponents)
		if (component && !dynamic_cast<CAlphaCPU*>(component))
			component->stop_threads();
	printf("\n");
}

// 2.2 includes complete S3 graphics and NIC packet state. 
// Reject 2.1 before changing RAM.
// 2.3 adds a device/media manifest, audio, RAM-disk and flash command state.
// 2.4 requires the hardware-controlled S3 PCI ROM BAR; no fixed ROM alias.
// 2.5 stores the canonical Trio64 setup controls and enforces their access gates.
// 2.6 requires the documented S3 PCI COMMAND mask and fixed STATUS value.
static const u32 system_state_magic = 0xa1fae540;
// 2.7 requires writable M7101 docking selectors and removes the CF8/CFC latch.
// 2.8 preserves the Trio64 scan-counter phase.
static const u32 system_state_version = 0x00020008;
static const u32 snapshot_identity_limit = 65536;

void CSystem::flush_storage()
{
	std::lock_guard<std::recursive_mutex> bus_lock(device_bus_mutex);
	for (int i = 0; i < iNumComponents; ++i)
		acComponents[i]->flush_storage();
}

/**
 * Save system state to a state file. Callers must first stop device threads.
 **/
void CSystem::SaveState(const char* fn)
{
	std::lock_guard<std::recursive_mutex> bus_lock(device_bus_mutex);
	SDeviceAccessScope time_scope(this);
	std::vector<std::string> identities;
	for (int i = 0; i < iNumComponents; ++i)
		acComponents[i]->prepare_snapshot();
	for (int i = 0; i < iNumComponents; ++i)
	{
		identities.push_back(acComponents[i]->snapshot_identity());
		if (identities.back().empty() || identities.back().size() > snapshot_identity_limit)
			FAILURE(Runtime, "Invalid snapshot device identity");
	}
	CSnapshotFile file(fn);
	FILE* f = file.get();
	const auto write = [f](const void* data, size_t size)
	{
		if (fwrite(data, size, 1, f) != 1)
			FAILURE(Runtime, "Unable to write system state");
	};
	const u64 memory_size = U64(1) << iNumMemoryBits;
	const u32 system_size = (u32)sizeof(state);
	const u32 component_count = (u32)iNumComponents;
	write(&system_state_magic, sizeof(system_state_magic));
	write(&system_state_version, sizeof(system_state_version));
	write(&memory_size, sizeof(memory_size));
	write(&system_size, sizeof(system_size));
	write(&component_count, sizeof(component_count));
	for (const auto& identity : identities)
	{
		const u32 length = (u32)identity.size();
		write(&length, sizeof(length));
		write(identity.data(), length);
	}

	const int* mem = (const int*)memory;
	const u64 memints = memory_size / sizeof(int);
	for (u64 m = 0; m < memints;)
	{
		const int value = mem[m++];
		write(&value, sizeof(value));
		if (!value)
		{
			// A zero word is followed by the number of additional zero words.
			u32 extra = 0;
			while (m < memints && !mem[m] && extra != UINT32_MAX)
			{
				++m;
				++extra;
			}
			write(&extra, sizeof(extra));
		}
	}

	write(&state, sizeof(state));
	for (int i = 0; i < iNumComponents; i++)
	{
		if (acComponents[i]->SaveState(f) || ferror(f))
			FAILURE(Runtime, "Unable to save component state");
	}
	file.publish();
}

/**
 * Restore system state with device threads stopped. Return false only when
 * the file is rejected before changing guest state; failures after that throw.
 **/
bool CSystem::RestoreState(const char* fn)
{
	std::lock_guard<std::recursive_mutex> bus_lock(device_bus_mutex);
	SDeviceAccessScope time_scope(this);
	std::unique_ptr<FILE, decltype(&fclose)> file(fopen(fn, "rb"), &fclose);
	if (!file)
	{
		printf("%%SYS-F-NOFILE: Can't open restore file %s\n", fn);
		return false;
	}
	FILE* f = file.get();
	u32 magic = 0, version = 0, system_size = 0, component_count = 0;
	u64 memory_size = 0;

	if (fread(&magic, sizeof(magic), 1, f) != 1 ||
		magic != system_state_magic ||
		fread(&version, sizeof(version), 1, f) != 1)
	{
		printf("%%SYS-F-FORMAT: %s does not appear to be a state file.\n", fn);
		return false;
	}
	if (version != system_state_version)
	{
		printf("%%SYS-I-VERSION: State file %s is incompatible; "
			"version 2.8 is required.\n", fn);
		return false;
	}
	if (fread(&memory_size, sizeof(memory_size), 1, f) != 1 ||
		fread(&system_size, sizeof(system_size), 1, f) != 1 ||
		fread(&component_count, sizeof(component_count), 1, f) != 1 ||
		memory_size != (U64(1) << iNumMemoryBits) ||
		system_size != sizeof(state) || component_count != (u32)iNumComponents)
	{
		printf("%%SYS-F-CONFIG: State file %s has an incomplete or "
			"incompatible system header.\n", fn);
		return false;
	}

	// Validate every device and backing medium before replacing any guest RAM.
	try
	{
		for (int i = 0; i < iNumComponents; ++i)
			acComponents[i]->prepare_snapshot();
		for (int i = 0; i < iNumComponents; ++i)
		{
			u32 length = 0;
			if (fread(&length, sizeof(length), 1, f) != 1 ||
				!length || length > snapshot_identity_limit)
			{
				printf("%%SYS-F-MANIFEST: Invalid state device manifest.\n");
				return false;
			}
			std::string identity(length, '\0');
			if (fread(&identity[0], length, 1, f) != 1 ||
				identity != acComponents[i]->snapshot_identity())
			{
				printf("%%SYS-F-MANIFEST: State device or media does not match %s.\n",
					acComponents[i]->devid_string);
				return false;
			}
		}
	}
	catch (const CException& e)
	{
		printf("%%SYS-F-MANIFEST: Unable to verify state media: %s\n",
			e.displayText().c_str());
		return false;
	}

	// After mutation begins a failure must stop emulation, rather than resume.
	const auto read = [f](void* data, size_t size)
	{
		if (fread(data, size, 1, f) != 1)
			FAILURE(Runtime, "Incomplete system state");
	};
	int* mem = (int*)memory;
	const u64 memints = memory_size / sizeof(int);
	for (u64 m = 0; m < memints;)
	{
		int value;
		read(&value, sizeof(value));
		if (value)
			mem[m++] = value;
		else
		{
			u32 extra;
			read(&extra, sizeof(extra));
			if ((u64)extra >= memints - m)
				FAILURE(Runtime, "Invalid zero run in system state");
			const u64 end = m + (u64)extra + 1;
			while (m < end)
				mem[m++] = 0;
		}
	}

	read(&state, sizeof(state));

	// components
	//
	//  Components should also save any non-initial memory-registrations and re-register upon restore!
	//
	for (int i = 0; i < iNumComponents; i++)
	{
		if (acComponents[i]->RestoreState(f) || ferror(f) || feof(f))
			FAILURE(Runtime, "Unable to restore system state");
	}
	for (int i = 0; i < iNumComponents; ++i)
		acComponents[i]->finalize_restore();
	update_pchip_error_irqs();
	return true;
}

/**
 * Dump memory contents to a file.
 **/
void CSystem::DumpMemory(unsigned int filenum)
{
	char    file[100];
	u64     x;
	int* mem = (int*)memory;
	FILE* f;

	sprintf(file, "memory_%012d.dmp", filenum);
	f = fopen(file, "wb");

	x = (U64(1) << iNumMemoryBits) / sizeof(int) / 2;

	while (x > 0 && !mem[x - 1])
		x--;

	fwrite(mem, 1, (size_t)(x * sizeof(int)), f);
	fclose(f);
}

/**
 *  Dump system state to stdout for debugging purposes.
 **/
void CSystem::panic(char* message, int flags)
{
	int         cpunum;

	int         i;
	CAlphaCPU* cpu;
	printf("\n******** SYSTEM PANIC *********\n");
	printf("* %s\n", message);
	printf("*******************************\n");
	for (cpunum = 0; cpunum < iNumCPUs; cpunum++)
	{
		cpu = acCPUs[cpunum];
		printf("\n==================== STATE OF CPU %d ====================\n",
			cpunum);

		printf("PC: %016" PRIx64 "\n", cpu->get_pc());
#ifdef IDB
		printf("Physical PC: %016" PRIx64 "\n", cpu->get_current_pc_physical());
		printf("Instruction Count: %" PRId64 "\n", cpu->get_instruction_count());
#endif
		printf("\n");

		for (i = 0; i < 32; i++)
		{
			if (i < 10)
				printf("R");
			printf("%d:%016" PRIx64, i, cpu->get_r(i, false));
			if (i % 4 == 3)
				printf("\n");
			else
				printf(" ");
		}

		printf("\n");
		for (i = 4; i < 8; i++)
		{
			if (i < 10)
				printf("S");
			printf("%d:%016" PRIx64, i, cpu->get_r(i + 32, false));
			if (i % 4 == 3)
				printf("\n");
			else
				printf(" ");
		}

		for (i = 20; i < 24; i++)
		{
			if (i < 10)
				printf("S");
			printf("%d:%016" PRIx64, i, cpu->get_r(i + 32, false));
			if (i % 4 == 3)
				printf("\n");
			else
				printf(" ");
		}

		printf("\n");
		for (i = 0; i < 32; i++)
		{
			if (i < 10)
				printf("F");
			printf("%d:%016" PRIx64, i, cpu->get_f(i));
			if (i % 4 == 3)
				printf("\n");
			else
				printf(" ");
		}
	}

	printf("\n");
#ifdef IDB
	if (flags & PANIC_LISTING)
	{
		u64 start;

		u64 end;
		start = cpu->get_pc() - 64;
		end = start + 128;
		cpu->listing(start, end, cpu->get_pc());
	}
#endif
	if (flags & PANIC_ASKSHUTDOWN)
	{
		printf("Stop Emulation? ");

		int c = getc(stdin);
		if (c == 'y' || c == 'Y')
			flags |= PANIC_SHUTDOWN;
	}

	if (flags & PANIC_SHUTDOWN)
	{
		FAILURE(Abort, "Panic shutdown");
	}

	return;
}


#if defined(PROFILE)
u64       profile_buckets[PROFILE_BUCKETS];
u64       profiled_insts;
bool      profile_started = false;
#endif
CSystem* theSystem = 0;

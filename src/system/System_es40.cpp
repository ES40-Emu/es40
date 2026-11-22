/* ES40 Emulator.
 * Copyright (C) 2007-2008 by the ES40 Emulator Project
 * Copyright (C) 2026 by gdwnldsKSC
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
  * ES40 (Clipper) board: TIGbus registers, DIMM topology and SRM console loading.
  *
  * Split out of System.cpp; change history remains there.
  **/
#define __STDC_FORMAT_MACROS 1
#include "StdAfx.h"
#include "System_tsunami.h"
#include "CPU/AlphaCPU.h"
#ifdef ES40_JIT
#include "CPU/jit/jitengine.h"
#endif
#include "Flash.h"

/**
 * Read a byte from the TIGbus
 *
 * What information we have is sketchy at best. Following is extracted from T64,
 * Although we're not 100% sure that this is actually for ES40:
 *
 * \code
 * +-----------------+--------+
 * | register        | offset |
 * +-----------------+--------+
 * | trr             | 000    |
 * | smir            | 040    | system management IR
 * | cpuir           | 080    | CPU IR
 * | psir            | 0c0    | powe supply IR
 * | mod_info        | 100    |
 * | clk_info        | 140    |
 * | chip_info       | 180    |
 * | tpcr            | 200    |
 * | pll_data        | 280    |
 * | pll_clk         | 2c0    |
 * | ev6_init        | 300    |
 * | csleep          | 340    |
 * | smcr            | 380    |
 * | ttcr            | 3c0    |
 * | clr_irq5        | 400    |
 * | clr_irq4        | 440    |
 * | clr_pwr_flt_det | 480    |
 * | clr_temp_warn   | 4c0    |
 * | clr_temp_fail   | 500    |
 * | ev6_halt        | 5c0    |
 * | srcr0           | 600    |
 * | srcr1           | 640    |
 * | frar0           | 700    |
 * | frar1           | 740    |
 * | fwmr0           | 800    |
 * | fwmr1           | 840    |
 * | fwmr2           | 880    |
 * | fwmr3           | 8c0    |
 * | ipcr0           | a00    | inter-processor communications register for arbiter (?)
 * | ipcr1           | a40    |
 * | ipcr2           | a80    |
 * | ipcr3           | ac0    |
 * | ipcr4           | b00    |
 * +-----------------+--------+
 * \endcode
 **/
u8 Tsunami::tig_read(u32 a)
{
	switch (a)
	{
	case 0x30000000:  // trr
		return 0;
	case 0x30000040:  // smir
		return state.tig.FwWrite;
	case 0x30000100:  // mod_info
		return state.tig.ModInfo;
	case 0x300003c0:  // ttcr
		return state.tig.HaltA;
	case 0x30000440:  // clr_irq4: IRQ4 latch clear; our IRQ4 is level-driven
	case 0x30000480:  // clr_pwr_flt_det
		return 0;
	case 0x300005c0:  // ev6_halt
		return state.tig.HaltB;
	case 0x30000a00:  // ipcr0-4: PAL MP-restart handshake 
	case 0x30000a40:  
	case 0x30000a80:
	case 0x30000ac0:
	case 0x30000b00:
		return state.tig.ipcr[(a - 0x30000a00) >> 6];
	case 0x38000180:  // Arbiter revision
		return 0xfe;
	default:          printf("Unknown TIG %08x read attempted.\n", a); return 0;
	}
}

/**
 * Drive the per-CPU EV6 halt-interrupt lines (IRQ4) from the TIG halt
 * registers. 
 * Level-triggered: bit n of (ttcr | ev6_halt) is CPU n's line.
 * PAL's CSERVE MP_WORK_REQUEST sets the target's bit (read or write) after
 * storing the work code in the target's impure area. 
 * Target's halt-interrupt handler XOR-clears its own bit to ack and deassert.
 **/
void Tsunami::tig_update_halt_lines()
{
	u8 lines = state.tig.HaltA | state.tig.HaltB;
	for (int i = 0; i < get_cpu_num(); i++)
		get_cpu(i)->irq_h(4, (lines >> i) & 1, 0);
}

void Tsunami::tig_write(u32 a, u8 data)
{
	switch (a)
	{
	case 0x30000000:  // trr
		return;

	case 0x30000040:  // smir
		state.tig.FwWrite = data;
		return;

	case 0x30000100:  // mod_info
		state.tig.ModInfo = data;
		return;

	case 0x300003c0:  // ttcr
		state.tig.HaltA = data;
		tig_update_halt_lines();
		return;

	case 0x30000440:  // clr_irq4: IRQ4 latch clear; our IRQ4 is level-driven
	case 0x30000480:  // clr_pwr_flt_det
		return;

	case 0x300005c0:  // ev6_halt
		state.tig.HaltB = data;
		tig_update_halt_lines();
		return;

	case 0x30000600:  // srcr0
	case 0x30000640:  // srcr1
		// Empirical: LFU writes 0x30 here when exiting after an update.
		if (data & 0x30)
		{
			printf("%%SYS-I-RESETREQ: TIG SRCR write %07x=%02x\n", a, data);
			flush_boot_rom();
			RequestSystemReset();
		}
		return;

	case 0x30000a00:  // ipcr0-4: PAL MP-restart handshake registers
	case 0x30000a40:  
	case 0x30000a80:
	case 0x30000ac0:
	case 0x30000b00:
		state.tig.ipcr[(a - 0x30000a00) >> 6] = data;
		return;

	default:
		printf("Unknown TIG %07x write with %02x attempted.\n", a, data);
	}
}

/* ---------------- DIMM topology ---------------- */

void Tsunami::init_spd_from_config_mb(uint32_t total_mb)
{
	// Sets of 4 identical DIMMs fill one MMB's slot set (J1-4, then J5-8);
	// each populated MMB is one array (8 DIMMs max = Typhoon ASIZ max 8GB).
	// DIMM size = total/4 capped at 1GB, so every config is a 4-stick set
	// minimum and SRM reports 4-way interleave; >4GB spills to more sets.
	m_dimm_layout.dimm_mb = (total_mb / 4 > 1024) ? 1024 : total_mb / 4;
	uint32_t n_dimms = total_mb / m_dimm_layout.dimm_mb;
	m_dimm_layout.n_arrays = (int)((n_dimms + 7) / 8);
	m_dimm_layout.dimms_per_array = (int)(n_dimms / m_dimm_layout.n_arrays);
	m_dimm_spd = build_sdram_spd(m_dimm_layout.dimm_mb, /*registered_ecc*/true);

	// The I2C bus can't carry every DIMM (HRM 9.10): attach one
	// representative EEPROM per 4-DIMM set, at 0x50 + array*2 + set.
	for (int a = 0; a < m_dimm_layout.n_arrays; a++)
		for (int s = 0; s < m_dimm_layout.dimms_per_array / 4; s++)
			m_mpd_bus.attach(std::make_shared<Eeprom24C02>(uint8_t(0x50 + a * 2 + s), m_dimm_spd));
}

void Tsunami::flush_boot_rom()
{
	if (theSROM)
		theSROM->FlushIfDirty();
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
		cpu->jit_step((int) CJitEngine::kChainCeiling);
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
int Tsunami::LoadROM()
{
	if (!get_cpu_num() || get_cpu(0)->get_profile().reset.external_reset_vector)
		return 0;

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
			config()->get_text_value("rom.flash", "flash.rom"));

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

		get_cpu(0)->set_pc(load_base | 1);
		get_cpu(0)->set_PAL_BASE(load_base);
		get_cpu(0)->enable_icache();

		bool decomp_ok = true;
		j = 0;
		while (get_cpu(0)->get_clean_pc() > U64(0x200000))
		{
			srm_decomp_chunk(this, get_cpu(0));
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

		get_cpu(0)->restore_icache();

		if (decomp_ok)
		{
			for (i = 0; i < get_cpu_num(); i++)
				get_cpu(i)->set_pc(get_cpu(0)->get_pc());
			for (i = 0; i < get_cpu_num(); i++)
				get_cpu(i)->set_PAL_BASE(get_cpu(0)->get_pal_base());

			loadedFromFlash = true;
		}
	}

	if (!loadedFromFlash)
	{
		f = fopen(config()->get_text_value("rom.srm", "cl67srmrom.exe"), "rb");
		if (!f)
			FAILURE(Runtime, "No SRM ROM image found");
		printf("%%SYS-I-READROM: Reading original ROM image from %s.\n",
			config()->get_text_value("rom.srm", "cl67srmrom.exe"));
		for (i = 0; i < 0x240; i++)
		{
			auto r = fread(&scratch, 1, 1, f);
			if (r != 1)
			{
				const bool read_error = ferror(f) != 0;
				fclose(f);
				if (read_error)
					FAILURE(IO, "Error reading SRM ROM image");
				FAILURE(Runtime, "File is too short to be a SRM ROM image");
			}
		}

		buffer = PtrToMem(0x900000);
		const char* payload_start = buffer;
		// EOF terminates the variable-length payload; only a read error is fatal.
		while (fread(buffer, 1, 1, f) == 1)
			++buffer;
		const bool read_error = ferror(f) != 0;
		fclose(f);
		if (read_error)
			FAILURE(IO, "Error reading SRM ROM image");
		if (buffer == payload_start)
			FAILURE(Runtime, "File is too short to be a SRM ROM image");

		printf("%%SYS-I-DECOMP: Decompressing ROM image.\n0%%");
		get_cpu(0)->set_pc(0x900001);
		get_cpu(0)->set_PAL_BASE(0x900000);
		get_cpu(0)->enable_icache();

		j = 0;
		while (get_cpu(0)->get_clean_pc() > 0x200000)
		{
			srm_decomp_chunk(this, get_cpu(0));
			j++;
			if (((j % 5) == 0) && (j < 50))
				printf("%d%%", j * 2);
			else
				printf(".");
			fflush(stdout);
		}

		printf("100%%\n");
		get_cpu(0)->restore_icache();
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
	get_cpu(0)->flush_icache();
#endif
	printf("%%SYS-I-ROMLOADED: ROM Image loaded successfully!\n");
	return 0;
}

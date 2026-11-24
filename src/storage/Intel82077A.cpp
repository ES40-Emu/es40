/* ES40 Emulator.
 * Copyright (C) 2007-2008 by the ES40 Emulator Project
 * Copyright (C) 2019 by Ingo Albrecht
 * Copyright (C) 2026 by gdwnldsKSC
 * Copyright (C) 2026 by BlueRain-debug
 * Copyright (C) 2026 by WNT50
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

#include "StdAfx.h"
#include "Intel82077A.h"
#include "chipset/EISABus.h"

Intel82077A::Intel82077A(CConfigurator* cfg, CSystem* system, EISABus& bus,
	unsigned slot, bool secondary) :
	CFloppyController(cfg, system, secondary ? 1 : 0, true), bus(bus), slot(slot)
{
	const u16 base = static_cast<u16>(isa_io_base); // J6 selects the port block (Adaptec TRM, p. 3-15).
	bus.register_slot_io(slot, *this, FDC_REG_DOR, base + FDC_REG_DOR, 1, 8);
	bus.register_slot_io(slot, *this, FDC_REG_STATUS, base + FDC_REG_STATUS, 2, 8);
	bus.register_slot_io(slot, *this, FDC_REG_DIR, base + FDC_REG_DIR, 1, 8);
	bind_isa_dma(static_cast<CISABus&>(bus).dma_controller());
}

std::string Intel82077A::snapshot_identity() const
{
	return CSystemComponent::snapshot_identity() + ":" + std::to_string(isa_io_base);
}

bool Intel82077A::decodes_memory_access(int index, u64, int, bool write) const noexcept
{
	return index != FDC_REG_DOR || write; // DOR is write-only on this card (Adaptec TRM, p. 4-23).
}

void Intel82077A::drive_irq(bool asserted)
{
	bus.set_slot_irq(slot, 6, asserted); // Jensen routes the diskette controller to IRQ6 (PB22H-KB, Table 17-2).
}

bool Intel82077A::transfer_dma(u8* buffer, size_t length, bool to_memory)
{
	size_t offset = 0;
	bus.set_slot_drq(slot, 2, true); // DRQ precedes the data transfer (82077AA data sheet, sec. 2.3).
	try {
		while (offset < length) {
			u32 unit = 0;
			if (to_memory)
				for (unsigned i = 0; i < 4 && offset + i < length; ++i)
					unit |= u32(buffer[offset + i]) << (8 * i);
			auto result = to_memory ? bus.slot_dma_send_unit(slot, 2, unit)
				: bus.slot_dma_recv_unit(slot, 2, unit);
			if (!result.transferred || result.transferred > length - offset)
				break;
			if (!to_memory)
				for (size_t i = 0; i < result.transferred; ++i)
					buffer[offset + i] = u8(unit >> (8 * i));
			offset += result.transferred;
			if (result.terminal_count)
				break;
		}
	} catch (...) {
		bus.set_slot_drq(slot, 2, false);
		throw;
	}
	bus.set_slot_drq(slot, 2, false);
	return offset == length;
}

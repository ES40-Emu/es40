/* ES40 Emulator -- Cascaded 8259 interrupt controllers.
 * Copyright (C) 2007-2008 by the ES40 Emulator Project
 * Copyright (C) 2020 Martin Vorländer
 * Copyright (C) 2023-2026 by gdwnldsKSC
 * Copyright (C) 2026 by BlueRain-debug
 *
 * WWW    : https://github.com/ES40-Emu/es40
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
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

#include "config_debug.h"
#include "Pic8259.h"
#include <cstdio>
#include <utility>

Pic8259::Pic8259(State& state, std::function<void(bool)> output, EdgeRelease cascade_release)
	: state(state), output(std::move(output)), cascade_release(cascade_release)
{
}

void Pic8259::reset()
{
	CFastMutex::ScopedLock guard(picLock);
	state = {};
	update_output(0);
}

/**
 * Reset the PIC to its post-ICW1 state.  ELCR/LTIM are intentionally not reset
 * Caller must hold picLock.
 **/
void Pic8259::init_reset(int index)
{
	state.last_irr[index] = 0;
	state.irr[index] &= state.elcr[index];
	state.imr[index] = 0;
	state.isr[index] = 0;
	state.priority_add[index] = 0;
	state.irq_base[index] = 0;
	state.read_reg_select[index] = 0;
	state.poll[index] = 0;
	state.special_mask[index] = 0;
	state.init_state[index] = 0;
	state.auto_eoi[index] = 0;
	state.rotate_on_aeoi[index] = 0;
	state.special_fnm[index] = 0;
	state.init4[index] = 0;
	state.single_mode[index] = 0;
	update_output(index);
}

/* Highest-priority bit in mask, considering rotation.  Returns 8 if mask==0. */
static int pic_priority_of(u8 mask, u8 priority_add)
{
	if (mask == 0)
		return 8;
	int priority = 0;
	while ((mask & (1 << ((priority + priority_add) & 7))) == 0)
		priority++;
	return priority;
}

/**
 * Determine which IRQ (if any) the controller wants to deliver right now.
 * Returns -1 if nothing pending above the in-service priority threshold.
 * Caller must hold picLock.
 **/
int Pic8259::get_irq(int index) const
{
	int mask = state.irr[index] & ~state.imr[index];
	int priority = pic_priority_of((u8)mask, state.priority_add[index]);
	if (priority == 8)
		return -1;

	// Compute current in-service priority.  In SFNM on the master, slave-
	// originated IRQs (cascade IRQ2) do not block other slave IRQs.
	int isr_mask = state.isr[index];
	if (state.special_mask[index])
		isr_mask &= ~state.imr[index];
	if (state.special_fnm[index] && index == 0)
		isr_mask &= ~(1 << 2);

	int cur_priority = pic_priority_of((u8)isr_mask, state.priority_add[index]);
	if (priority < cur_priority)
		return (priority + state.priority_add[index]) & 7;
	return -1;
}

void Pic8259::update_output(int index)
{
	const bool high = get_irq(index) >= 0;
	if (index == 1)
		drive_line(0, 2, high, cascade_release);
	else
		output(high);
}

/**
 * Acknowledge an IRQ during the INTA cycle.  Sets ISR (unless AEOI), clears
 * IRR for edge-triggered lines, and re-evaluates the output.
 * Caller must hold picLock.
 **/
void Pic8259::intack(int index, int irq)
{
	if (state.auto_eoi[index])
	{
		if (state.rotate_on_aeoi[index])
			state.priority_add[index] = (irq + 1) & 7;
	}
	else
	{
		state.isr[index] |= (1 << irq);
	}

	// Level-sensitive lines keep their IRR bit set as long as the source
	// holds the line high; only edge-sensitive bits clear at INTA.
	const bool is_level =
		(state.elcr[index] & (1 << irq)) || state.ltim[index];
	if (!is_level)
		state.irr[index] &= ~(1 << irq);

	update_output(index);
}

/**
 * Read a byte from one of the programmable interrupt controller's registers.
 **/
u8 Pic8259::read(int index, u32 address)
{
	CFastMutex::ScopedLock guard(picLock);
	u8 data;

	if (state.poll[index])
	{
		int irq = get_irq(index);
		if (irq >= 0)
		{
			intack(index, irq);
			data = (u8)(irq | 0x80);
		}
		else
		{
			data = 0;
		}
		state.poll[index] = 0;
	}
	else
	{
		if (address == 0)
		{
			data = state.read_reg_select[index]
				? state.isr[index]
				: state.irr[index];
		}
		else
		{
			data = state.imr[index];
		}
	}

#ifdef DEBUG_PIC
	if (pic_messages)
		printf("%%PIC-I-READ: read %02x from port %u on PIC %d\n", data,
			address, index);
#endif
	return data;
}

/**
 * Read a byte from the edge/level register of one of the programmable interrupt controllers.
 **/
u8 Pic8259::read_edge_level(int index)
{
	CFastMutex::ScopedLock guard(picLock);
	return state.elcr[index];
}

u8 Pic8259::read_vector()
{
	CFastMutex::ScopedLock guard(picLock);

	int irq = get_irq(0);
	int intno;

	if (irq >= 0)
	{
		if (irq == 2)
		{
			// Cascade: fetch vector from slave.
			int irq2 = get_irq(1);
			if (irq2 >= 0)
			{
				intack(1, irq2);
			}
			else
			{
				irq2 = 7;   // spurious slave IRQ
			}
			intno = state.irq_base[1] + irq2;
			intack(0, irq);
		}
		else
		{
			intno = state.irq_base[0] + irq;
			intack(0, irq);
		}
	}
	else
	{
		// Spurious master IRQ.
		intno = state.irq_base[0] + 7;
	}

	return (u8)intno;
}

/**
 * Write a byte to one of the programmable interrupt controller's registers.
 * Implements the ICW1/ICW2/ICW3/ICW4 init sequence and the OCW1/OCW2/OCW3
 * operational commands.
 **/
void Pic8259::write(int index, u32 address, u8 data)
{
	CFastMutex::ScopedLock guard(picLock);

#ifdef DEBUG_PIC
	if (pic_messages)
		printf("%%PIC-I-WRITE: write %02x to port %u on PIC %d\n", data,
			address, index);
#endif

	if (address == 0)
	{
		if (data & 0x10)
		{
			// ICW1: start init sequence.
			init_reset(index);
			state.init_state[index] = 1;
			state.init4[index]      = (data & 0x01) ? 1 : 0;
			state.single_mode[index] = (data & 0x02) ? 1 : 0;
			state.ltim[index]       = (data & 0x08) ? 1 : 0;
		}
		else if (data & 0x08)
		{
			// OCW3
			if (data & 0x04)
				state.poll[index] = 1;
			if (data & 0x02)
				state.read_reg_select[index] = data & 0x01;
			if (data & 0x40)
				state.special_mask[index] = (data >> 5) & 0x01;
		}
		else
		{
			// OCW2
			const int cmd   = (data >> 5) & 7;
			const int level = data & 7;
			int eoi_irq;
			int priority;

			switch (cmd)
			{
			case 0: // rotate in auto-EOI mode (clear)
			case 4: // rotate in auto-EOI mode (set)
				state.rotate_on_aeoi[index] = (u8)(cmd >> 2);
				break;

			case 1: // non-specific EOI
			case 5: // rotate on non-specific EOI
				priority = pic_priority_of(state.isr[index],
					state.priority_add[index]);
				if (priority != 8)
				{
					eoi_irq = (priority + state.priority_add[index]) & 7;
					state.isr[index] &= ~(1 << eoi_irq);
					if (cmd == 5)
						state.priority_add[index] = (u8)((eoi_irq + 1) & 7);
					update_output(index);
				}
#ifdef DEBUG_PIC
				pic_messages = false;
#endif
				break;

			case 3: // specific EOI
				state.isr[index] &= ~(1 << level);
				update_output(index);
#ifdef DEBUG_PIC
				pic_messages = false;
#endif
				break;

			case 6: // set priority command
				state.priority_add[index] = (u8)((level + 1) & 7);
				update_output(index);
				break;

			case 7: // rotate on specific EOI
				state.isr[index] &= ~(1 << level);
				state.priority_add[index] = (u8)((level + 1) & 7);
				update_output(index);
#ifdef DEBUG_PIC
				pic_messages = false;
#endif
				break;

			default:
				// no operation
				break;
			}
		}
		return;
	}

	// address == 1: ICW2/3/4 during init, IMR (OCW1) otherwise.
	switch (state.init_state[index])
	{
	case 0:
		// OCW1: write IMR.  Masking only gates delivery; IRR and ISR are
		// unaffected — re-evaluating the output picks up any newly-unmasked
		// pending IRR bit.
		state.imr[index] = data;
		update_output(index);
		break;

	case 1:
		// ICW2: vector base.
		state.irq_base[index] = data & 0xf8;
		state.init_state[index] = state.single_mode[index]
			? (state.init4[index] ? 3 : 0)
			: 2;
		break;

	case 2:
		// ICW3: cascade configuration (we hard-wire master IRQ2 ↔ slave).
		state.init_state[index] = state.init4[index] ? 3 : 0;
		break;

	case 3:
		// ICW4
		state.special_fnm[index] = (data >> 4) & 0x01;
		state.auto_eoi[index]    = (data >> 1) & 0x01;
		state.init_state[index]  = 0;
		break;
	}
}

/**
 * Write a byte to the edge/level register of one of the programmable interrupt controllers.
 **/
void Pic8259::write_edge_level(int index, u8 data)
{
	CFastMutex::ScopedLock guard(picLock);
	state.elcr[index] = data;
	// Changing trigger mode can affect the wanted-IRQ calculation if any
	// previously-edge-latched bit is now considered level.
	update_output(index);
}

void Pic8259::latch_request(int index, int irq)
{
	CFastMutex::ScopedLock guard(picLock);
	const u8 mask = (u8)(1 << irq);
	state.irr[index] |= mask;
	state.last_irr[index] |= mask;
#ifdef DEBUG_PIC
	if (index != 0 || irq > 4)
	{
		printf("%%PIC-I-INCOMING: Interrupt %d incoming on PIC %d%s\n", irq, index,
			(state.imr[index] & mask) ? " (masked, latched)" : "");
		pic_messages = true;
	}
#endif
	update_output(index);
}

void Pic8259::set_line(int index, int irq, bool active, EdgeRelease release)
{
	CFastMutex::ScopedLock guard(picLock);
	drive_line(index, irq, active, release);
}

void Pic8259::drive_line(int index, int irq, bool active, EdgeRelease release)
{
	const u8 mask = (u8)(1 << irq);
	const bool is_level = (state.elcr[index] & mask) || state.ltim[index];
	if (active)
	{
		if (is_level || !(state.last_irr[index] & mask))
			state.irr[index] |= mask;
		state.last_irr[index] |= mask;
	}
	else
	{
		if (is_level || release == EdgeRelease::Withdraw)
			state.irr[index] &= ~mask;
		state.last_irr[index] &= ~mask;
	}
	update_output(index);
}

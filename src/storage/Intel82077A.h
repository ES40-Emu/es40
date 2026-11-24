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

#ifndef ES40_INTEL82077A_H
#define ES40_INTEL82077A_H

#include "FloppyController.h"

class EISABus;

// The AHA-1742A has a separate PC/AT floppy controller (Adaptec TRM, Fig. 2-1 and p. 2-4).
class Intel82077A final : public CFloppyController
{
public:
  Intel82077A(CConfigurator* cfg, CSystem* system, EISABus& bus,
    unsigned slot, bool secondary = false);
  std::string snapshot_identity() const override;
  bool decodes_memory_access(int index, u64 address, int dsize, bool write) const noexcept override;

private:
  void drive_irq(bool asserted) override;
  bool transfer_dma(u8* buffer, size_t length, bool to_memory) override;
  EISABus& bus;
  const unsigned slot;
};

#endif

/* ES40 Emulator.
 * Copyright (C) 2007 by the ES40 Emulator Project
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
  * Contains code for the base class for devices that connect to the chipset.
  *
  * X-1.12       Camiel Vanderhoeven                             13-MAR-2008
  *      Create init(), start_threads() and stop_threads() functions.
  *
  * X-1.11       Camiel Vanderhoeven                             29-DEC-2007
  *      Fix memory-leak.
  *
  * X-1.10       Camiel Vanderhoeven                             28-DEC-2007
  *      Keep the compiler happy.
  *
  * X-1.9        Camiel Vanderhoeven                             17-DEC-2007
  *      SaveState file format 2.1
  *
  * X-1.8        Camiel Vanderhoeven                             10-DEC-2007
  *      Use configurator.
  *
  * X-1.7        Camiel Vanderhoeven                             30-MAR-2007
  *      Added old changelog comments.
  *
  * X-1.6        Brian Wheeler                                   13-FEB-2007
  *      Formatting.
  *
  * X-1.5        Camiel Vanderhoeven                             12-FEB-2007
  *      Added comments.
  *
  * X-1.4        Camiel Vanderhoeven                             9-FEB-2007
  *      Added comments.
  *
  * X-1.3        Brian Wheeler                                   3-FEB-2007
  *      Formatting.
  *
  * X-1.2        Brian Wheeler                                   3-FEB-2007
  *      Includes are now case-correct (necessary on Linux)
  *
  * X-1.1        Camiel Vanderhoeven                             19-JAN-2007
  *      Initial version in CVS.
  *
  * \author Camiel Vanderhoeven
  **/
#include "StdAfx.h"
#include "SystemComponent.h"
#include "system/System.h"

  /**
   * Constructor.
   **/
CSystemComponent::CSystemComponent(CConfigurator* cfg, CSystem* system)
{
	char* a;
	char* b;

	system->RegisterComponent(this);
	cSystem = system;
	myCfg = cfg;

	a = myCfg->get_myName();
	b = myCfg->get_myValue();

	CHECK_ALLOCATION(devid_string = (char*)malloc(strlen(a) + strlen(b) + 3));
	sprintf(devid_string, "%s(%s)", a, b);
}

/**
 * destructor.
 **/
CSystemComponent::~CSystemComponent()
{
	if (cSystem)
		cSystem->UnregisterComponent(this);
	free(devid_string);
	devid_string = nullptr;
}

void CSystemComponent::reset_state_registration()
{
	if (m_state_registration_closed)
		throw std::logic_error("State registration is closed");
	m_state_fields.clear();
}

void CSystemComponent::add_state_field(StateField field)
{
	if (m_state_registration_closed)
		throw std::logic_error("State registration is closed");
	if (field.name.empty() || field.name.size() > 65536 ||
		m_state_fields.size() == (std::numeric_limits<u32>::max)())
		throw std::invalid_argument("Invalid saved field name or count");
	for (const auto& existing : m_state_fields)
		if (existing.name == field.name && existing.index == field.index)
			throw std::invalid_argument("Duplicate saved field: " + field.name);
	m_state_fields.push_back(std::move(field));
}

static constexpr u32 registered_state_magic = 0x31545352; // RST1

bool CSystemComponent::save_registered_state(FILE* f) const
{
	if (!f)
		return false;
	m_state_registration_closed = true;
	const auto write = [f](const void* data, size_t size) {
		return !size || fwrite(data, size, 1, f) == 1;
	};
	const u32 count = u32(m_state_fields.size());
	if (!write(&registered_state_magic, sizeof(u32)) || !write(&count, sizeof(count)))
		return false;
	for (const auto& field : m_state_fields)
	{
		const auto* source = static_cast<const u8*>(field.address());
		if (field.count && !source)
			return false;
		const u32 name_size = u32(field.name.size());
		if (!write(&name_size, sizeof(name_size)) || !write(field.name.data(), name_size) ||
			!write(&field.index, sizeof(field.index)) || !write(&field.type, sizeof(field.type)) ||
			!write(&field.element_size, sizeof(field.element_size)) || !write(&field.count, sizeof(field.count)))
			return false;
		for (u32 block = 0; block < field.blocks; ++block)
		{
			if (field.type == 1)
			{
				const auto* values = reinterpret_cast<const bool*>(source);
				for (size_t i = 0; i < field.block_size / sizeof(bool); ++i)
				{
					const u8 value = values[i] ? 1 : 0;
					if (!write(&value, sizeof(value)))
						return false;
				}
			}
			else if (!write(source, field.block_size))
				return false;
			if (block + 1 < field.blocks)
				source += field.stride;
		}
	}
	return true;
}

bool CSystemComponent::read_registered_state(FILE* f, RegisteredState& saved) const
{
	saved.clear();
	if (!f)
		return false;
	m_state_registration_closed = true;
	const auto read = [f](void* data, size_t size) {
		return !size || fread(data, size, 1, f) == 1;
	};
	u32 magic = 0, count = 0;
	if (!read(&magic, sizeof(magic)) || magic != registered_state_magic ||
		!read(&count, sizeof(count)) || count != m_state_fields.size())
		return false;
	RegisteredState pending;
	pending.reserve(count);
	for (const auto& field : m_state_fields)
	{
		u32 name_size = 0, type = 0, element_size = 0, elements = 0;
		s32 index = 0;
		if (!read(&name_size, sizeof(name_size)) || name_size != field.name.size())
			return false;
		std::string name(name_size, '\0');
		if (!read(name.data(), name.size()) || name != field.name ||
			!read(&index, sizeof(index)) || index != field.index ||
			!read(&type, sizeof(type)) || type != field.type ||
			!read(&element_size, sizeof(element_size)) || element_size != field.element_size ||
			!read(&elements, sizeof(elements)) || elements != field.count)
			return false;
		pending.emplace_back(field.size());
		auto& bytes = pending.back();
		if (!read(bytes.data(), bytes.size()))
			return false;
		if (field.type == 1)
			for (u8 value : bytes)
				if (value > 1)
					return false;
	}
	saved = std::move(pending);
	return true;
}

bool CSystemComponent::commit_registered_state(const RegisteredState& saved) const
{
	if (saved.size() != m_state_fields.size())
		return false;
	// Validate all destinations before modifying state.
	std::vector<u8*> destinations;
	destinations.reserve(m_state_fields.size());
	for (size_t i = 0; i < m_state_fields.size(); ++i)
	{
		const auto& field = m_state_fields[i];
		auto* address = static_cast<u8*>(field.address());
		if ((field.count && !address) || saved[i].size() != field.size())
			return false;
		if (field.type == 1)
			for (u8 value : saved[i])
				if (value > 1)
					return false;
		destinations.push_back(address);
	}
	for (size_t i = 0; i < m_state_fields.size(); ++i)
	{
		const auto& field = m_state_fields[i];
		auto* destination = destinations[i];
		const auto* source = saved[i].data();
		for (u32 block = 0; block < field.blocks; ++block)
		{
			if (field.type == 1)
			{
				auto* values = reinterpret_cast<bool*>(destination);
				for (size_t j = 0; j < field.block_size / sizeof(bool); ++j)
					values[j] = *source++ != 0;
			}
			else
			{
				memcpy(destination, source, field.block_size);
				source += field.block_size;
			}
			if (block + 1 < field.blocks)
				destination += field.stride;
		}
	}
	return true;
}

int CSystemComponent::SaveState(FILE* f)
{
	return save_registered_state(f) ? 0 : -1;
}

int CSystemComponent::RestoreState(FILE* f)
{
	RegisteredState saved;
	return read_registered_state(f, saved) && commit_registered_state(saved) ? 0 : -1;
}

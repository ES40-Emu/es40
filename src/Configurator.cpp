/* ES40 Emulator.
 * Copyright (C) 2007-2008 by the ES40 Emulator Project
 * Copyright (C) 2026 by gdwnldsKSC
 * Copyright (C) 2026 by Cacodemon345
 * Copyright (C) 2026 by Martin Vorländer
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
  * Contains the code for the configuration file interpreter.
  *
  * $Id$
  *
  * X-1.31       Camiel Vanderhoeven                             30-APR-2008
  *      For disk controllers, myDevice points to the CDiskController part
  *      of the class as it's used to register disks to.
  *
  * X-1.30       Camiel Vanderhoeven                             29-APR-2008
  *      Fixed a mistake in the last commit.
  *
  * X-1.28       Brian Wheeler/Camiel Vanderhoeven               29-APR-2008
  *      Added Floppy Controller.
  *
  * X-1.27       Camiel Vanderhoeven                             26-MAR-2008
  *      Fix compiler warnings.
  *
  * X-1.26       Camiel Vanderhoeven                             14-MAR-2008
  *      Formatting.
  *
  * X-1.25       Camiel Vanderhoeven                             14-MAR-2008
  *   1. More meaningful exceptions replace throwing (int) 1.
  *   2. U64 macro replaces X64 macro.
  *
  * X-1.24       Camiel Vanderhoeven                             13-MAR-2008
  *      Create init() start_threads() and stop_threads() functions.
  *
  * X-1.23       Camiel Vanderhoeven                             05-MAR-2008
  *      Multi-threading version.
  *
  * X-1.22       David Hittner                                   04-MAR-2008
  *      Allow curly braces inside strings.
  *
  * X-1.21       Camiel Vanderhoeven                             04-MAR-2008
  *      Merged Brian wheeler's New IDE code into the standard controller.
  *
  * X-1.20       Pepito Grillo                                   02-MAR-2008
  *      Avoid compiler warnings.
  *
  * X-1.19       Camiel Vanderhoeven                             02-MAR-2008
  *      Natural way to specify large numeric values ("10M") in the config file.
  *
  * X-1.18       Brian Wheeler                                   27-FEB-2008
  *      Avoid compiler warnings.
  *
  * X-1.17       Brian Wheeler                                   26-FEB-2007
  *      Better syntax checking and error reporting.
  *
  * X-1.16       Camiel Vanderhoeven                             26-FEB-2008
  *      Moved DMA code into it's own class (CDMA)
  *
  * X-1.15       Camiel Vanderhoeven                             16-FEB-2008
  *      Forgot something on last change.
  *
  * X-1.14       Camiel Vanderhoeven                             16-FEB-2008
  *      Added Symbios 53C810 controller.
  *
  * X-1.13       Camiel Vanderhoeven                             12-FEB-2008
  *      Moved keyboard code into it's own class (CKeyboard)
  *
  * X-1.12       Camiel Vanderhoeven                             20-JAN-2008
  *      Added X11 GUI.
  *
  * X-1.11       Camiel Vanderhoeven                             19-JAN-2008
  *      Added win32 GUI.
  *
  * X-1.10       Camiel Vanderhoeven                             09-JAN-2008
  *      Save disk state to state file.
  *
  * X-1.9        Camiel Vanderhoeven                             08-JAN-2008
  *      Use Brian Wheeler's CNewIde class instead of the CAliM1543C_ide
  *      class if HAVE_NEW_IDE is defined. This change will be undone when
  *      the new ide controller will replace the old standard one.
  *
  * X-1.8        Camiel Vanderhoeven                             05-JAN-2008
  *      Added CDiskDevice class.
  *
  * X-1.7        Camiel Vanderhoeven                             02-JAN-2008
  *      Better handling of configuration errors.
  *
  * X-1.6        Camiel Vanderhoeven                             28-DEC-2007
  *      Throw exceptions rather than just exiting when errors occur.
  *
  * X-1.5        Camiel Vanderhoeven                             28-DEC-2007
  *      Keep the compiler happy.
  *
  * X-1.4        Camiel Vanderhoeven                             14-DEC-2007
  *      Add support for Symbios SCSI controller.
  *
  * X-1.3        Camiel Vanderhoeven                             12-DEC-2007
  *      Add support for file- and RAM-disk.
  *
  * X-1.2        Brian Wheeler                                   10-DEC-2007
  *      Better error reporting.
  *
  * X-1.1        Camiel Vanderhoeven                             10-DEC-2007
  *      Initial version in CVS.
  **/
#include "StdAfx.h"
#include "Configurator.h"

#ifndef CONFIGURATION_ONLY

#include "System.h"
#include "AlphaCPU.h"
#include "Serial.h"
#include "Flash.h"
#include "DPR.h"
#include "AliM1543C.h"
#include "Keyboard.h"
#include "DMA.h"
#include "AliM1543C_ide.h"
#include "AliM1543C_usb.h"
#include "AliM1543C_pmu.h"
#include "DiskFile.h"
#include "DiskDevice.h"
#include "DiskRam.h"
#include "Port80.h"
#include "graphics/S3Trio64.h"
#include "graphics/GloriaSynergy.h"
#include "graphics/Radeon7500.h"
#include "graphics/PowerStorm3xx.h"
#ifdef HAVE_CIRRUS
#include "graphics/Cirrus.h" // to be re-added and fixed in the future
#endif
#include "FloppyController.h"
#include "gui/plugin.h"
#if defined(HAVE_PCAP) || defined(HAVE_TAP_NET) || defined(HAVE_VMNET)
#include "DEC21143.h"
#endif
#include "LSI53C1020.h"
#include "Sym53C895.h"
#include "Sym53C810.h"
#include "ES1370.h"
#include "MPU401.h"

#endif // !CONFIGURATION_ONLY

  /**
   * Constructor.
   *
   * The portion of the configuration file that corresponds with the device we are the configurator for
   * is passed as text, with a length of textlen.
   * We parse this text portion, creating values and children configurators as needed.
   * If parent is NULL, we are the master configurator, and we will call initialize for our children
   * (probably GUI and System) so they can instantiate the classes that correspond to the devices that
   * they configure for. The children will in turn initialize their children.
   *
   * \bug This needs to be more robust! As it is now, this code was more or less "hacked together" in a
   *      few minutes. Also, more comments should be provided to make it more readable.
   **/
CConfigurator::CConfigurator(class CConfigurator* parent, char* name,
	char* value, char* text, size_t textlen)
{
	if (parent == 0)
	{

		/* Phase 1:  Basic Syntax Check & Strip [first pass only]
		 * - Make sure quotes and comments are closed.
		 * - Make sure braces are balanced
		 * - remove everything except configuration data.
		 */
		char* dst = (char*)malloc(textlen + 1);
		char* p = dst;
		char* q = text;

		int     cbrace = 0;
		enum
		{
			STATE_NONE,
			STATE_C_COMMENT,
			STATE_CC_COMMENT,
			STATE_STRING
		}

		state = STATE_NONE;

		int state_start = 0;
		int line = 1;
		int col = 1;
		bool line_has_content = false;
		for (unsigned i = 0; i < textlen; i++, q++, col++)
		{
			if (*q == 0x0a)
			{
				line++;
				col = 1;
				line_has_content = false;
			}

			switch (state)
			{
			case STATE_NONE:
				switch (*q)
				{
				case '"':
					state = STATE_STRING;
					state_start = line;
					line_has_content = true;
					*p++ = *q;
					break;

				case '/':
					if (i == textlen - 1)
						FAILURE_2(Configuration,
							"Configuration file ends in mid-token at line %d, col %d",
							line, col);

					if (*(q + 1) == '/')
					{
						state = STATE_CC_COMMENT;
						state_start = line;
					}
					else if (*(q + 1) == '*')
					{
						state = STATE_C_COMMENT;
						state_start = line;
					}
					break;

				case '{':
					cbrace++;
					line_has_content = true;
					*p++ = *q;
					break;

				case '}':
					if (cbrace == 0)
						FAILURE_2(Configuration,
							"Too many closed braces at line %d, col %d", line, col);
					cbrace--;
					line_has_content = true;
					*p++ = *q;
					break;

				case ';':
					// A ';' as the first non-whitespace character of a line is
					// an INI-style comment running to end of line. Elsewhere it
					// keeps its meaning as the value terminator.
					if (!line_has_content)
					{
						state = STATE_CC_COMMENT;
						state_start = line;
					}
					else
						*p++ = *q;
					break;

				default:
					if (!isspace(*q))
					{
						line_has_content = true;
						if (isalnum(*q) || *q == '_' || *q == '.' || *q == '=')
						{
							*p++ = *q;
						}
						else
							FAILURE_3(Configuration,
								"Illegal character %c at line %d, col %d", *q, line, col);
					}
				}
				break;

			case STATE_CC_COMMENT:  // c++ comment
				if (*q == 0x0d || *q == 0x0a)
				{
					state = STATE_NONE;
					state_start = line;
				}
				break;

			case STATE_C_COMMENT:   // c comment
				if (*q == '*')
				{
					if (i == textlen - 1)
						FAILURE_2(Configuration,
							"Configuration file ends in mid-comment at line %d, col %d",
							line, col);
					if (*(q + 1) == '/')
					{
						state = STATE_NONE;
						state_start = line;
					}
				}
				break;

			case STATE_STRING:      // string
				if (*q == '"')
				{
					if (i == textlen - 1)
						FAILURE_2(Configuration,
							"Configuration file ends in mid-string at line %d, col %d",
							line, col);
					if (*(q + 1) != '"')
					{
						state_start = line;
						state = STATE_NONE;
					}
					else
					{
						*p++ = *q;
						i++;
						q++;
					}
				}
				else if (*q == 0x0a || *q == 0x0d)
					FAILURE_2(Configuration,
						"Multi-line strings are forbidden at line %d, col %d", line,
						col);
				*p++ = *q;
				break;
			}
		}

		*p++ = 0;

		if (state != 0 && state != 1)
		{
			printf("%%SYS-E-PARSE: unclosed %s.  Started on line %d.\n",
				state == STATE_C_COMMENT ? "comment" : "string", state_start);
		}

		if (cbrace != 0)
		{
			printf("%%SYS-E-PARSE: unclosed brace in file.\n");
		}

		textlen = strlen(dst);
		memcpy(text, dst, textlen + 1);
		free(dst);
	}

	/* Phase 2: Parse the file [every time]
	 * The data is in a very compressed form at this point.  We also
	 * know some important things about the data:
	 * - There is no whitespace (except in strings)
	 * - the braces are all closed
	 * - comments have been removed.
	 * - strings are valid.
	 */
	enum
	{
		STATE_NONE,
		STATE_NAME,
		STATE_IS,
		STATE_VALUE,
		STATE_QUOTE,
		STATE_CHILD
	}

	state = STATE_NONE;

	char* cur_name;
	char* cur_value;

	int     child_depth = 0;

	size_t  name_start = 0;
	size_t  name_len = 0;

	size_t  value_start = 0;
	size_t  value_len = 0;

	size_t  child_start = 0;
	size_t  child_len = 0;

	pParent = parent;
	iNumChildren = 0;
	iNumValues = 0;
	myName = name;
	myValue = value;

	for (size_t curtext = 0; curtext < textlen; curtext++)
	{
		switch (state)
		{
		case STATE_NONE:
			if (isalnum((unsigned char)text[curtext]) || text[curtext] == '.'
				|| text[curtext] == '_')
			{
				name_start = curtext;
				state = STATE_NAME;
			}
			break;

		case STATE_NAME:
			if (text[curtext] == '=')
			{
				state = STATE_IS;
				name_len = curtext - name_start;
			}
			break;

		case STATE_IS:
			if (isalnum((unsigned char)text[curtext]) || text[curtext] == '.'
				|| text[curtext] == '_')
			{
				value_start = curtext;
				value_len = 1;
				state = STATE_VALUE;
			}

			if (text[curtext] == '\"')
			{
				value_start = curtext;
				state = STATE_QUOTE;

				//curtext--;
			}
			break;

		case STATE_VALUE:
			if (text[curtext] == ';')
			{
				value_len = curtext - value_start;
				cur_name = (char*)malloc(name_len + 1);
				memcpy(cur_name, &text[name_start], name_len);
				cur_name[name_len] = '\0';
				cur_value = (char*)malloc(value_len + 1);
				memcpy(cur_value, &text[value_start], value_len);
				cur_value[value_len] = '\0';

				//        printf("Calling strip_string for  <%s>. \n",cur_value);
				strip_string(cur_value);
				add_value(cur_name, cur_value);

				state = STATE_NONE;
			}
			else if (text[curtext] == '{')
			{
				value_len = curtext - value_start;
				state = STATE_CHILD;
				child_start = curtext + 1;
				child_depth = 1;
			}
			break;

		case STATE_QUOTE:
			if ((text[curtext] == '\"') && (text[curtext + 1] == '\"'))
			{
				curtext++;
			}
			else if (text[curtext] == '\"')
			{
				state = STATE_VALUE;
			}
			break;

		case STATE_CHILD:
			if (text[curtext] == '{')
				child_depth++;
			else if (text[curtext] == '}')
			{
				child_depth--;
				if (!child_depth)
				{
					cur_name = (char*)malloc(name_len + 1);
					memcpy(cur_name, &text[name_start], name_len);
					cur_name[name_len] = '\0';
					cur_value = (char*)malloc(value_len + 1);
					memcpy(cur_value, &text[value_start], value_len);
					cur_value[value_len] = '\0';
					child_len = curtext - child_start;
					state = STATE_NONE;

					strip_string(cur_value);

					pChildren[iNumChildren++] = new CConfigurator(this, cur_name,
						cur_value,
						&text[child_start],
						child_len);
				}
			}
		}
	}

	int i;
	if (parent == 0)
	{
		myFlags = 0;
		for (i = 0; i < iNumChildren; i++)
		{
			pChildren[i]->initialize();
		}
	}
}

/**
 * Destructor.
 *
 * \bug This does nothing now; it should:
 *       - if we are a top-level component (System or GUI) delete the component (which
 *         will delete the components children).
 *       - delete our children.
 *       - free memory we allocated for values.
 *       .
 **/
CConfigurator::~CConfigurator(void)
{
}

/**
 * Reduce a quoted string to it's real value.
 * Some values are enclosed in double quotes("), in this case, we take off the quotes,
 * and replace all double double quotes ("") with single double quotes ("). Quoting values
 * is particularly useful if values contain forbidden characters such as spaces, quotes,
 * semicolons, etc. e.g. a text like
 *   "c:\program files\putty\putty.exe" telnet://localhost:8000
 * should be quoted as
 *   """c:\program files\putty\putty.exe"" telnet://localhost:8000"
 **/
char* CConfigurator::strip_string(char* c)
{
	char* pos = c;
	char* org = c + 1;
	bool    end_it = false;

	if (c[0] == '\"')
	{
		while (!end_it)
		{
			if (*org == '\"')
			{
				org++;
				if (*org == '\"')
					*pos++ = *org++;
				else
					end_it = true;
			}
			else
				*pos++ = *org++;
		}

		*pos = '\0';
	}

	return c;
}

/**
 * Add a value to our list of values.
 **/
void CConfigurator::add_value(char* n, char* v)
{
	pValues[iNumValues].name = n;
	pValues[iNumValues].value = v;
	iNumValues++;
}

/**
 * Return a text value, if the name of the value can't be found in
 * our list of values, return def.
 **/
char* CConfigurator::get_text_value(const char* n, const char* def)
{
	int i;
	for (i = 0; i < iNumValues; i++)
	{
		if (!strcmp(pValues[i].name, n))
			return pValues[i].value;
	}

	return (char*)def;
}

/**
 * Return a boolean value, if the name of the value can't be found in
 * our list of values, or if the value isn't valid, return def.
 *
 * Valid values are strings that have a first character of:
 *  - t (true)
 *  - y (yes, evaluates to true)
 *  - 1 (evaluates to true)
 *  - f (false)
 *  - n (no, evaluates to false)
 *  - 0 (evaluates to false)
 *  .
 **/
bool CConfigurator::get_bool_value(const char* n, bool def)
{
	int i;
	for (i = 0; i < iNumValues; i++)
	{
		if (!strcmp(pValues[i].name, n))
		{
			switch (pValues[i].value[0])
			{
			case 't':
			case 'T':
			case 'y':
			case 'Y':
			case '1':
				return true;

			case 'f':
			case 'F':
			case 'n':
			case 'N':
			case '0':
				return false;

			default:
				FAILURE_2(Configuration, "Illegal boolean value (%s) for %s",
					pValues[i].value, n);
			}
		}
	}

	return def;
}

/**
 * Return a numeric value, if the name of the value can't be found in
 * our list of values, return def.
 **/
u64 CConfigurator::get_num_value(const char* n, bool decimal, u64 def)
{
	int i;
	u64 multiplier = decimal ? 1000 : 1024;
	for (i = 0; i < iNumValues; i++)
	{
		if (!strcmp(pValues[i].name, n))
		{
			u64     retval = 0;
			u64     partval = 0;
			char* val = pValues[i].value;
			int     j = 0;
			for (;;)
			{
				while (val[j] && strchr("0123456789", val[j]))
				{
					partval *= 10;
					partval += val[j] - '0';
					j++;
				}

				switch (val[j])
				{
				case 'T':
					partval *= multiplier;
					[[fallthrough]];
				case 'G':
					partval *= multiplier;
					[[fallthrough]];
				case 'M':
					partval *= multiplier;
					[[fallthrough]];
				case 'K':
					retval += partval * multiplier;
					partval = 0;
					j++;
					break;

				case '\0':
					retval += partval;
					return retval;

				default:
					FAILURE_2(Configuration, "Illegal numeric value (%s) for %s",
						pValues[i].value, n);
				}
			}
		}
	}

	return def;
}

// THIS IS WHERE THINGS GET COMPLICATED...
#define NO_FLAGS  0

#define IS_CS     1
#define ON_CS     2

#define HAS_PCI   4
#define IS_PCI    8

#define HAS_ISA   16
#define IS_ISA    32

#define HAS_DISK  64
#define IS_DISK   128

#define IS_GUI    256
#define ON_GUI    512

#define IS_NIC    1024

#define N_P       2048  // no parent
typedef struct
{
	const char* name;
	classid id;
	int     flags;
	const char* const* known_values;  // values the class reads; others warn
} classinfo;

// Configuration values each device class actually reads. Anything else found
// in a device's config section is ignored by the code, so initialize() warns
// the user to remove it.
static const char* const kv_none[] = { 0 };
static const char* const kv_tsunami[] = {
  "memory.bits", "rom.srm", "rom.flash", "rom.dpr", "rom.toy", "time",
  "arc_year_compat", "exit_on_pal_halt", "graphics.multihead", "debug.stop_on_decode_conflict",
  "debug.shared_read_policy", "debug.shared_read_claimant", 0 };
static const char* const kv_ev68cb[] = { "speed", "palcode.vms.nohle", 0 };
static const char* const kv_serial[] = {
  "port", "listen_address", "action", "disabled", "raw_mode", "null_attach", 0 };
static const char* const kv_ali[] = { "vga_console", "lpt.outfile", 0 };
static const char* const kv_ali_ide[] = { "dma", 0 };
static const char* const kv_vga[] = { "rom", 0 };
static const char* const kv_gloria[] = {
  "rom", "rom_layout", "subsystem_from_rom", "vendor", "device", "revision",
  "subsystem_vendor", "subsystem_device", "chip_config", "mem_control",
  "mem_config", "fifo_entries", "service_budget", "scanline_access_divisor",
  "allow_dma", "trace", "trace_apertures", "frame_file", 0 };
static const char* const kv_radeon[] = {
  "rom", "rom_layout", "vram", "revision", "subsystem_vendor",
  "subsystem_device", "strict_mmio", "scanline_access_divisor", "trace",
  "trace_apertures", "frame_file", 0 };
static const char* const kv_powerstorm[] = {
  "rom", "rom_layout", "model", "vga_enabled", "revision", "subsystem_vendor",
  "subsystem_device", "bar0_size", "bar1_size", "trace", "trace_apertures",
  "frame_file", 0 };
static const char* const kv_lsi53c1020[] = { "flash", "rom", "firmware", 0 };
static const char* const kv_dec21143[] = {
  "adapter", "mac", "queue", "crc", "trace_packets",
  "type", "autonegotiate_delay", "drop_privileges", 0 };
static const char* const kv_disk_file[] = {
  "file", "model_number", "serial_number", "serial_num", "rev_number",
  "rev_num", "read_only", "cdrom", "autocreate_size", 0 };
static const char* const kv_disk_device[] = {
  "device", "model_number", "serial_number", "serial_num", "rev_number",
  "rev_num", "read_only", "cdrom", 0 };
static const char* const kv_disk_ram[] = {
  "size", "file", "model_number", "serial_number", "serial_num", "rev_number",
  "rev_num", "read_only", "cdrom", 0 };
static const char* const kv_gui_sdl[] = {
  "keyboard.use_mapping", "keyboard.map", "mouse.speed", "mouse.invert_x",
  "mouse.invert_y", "mouse.absolute", "video.linear", "video.scale_ratio",
  "video.scale_change_enable", "hotkey.mouse_capture", "hotkey.media",
  "hotkey.ctrl_alt_delete", "hotkey.reset_window", "hotkey.scale_up",
  "hotkey.scale_down", 0 };
static const char* const kv_gui_x11[] = {
  "keyboard.use_mapping", "keyboard.map", "private_colormap", 0 };
static const char* const kv_mpu401[] = { "midi_out", 0 };

classinfo classes[] = {
  {"tsunami", c_tsunami, N_P | IS_CS | HAS_PCI, kv_tsunami},
  {"ev68cb", c_ev68cb, ON_CS, kv_ev68cb},
  {"ali", c_ali, IS_PCI | HAS_ISA, kv_ali},
  {"ali_ide", c_ali_ide, IS_PCI | HAS_DISK, kv_ali_ide},
  {"ali_usb", c_ali_usb, IS_PCI, kv_none},
  {"ali_pmu", c_ali_pmu, IS_PCI, kv_none},
  {"serial", c_serial, ON_CS, kv_serial},
  {"s3", c_s3, IS_PCI | ON_GUI, kv_vga},
  {"gloria", c_gloria, IS_PCI | ON_GUI, kv_gloria},
  {"radeon", c_radeon, IS_PCI | ON_GUI, kv_radeon},
  {"powerstorm", c_powerstorm, IS_PCI | ON_GUI, kv_powerstorm},
  //{"cirrus", c_cirrus, IS_PCI | ON_GUI, kv_vga},
  {"dec21143", c_dec21143, IS_PCI | IS_NIC, kv_dec21143},
  {"lsi53c1020", c_lsi53c1020, IS_PCI | HAS_DISK, kv_lsi53c1020},
  {"sym53c895", c_sym53c895, IS_PCI | HAS_DISK, kv_none},
  {"sym53c810", c_sym53c810, IS_PCI | HAS_DISK, kv_none},
  {"floppy", c_floppy, ON_CS | HAS_DISK, kv_none},
  {"file", c_file, IS_DISK, kv_disk_file},
  {"device", c_device, IS_DISK, kv_disk_device},
  {"ramdisk", c_ramdisk, IS_DISK, kv_disk_ram},
  {"sdl", c_sdl, N_P | IS_GUI, kv_gui_sdl},
  {"win32", c_sdl, N_P | IS_GUI, kv_gui_sdl},
  {"X11", c_x11, N_P | IS_GUI, kv_gui_x11},
  {"mpu401", c_mpu401, ON_CS, kv_mpu401},
  {"es1370", c_es1370, IS_PCI, kv_none},
  {0, c_none, 0, 0}
};

/**
 * Determine what device this configurator represents, and instantiate it;
 * then call initialize for our children.
 **/
void CConfigurator::initialize()
{
	myClassId = c_none;

	int     i = 0;
	int     pcibus = 0;
	int     pcidev = 0;
	int     idedev = 0;
	int     idebus = 0;
	int     fdcbus = 0;
	int     number;
	char* pt;

	for (i = 0; classes[i].id != c_none; i++)
	{
		if (!strcmp(myValue, classes[i].name))
		{
			myClassId = classes[i].id;
			myFlags = classes[i].flags;
			break;
		}
	}

	if (myClassId == c_none)
		FAILURE_2(Configuration, "Class %s for %s not known", myValue, myName);

	// Warn about configuration values this device class does not read;
	// they have no effect and should be removed from the config file.
	if (classes[i].known_values)
	{
		for (int v = 0; v < iNumValues; v++)
		{
			bool recognized = false;
			for (const char* const* k = classes[i].known_values; *k; k++)
			{
				if (!strcmp(pValues[v].name, *k))
				{
					recognized = true;
					break;
				}
			}

			if (!recognized)
				printf("%%SYS-W-UNKNOWNCFG: %s(%s): \"%s\" is not a recognized "
					"configuration value for this device; it has been ignored "
					"and should be removed from the configuration file.\n",
					myName, myValue, pValues[v].name);
		}
	}

	if (myFlags & N_P)
	{
		if (pParent->get_flags())
			FAILURE_2(Configuration, "Class %s for %s needs a parent", myValue, myName);
	}

	if (myFlags & ON_CS)
	{
		if (!(pParent->get_flags() & IS_CS))
			FAILURE_2(Configuration, "Class %s for %s needs a chipset parent",
				myValue, myName);
	}

	if (myFlags & ON_GUI)
	{
#ifndef CONFIGURATION_ONLY
		if (!bx_gui)
			FAILURE_2(Configuration, "Class %s for %s needs a GUI", myValue, myName);
#endif
	}

	if (myFlags & IS_GUI)
	{
#ifndef CONFIGURATION_ONLY
		if (bx_gui)
			FAILURE_2(Configuration, "Class %s for %s already found a gui", myValue,
				myName);
#endif
	}

#if !defined(HAVE_PCAP) && !defined(HAVE_TAP_NET)
	if (myFlags & IS_NIC)
		FAILURE_2(Configuration,
			"Class %s for %s needs compilation with libpcap or TAP support", myValue,
			myName);
#endif
	if (myFlags & IS_PCI)
	{
		if (strncmp(myName, "pci", 3))
			FAILURE_2(Configuration,
				"Name %s for class %s should be pci<bus>.<device>", myName,
				myValue);
		if (!(pParent->get_flags() & HAS_PCI))
		{
			FAILURE_2(Configuration,
				"Class %s for %s should have a pci-bus capable parent device", myValue,
				myName);
		}

		pt = &myName[3];
		pcibus = atoi(pt);
		pt = strchr(pt, '.');
		if (!pt)
			FAILURE_2(Configuration,
				"Name %s for class %s should be pci<bus>.<device>", myName,
				myValue);
		pt++;
		pcidev = atoi(pt);

		// Validate PCI slot assignments. On the Tsunami chipset, certain
		// pci0 slots are reserved for system-internal devices. Placing an
		// add-in device on one of those slots causes the SRM firmware to
		// malfunction (e.g. SCSI disks not detected, device conflicts).
		if (pcibus == 0)
		{
			bool is_system_device = (myClassId == c_ali
				|| myClassId == c_ali_ide
				|| myClassId == c_ali_usb
				|| myClassId == c_ali_pmu);
			if (!is_system_device)
			{
				if (pcidev == 0)
					FAILURE_2(Configuration,
						"%s (%s): PCI slot pci0.0 is reserved and cannot be "
						"used for add-in devices. Use pci0.1 through pci0.4",
						myName, myValue);
				if (pcidev == 7 || pcidev == 15 || pcidev == 17 || pcidev == 19)
					FAILURE_3(Configuration,
						"%s (%s): PCI slot pci0.%d is reserved for a "
						"system-internal device. Use pci0.1 through pci0.4 "
						"for add-in devices",
						myName, myValue, pcidev);
			}
		}
	}

	if (myClassId == c_floppy)
	{
		if (strncmp(myName, "fdc", 3))
			FAILURE_2(Configuration,
				"Name %s for class %s should be fdc<bus>", myName,
				myValue);

		pt = &myName[3];
		fdcbus = atoi(pt);
	}

	if (myFlags & IS_DISK)
	{
		if (strncmp(myName, "disk", 4))
			FAILURE_2(Configuration,
				"Name %s for class %s should be disk<bus>.<device>", myName,
				myValue);
		if (!(pParent->get_flags() & HAS_DISK))
		{
			FAILURE_2(Configuration,
				"Class %s for %s should have a disk-controller parent device", myValue,
				myName);
		}

		pt = &myName[4];
		idebus = atoi(pt);
		pt = strchr(pt, '.');
		if (!pt)
			FAILURE_2(Configuration,
				"Name %s for class %s should be disk<bus>.<device>", myName,
				myValue);
		pt++;
		idedev = atoi(pt);
	}

#ifndef CONFIGURATION_ONLY
	switch (myClassId)
	{
	case c_tsunami:
		myDevice = new CSystem(this);
		new CDPR(this, (CSystem*)myDevice);
		new CFlash(this, (CSystem*)myDevice);
		break;

	case c_ev68cb:
		myDevice = new CAlphaCPU(this, (CSystem*)pParent->get_device());
		break;

	case c_ali:
		myDevice = new CAliM1543C(this, (CSystem*)pParent->get_device(), pcibus,
			pcidev);
		new CPort80(this, (CSystem*)pParent->get_device());
		new CKeyboard(this, (CSystem*)pParent->get_device());
		new CDMA(this, (CSystem*)pParent->get_device(),
			*static_cast<CAliM1543C*>(myDevice));
		break;

	case c_floppy:
		/* For disk controllers, myDevice points to the
		 * CDiskController part of the class as it's used
		 * to register disks to.
		 */
		myDevice = (CDiskController*) new CFloppyController(this, (CSystem*)pParent->get_device(), fdcbus);
		break;

	case c_ali_ide:
		/* For disk controllers, myDevice points to the
		 * CDiskController part of the class as it's used
		 * to register disks to.
		 */
		myDevice = (CDiskController*) new CAliM1543C_ide(this, (CSystem*)pParent->get_device(),
			pcibus, pcidev);
		break;

	case c_ali_usb:
		myDevice = new CAliM1543C_usb(this, (CSystem*)pParent->get_device(),
			pcibus, pcidev);
		break;

#ifdef _WIN32
	case c_mpu401:
		myDevice = (void*)new CMPU401(this, (CSystem*)pParent->get_device());
		break;
#endif

	case c_ali_pmu:
		myDevice = new CAliM1543C_pmu(this, (CSystem*)pParent->get_device(),
			pcibus, pcidev);
		break;

	case c_s3:
		myDevice = new CS3Trio64(this, (CSystem*)pParent->get_device(), pcibus,
			pcidev, bx_gui->display_for_output(get_device_path(), 0));
		break;

	case c_gloria:
		myDevice = new CGloriaSynergy(this, (CSystem*)pParent->get_device(), pcibus,
			pcidev, bx_gui->display_for_output(get_device_path(), 0));
		break;

	case c_radeon:
		myDevice = new CRadeon7500(this, (CSystem*)pParent->get_device(), pcibus,
			pcidev, bx_gui->display_for_output(get_device_path(), 0));
		break;

	case c_powerstorm:
		myDevice = new CPowerStorm3xx(this, (CSystem*)pParent->get_device(), pcibus,
			pcidev, bx_gui->display_for_output(get_device_path(), 0));
		break;

		// i broke this, my bad. To be restored in the future. 
//	case c_cirrus:
//		myDevice = new CCirrus(this, (CSystem*)pParent->get_device(), pcibus,
//			pcidev);
//		break;
#if defined(HAVE_SDL)
	case c_es1370:
		myDevice = new CES1370(this, (CSystem*)pParent->get_device(), pcibus,
			pcidev);
		break;
#endif

#if defined(HAVE_PCAP) || defined(HAVE_TAP_NET)

	case c_dec21143:
		myDevice = new CDEC21143(this, (CSystem*)pParent->get_device(), pcibus,
			pcidev);
		break;
#endif

	case c_lsi53c1020:
		/* For disk controllers, myDevice points to the
		 * CDiskController part of the class as it's used
		 * to register disks to.
		 */
		myDevice = (CDiskController*) new CLSI53C1020(this,
			(CSystem*)pParent->get_device(), pcibus, pcidev);
		break;

	case c_sym53c895:
		/* For disk controllers, myDevice points to the
		 * CDiskController part of the class as it's used
		 * to register disks to.
		 */
		myDevice = (CDiskController*) new CSym53C895(this, (CSystem*)pParent->get_device(), pcibus,
			pcidev);
		break;

	case c_sym53c810:
		/* For disk controllers, myDevice points to the
		 * CDiskController part of the class as it's used
		 * to register disks to.
		 */
		myDevice = (CDiskController*) new CSym53C810(this, (CSystem*)pParent->get_device(), pcibus,
			pcidev);
		break;

	case c_file:
		myDevice = new CDiskFile(this, theSystem,
			(CDiskController*)pParent->get_device(), idebus,
			idedev);
		break;

	case c_device:
		myDevice = new CDiskDevice(this, theSystem,
			(CDiskController*)pParent->get_device(),
			idebus, idedev);
		break;

	case c_ramdisk:
		myDevice = new CDiskRam(this, theSystem,
			(CDiskController*)pParent->get_device(), idebus,
			idedev);
		break;

	case c_serial:
		number = 0;
		if (!strncmp(myName, "serial", 6))
		{
			pt = &myName[6];
			number = atoi(pt);
		}

		myDevice = new CSerial(this, (CSystem*)pParent->get_device(), number);
		break;

	case c_sdl:
#if defined(HAVE_SDL)
		PLUG_load_plugin(this, sdl);
#else
		FAILURE_2(Configuration,
			"Class %s for %s needs compilation with SDL support", myValue,
			myName);
#endif
		break;

	case c_win32:
#if defined(_WIN32)
		PLUG_load_plugin(this, win32);
#else
		FAILURE_2(Configuration, "Class %s for %s needs a Win32 platform", myValue,
			myName);
#endif
		break;

	case c_x11:
#if defined(HAVE_X11)
		PLUG_load_plugin(this, x11);
#else
		FAILURE_2(Configuration, "Class %s for %s needs an X11 platform", myValue,
			myName);
#endif
		break;

	case c_none:
		break;

	default:
		FAILURE_1(Configuration, "Enum case not handled: %i", myClassId);
		break;
	}

	// The ES40 has a built-in 3.5-inch 1.44 MB drive A.
	if (myClassId == c_floppy && fdcbus == 0)
	{
		bool have_drive_a = false;
		for (i = 0; i < iNumChildren; i++)
		{
			int disk_bus;
			int disk_device;
			char trailing;
			if (sscanf(pChildren[i]->get_myName(), "disk%d.%d%c",
				&disk_bus, &disk_device, &trailing) == 2 &&
				disk_bus == 0 && disk_device == 0)
			{
				have_drive_a = true;
				break;
			}
		}

		if (!have_drive_a)
		{
			if (iNumChildren >= CFG_MAX_CHILDREN)
				FAILURE(Configuration,
					"No room to add the built-in floppy drive A");

			printf("%%SYS-I-DEFAULTFLOPPY: disk0.0 is not configured; "
				"adding the built-in 3.5-inch 1.44 MB drive with no media.\n");

			char* dname;
			CHECK_ALLOCATION(dname = (char*)malloc(8));
			strcpy(dname, "disk0.0");
			char* dvalue;
			CHECK_ALLOCATION(dvalue = (char*)malloc(5));
			strcpy(dvalue, "file");
			char dtext[] = "";
			CConfigurator* drive_a = new CConfigurator(this, dname, dvalue,
				dtext, 0);
			for (i = iNumChildren; i > 0; i--)
				pChildren[i] = pChildren[i - 1];
			pChildren[0] = drive_a;
			iNumChildren++;
		}
	}

	// Synthesize mandatory system hardware omitted from the configuration.
	// Missing serial ports use null_attach (bit-bucket) mode; the IDE and
	// floppy controllers are chipset integrated and always exist.
	if (myFlags & IS_CS)
	{
		bool have_serial[2] = { false, false };
		bool have_ide = false;
		bool have_fdc0 = false;
		for (i = 0; i < iNumChildren; i++)
		{
			if (!strcmp(pChildren[i]->get_myValue(), "serial"))
			{
				number = 0;
				if (!strncmp(pChildren[i]->get_myName(), "serial", 6))
					number = atoi(&pChildren[i]->get_myName()[6]);
				if (number >= 0 && number < 2)
					have_serial[number] = true;
			}
			else if (!strcmp(pChildren[i]->get_myValue(), "ali_ide"))
			{
				have_ide = true;
			}
			else if (!strcmp(pChildren[i]->get_myValue(), "floppy") &&
				!strncmp(pChildren[i]->get_myName(), "fdc", 3) &&
				atoi(&pChildren[i]->get_myName()[3]) == 0)
			{
				have_fdc0 = true;
			}
		}

		if (!have_ide)
		{
			if (iNumChildren >= CFG_MAX_CHILDREN)
				FAILURE(Configuration,
					"No room to add the default IDE controller configuration");

			printf("%%SYS-I-DEFAULTIDE: IDE controller is not configured; "
				"adding the built-in controller at pci0.15 with no drives.\n");

			char* ide_name;
			CHECK_ALLOCATION(ide_name = (char*)malloc(9));
			strcpy(ide_name, "pci0.15");
			char* ide_value;
			CHECK_ALLOCATION(ide_value = (char*)malloc(8));
			strcpy(ide_value, "ali_ide");
			char ide_text[] = "";
			pChildren[iNumChildren++] = new CConfigurator(this, ide_name,
				ide_value, ide_text, 0);
		}

		if (!have_fdc0)
		{
			if (iNumChildren >= CFG_MAX_CHILDREN)
				FAILURE(Configuration,
					"No room to add the default fdc0 configuration");

			printf("%%SYS-I-DEFAULTFDC: fdc0 is not configured; "
				"adding the built-in floppy controller.\n");

			char* fname;
			CHECK_ALLOCATION(fname = (char*)malloc(5));
			strcpy(fname, "fdc0");
			char* fvalue;
			CHECK_ALLOCATION(fvalue = (char*)malloc(7));
			strcpy(fvalue, "floppy");
			char ftext[] = "";
			pChildren[iNumChildren++] = new CConfigurator(this, fname, fvalue,
				ftext, 0);
		}

		for (number = 0; number < 2; number++)
		{
			if (have_serial[number])
				continue;

			if (iNumChildren >= CFG_MAX_CHILDREN)
				FAILURE_1(Configuration,
					"No room to add default configuration for serial%d", number);

			printf("%%SYS-W-NOSERIAL: serial%d is not configured; "
				"defaulting to null_attach mode.\n", number);

			char* sname = (char*)malloc(8);
			sprintf(sname, "serial%d", number);
			char* svalue = (char*)malloc(7);
			strcpy(svalue, "serial");
			char stext[] = "null_attach=true;";
			pChildren[iNumChildren++] = new CConfigurator(this, sname, svalue,
				stext, strlen(stext));
		}
	}

	for (i = 0; i < iNumChildren; i++)
		pChildren[i]->initialize();

	if (myFlags & IS_CS)
		((CSystem*)myDevice)->init();
#endif // !CONFIGURATION_ONLY
}

#ifdef CONFIGURATION_ONLY
  /**
   * Constructor
   * 
   * Create an empty Configurator object, only setting name and value,
   * and linking it to its parent.
   **/
CConfigurator::CConfigurator(class CConfigurator* parent, char* name, char* value)
: CConfigurator(parent, strdup(name), strdup(value), strdup(""), 0)
{
	// The base constructor assigns these only when parent == 0.
	if (parent != 0)
	{
		pParent = parent;
		iNumChildren = 0;
		iNumValues = 0;
		myName = strdup(name);
		myValue = strdup(value);
	}

	parent->set_child(this);
}

/**
 * Set a value in our list of values.
 * If a value with this name does not exist, add it.
 **/
void CConfigurator::set_value(char *name, char *value)
{
	for (int i = 0; i < iNumValues; ++i)
		if (!strcmp(pValues[i].name, name))
		{
			pValues[i].value = value;
			return;
		}
	add_value(name, value);
}

  /**
   * Remove a value with the given name.
   **/
void CConfigurator::remove_value(char *name)
{
	for (int i = 0; i < iNumValues; ++i)
		if (!strcmp(pValues[i].name, name))
		{
			for (int j = i; j < iNumValues - 1; ++j)
				pValues[j] = pValues[j + 1];
			--iNumValues;
		}
}

  /**
   * Set a child in our list of children.
   * If a child with this name does not exist, add it.
   **/
void CConfigurator::set_child(CConfigurator *child)
{
	if (child->pParent != this)
		FAILURE(Configuration, "Child already has a parent");

	for (int i = 0; i < iNumChildren; ++i)
		if (!strcmp(pChildren[i]->myName, child->myName))
		{
			pChildren[i] = child;
			return;
		}

	if (iNumChildren >= CFG_MAX_CHILDREN)
		FAILURE(Configuration, "No more children can be added");
	pChildren[iNumChildren++] = child;
}

  /**
   * Find a child with the given name (recursively).
   * Returns a pointer to the child, or nullptr if not found.
   **/
CConfigurator *CConfigurator::find_child(const char *name)
{
	if (myName != NULL && !strcmp(myName, name))
		return this;
	for (int i = 0; i < iNumChildren; ++i)
	{
		CConfigurator *c = pChildren[i]->find_child(name);
		if (c != nullptr)
			return c;
	}
		
    return nullptr;
}

  /**
   * Find the first (direct) child with the given myValue.
   * Returns a pointer to the child, or nullptr if not found.
   **/
CConfigurator *CConfigurator::find_child_myValue(const char *myValue)
{
	for (int i = 0; i < iNumChildren; ++i)
		if (!strcmp(pChildren[i]->myValue, myValue))
			return pChildren[i];
    return nullptr;
}

  /**
   * Remove a child with the given name.
   **/
void CConfigurator::remove_child(const char *name)
{
	for (int i = 0; i < iNumChildren; ++i)
		if (!strcmp(pChildren[i]->myName, name))
		{
			pChildren[i]->remove_all_children();
			for (int j = i; j < iNumChildren - 1; ++j)
				pChildren[j] = pChildren[j + 1];
			--iNumChildren;
		}
}

  /**
   * Remove all children.
   */
void CConfigurator::remove_all_children(void)
{
	for (int i = iNumChildren - 1; i >= 0; --i)
		remove_child(pChildren[i]->myName);
}

  /**
   * Write the configuration to a file, indenting the levels.
   **/
void CConfigurator::write_configuration(FILE *fp, int indent)
{
	char indent_str[80];

	if (indent == 0)
		indent_str[0] = '\0';
	else
		snprintf(indent_str, sizeof(indent_str), "%*c", indent, ' ');

	if (myName != NULL)
	{
		fprintf(fp, "%s%s = %s\n%s{\n", indent_str, myName, myValue, indent_str);
		indent += 2;
	}

	for (int i = 0; i < iNumValues; ++i)
		fprintf(fp, "%s  %s = \"%s\";\n", indent_str, pValues[i].name, pValues[i].value);
	for (int i = 0; i < iNumChildren; ++i)
		pChildren[i]->write_configuration(fp, indent);
	if (myName != NULL)
		fprintf(fp, "%s}\n", indent_str);
}
#endif 

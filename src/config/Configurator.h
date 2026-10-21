/* ES40 Emulator.
 * Copyright (C) 2007-2008 by the ES40 Emulator Project
 * Copyright (C) 2026 by gdwnldsKSC
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
  * Contains the definitions for the configuration file interpreter.
  *
  * $Id$
  *
  * X-1.17       Camiel Vanderhoeven                             29-APR-2008
  *      Added floppy configuration.
  *
  * X-1.16       Camiel Vanderhoeven                             26-MAR-2008
  *      Fix compiler warnings.
  *
  * X-1.15       Camiel Vanderhoeven                             14-MAR-2008
  *      Formatting.
  *
  * X-1.14       Camiel Vanderhoeven                             13-MAR-2008
  *      Create init() start_threads() and stop_threads() functions.
  *
  * X-1.13       Camiel Vanderhoeven                             05-MAR-2008
  *      Multi-threading version.
  *
  * X-1.12       Pepito Grillo                                   02-MAR-2008
  *      Avoid compiler warnings.
  *
  * X-1.11       Camiel Vanderhoeven                             02-MAR-2008
  *      Natural way to specify large numeric values ("10M") in the config file.
  *
  * X-1.10       Camiel Vanderhoeven                             26-FEB-2008
  *      Moved DMA code into it's own class (CDMA)
  *
  * X-1.9        Camiel Vanderhoeven                             16-FEB-2008
  *      Added Symbios 53C810 controller.
  *
  * X-1.8        Camiel Vanderhoeven                             20-JAN-2008
  *      Added X11 GUI.
  *
  * X-1.7        Camiel Vanderhoeven                             19-JAN-2008
  *      Added win32 GUI.
  *
  * X-1.6        Camiel Vanderhoeven                             05-JAN-2008
  *      Added CDiskDevice class.
  *
  * X-1.5        Camiel Vanderhoeven                             02-JAN-2008
  *      Better handling of configuration errors.
  *
  * X-1.4        Camiel Vanderhoeven                             28-DEC-2007
  *      Keep the compiler happy.
  *
  * X-1.3        Camiel Vanderhoeven                             14-DEC-2007
  *      Add support for Symbios SCSI controller.
  *
  * X-1.2        Camiel Vanderhoeven                             12-DEC-2007
  *      Add support for file- and RAM-disk.
  *
  * X-1.1        Camiel Vanderhoeven                             10-DEC-2007
  *      Initial version in CVS.
  **/
#if !defined(__CONFIGURATOR_H__)
#define __CONFIGURATOR_H__

#include <string>
#include <vector>

#define CFG_MAX_CHILDREN  25
#define CFG_MAX_VALUES    50

typedef enum
{
  c_none,

  // chipsets
  c_tsunami,

  // system devices
  c_ev68cb,
  c_serial,
  c_floppy,

  // pci devices
  c_ali,
  c_ali_ide,
  c_ali_usb,
  c_ali_pmu,
  c_s3,
  c_gloria,
  c_cirrus,
  c_radeon,
  c_powerstorm,
  c_dec21143,
  c_lsi53c1020,
  c_sym53c895,
  c_sym53c810,
  c_mpu401,
  c_es1370,

  // disk devices
  c_file,
  c_device,
  c_ramdisk,

  // gui's
  c_sdl,
  c_win32,
  c_x11
} classid;

class CConfigurator
{
public:
  std::string get_device_path() const
  {
    std::string path = pParent ? pParent->get_device_path() : std::string();
    const std::string name = myName ? myName : "";
    const std::string value = myValue ? myValue : "";
    return path + "/" + std::to_string(name.size()) + ":" + name +
      "=" + std::to_string(value.size()) + ":" + value;
  }

  CConfigurator(class CConfigurator* parent, char* name, char* value,
    char* text, size_t textlen);
  ~CConfigurator(void);

  char* strip_string(char* c);
  void    add_value(char* n, char* v);

  char* get_text_value(const char* n) { return get_text_value(n, (char*)0); };
  char* get_text_value(const char* n, const char* def);

  bool    get_bool_value(const char* n) { return get_bool_value(n, false); };
  bool    get_bool_value(const char* n, bool def);

  u64 get_num_value(const char* n, bool decimal_suffixes)
  {
    return get_num_value(n, decimal_suffixes, 0);
  };
  u64             get_num_value(const char* n, bool decimal_suffixes, u64 def);

  classid         get_class_id() { return myClassId; };
  void* get_device() { return myDevice; };
  int             get_flags() { return myFlags; };

  char* get_myName() { return myName; };
  char* get_myValue() { return myValue; };
  CConfigurator* get_myParent() { return pParent; };

  void            initialize();

#ifdef CONFIGURATION_ONLY
  CConfigurator(class CConfigurator* parent, char* name, char* value);
  void set_myName(char *name) { myName = strdup(name); };
  void set_myValue(const char *value) { myValue = strdup(value); }
  int editor_child_count() const { return iNumChildren; }
  CConfigurator *editor_child(int i) const { return pChildren[i]; }
  int editor_value_count() const { return iNumValues; }
  const char *editor_value_name(int i) const { return pValues[i].name; }
  const char *editor_value(int i) const { return pValues[i].value; }
  void set_value(char* name, char* value);
  void remove_value(char *name);
  void set_child(class CConfigurator *child);
  CConfigurator *find_child(const char *name);
  CConfigurator *find_child_myValue(const char *myValue);
  std::vector<CConfigurator *> find_children_myValue(const char *myValue);
  void remove_child(const char *name);
  void remove_all_children(void);
  void write_configuration(FILE *fp, int indent = 0);
#endif
private:
  class CConfigurator* pParent;
  class CConfigurator* pChildren[CFG_MAX_CHILDREN];
  int                   iNumChildren;
  char* myName;
  char* myValue;
  void* myDevice;
  classid               myClassId;
  int                   myFlags;
  int                   iNumValues;
  struct SCfg_Value
  {
    char* name;
    char* value;
  } pValues[CFG_MAX_VALUES];
};
#endif //!defined(__CONFIGURATOR_H__)

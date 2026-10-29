/* ES40 Emulator.
 * Copyright (C) 2007-2008 by the ES40 Emulator Project
 * Copyright (C) 2026 by Martin Vorländer
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

// Shared configuration editor: option definitions, validation and workflows.
#include "StdAfx.h"
#include "ConfigEditor.h"
#include "ConfigDocument.h"
#include <optional>
#include <set>
#include <fcntl.h>
#ifdef _WIN32
#include <io.h>
#endif
#include "Configurator.h"
#include "io/SerialListenAddress.h"
#include <cmath>
#include <regex>
#ifdef HAVE_PCAP
#include <pcap/pcap.h>
#endif
using namespace std;

#define ARRAY_SIZE(a) (int)(sizeof(a) / sizeof(a[0]))

// Maximum number of CPUs
#define MAX_CPUS 4

// Low and high values for memory.bits parameter
#define LOW_MEM_BITS 25 // 32MB
#define HI_MEM_BITS 35  // 32GB

// PCI bus ranges
const int MAX_PCI_BUS = 2;
const int MAX_PCI_SLOT[MAX_PCI_BUS] = {4, 6};

// Internal PCI device slot names, plus a placeholder one
#define PCI_SLOT_ALI "pci0.7"
#define PCI_SLOT_IDE "pci0.15"
#define PCI_SLOT_PMU "pci0.17"
#define PCI_SLOT_USB "pci0.19"
#define PCI_SLOT_TEMP "pci99.99"

// Forms: Strings for negative and affirmative answer
#define STR_NO "no"   // "no" , "false", "0"
#define STR_YES "yes" // "yes", "true" , "1"

// Preferred terminal positions (ignored by graphical frontends)
#define MENU_1ST_LEVEL 1, 4
#define MENU_2ND_LEVEL 4, 7
#define MENU_3RD_LEVEL 7, 10
#define MENU_4TH_LEVEL 10, 13

#define MYKEY_HELP_STR "F1"
#define MYKEY_EXIT_STR "F2"
#define MYKEY_QUIT_STR "F3"
static EditorUI *editor_ui;
static std::unique_ptr<ConfigDocument> configuration_document;
static std::optional<ConfigFileSnapshot> input_snapshot, output_snapshot;
static std::vector<bool> changed_fields;
static std::set<CConfigurator *> existing_form_nodes;

static void remember_form_nodes(CConfigurator *node)
{
    existing_form_nodes.insert(node);
    for (int i = 0; i < node->editor_child_count(); ++i) remember_form_nodes(node->editor_child(i));
}
static void set_form_value(CConfigurator *node, int field, const char *name,
                           const char *value, bool force = false)
{
    if (force || !existing_form_nodes.count(node) || changed_fields.at(field))
        node->set_value(strdup(name), strdup(value));
}
CConfigurator *theConfig = nullptr, *sys0 = nullptr;

void show_text(const char *title, const char *text)
{ if (text) editor_ui->text(title, text); }
int show_menu(const char *title, MenuEntry_t entry[], int count, int y, int x)
{ return editor_ui->menu(title, entry, count, y, x); }

static void editor_choices(EditorField *field, const char *const *choices)
{
    field->type = EditorField::Type::Choice;
    for (int i = 0; choices[i]; ++i) field->choices.emplace_back(choices[i]);
}
static void editor_pattern(EditorField *field, const char *pattern)
{ field->type = EditorField::Type::Pattern; field->pattern = pattern; }
static void editor_number(EditorField *field, EditorField::Type type, int precision, double low, double high)
{ field->type = type; field->precision = precision; field->minimum = low; field->maximum = high; }

EditorField prepare_editor_field(const FormEntry_t& entry, const char *preset)
{
    EditorField field;
    field.value = preset ? preset : (entry.preset ? entry.preset : "");
    if (entry.validation_callback) entry.validation_callback(&field);
    if (field.type == EditorField::Type::Choice && field.choices == vector<string>{STR_NO, STR_YES})
    {
        // Unify boolean values read from config file
        if (!strcasecmp(field.value.c_str(), "no") || !strcasecmp(field.value.c_str(), "false") || field.value == "0")
            field.value = STR_NO;
        if (!strcasecmp(field.value.c_str(), "yes") || !strcasecmp(field.value.c_str(), "true") || field.value == "1")
            field.value = STR_YES;
    }
    return field;
}

bool validate_editor_field(EditorField& field)
{
    const string& value = field.value;
    switch (field.type)
    {
    case EditorField::Type::Choice:
        for (const auto& choice : field.choices)
            if (!strcasecmp(value.c_str(), choice.c_str())) { field.value = choice; return true; }
        return false;
    case EditorField::Type::Integer:
    case EditorField::Type::Number:
    {
        if (value.empty()) return false;
        char *end;
        errno = 0;
        double number = strtod(value.c_str(), &end);
        if (end == value.c_str() || *end || errno || !isfinite(number)) return false;
        if (field.type == EditorField::Type::Integer && value.find_first_not_of("+-0123456789") != string::npos) return false;
        return number >= field.minimum && number <= field.maximum;
    }
    case EditorField::Type::Pattern:
        return regex_match(value, regex(field.pattern, regex::extended));
    default: return true;
    }
}

FormValues_t show_form(const char *title, FormEntry_t entry[], FormValues_t preset,
                       int count, FormCheckFuncPtr check)
{
    existing_form_nodes.clear(); remember_form_nodes(theConfig);
    vector<EditorField> fields;
    for (int i = 0; i < count; ++i) fields.push_back(prepare_editor_field(entry[i], preset ? preset[i] : nullptr));
    const auto initial = fields;
    while (editor_ui->form(title, entry, fields))
    {
        bool valid = true;
        for (int i = 0; i < count; ++i)
            if (!validate_editor_field(fields[i]))
            {
                string error = string("Invalid value for ") + entry[i].label + ".";
                show_text("Error", error.c_str()); valid = false; break;
            }
        if (!valid) continue;
        auto values = (FormValues_t)calloc(count, sizeof(char *));
        for (int i = 0; i < count; ++i) values[i] = strdup(fields[i].value.c_str());
        changed_fields.assign(count, false);
        for (int i = 0; i < count; ++i)
        {
            bool same = initial[i].value == values[i];
            if (initial[i].type == EditorField::Type::Integer || initial[i].type == EditorField::Type::Number)
                same = strtod(initial[i].value.c_str(), nullptr) == strtod(values[i], nullptr);
            if (initial[i].type == EditorField::Type::Choice)
                same = !strcasecmp(initial[i].value.c_str(), values[i]);
            changed_fields[i] = !same;
        }
        try
        {
            if (!check || check(entry, count, values))
            {
                return values;
            }
        }
        catch (...) { for (int i = 0; i < count; ++i) free(values[i]); free(values); throw; }
        for (int i = 0; i < count; ++i) free(values[i]);
        free(values);
    }
    return nullptr;
}

/**
 * Convert an integer to a string.
 **/
inline string i2s(int x)
{
    // From src/NumberQuestion.h
    ostringstream o;
    o << x;
    return o.str();
}

/**
 * Convert a string to an integer.
 *
 * Throws a CLogicException when the input is not numeric.
 **/
inline int s2i(const string x)
{
    // From src/NumberQuestion.h
    istringstream i(x);
    int x1;
    char c;
    if (!(i >> x1) || i.get(c))
        FAILURE_1(Logic, "invalid integer conversion of '%s'", x.c_str());
    return x1;
}

/**
 * Convert memory size to memory bits
 * (assumes mem_size is a power of 2)
 *
 * Throws a CLogicException when 'mem_size' does not end in 'M', 'm', 'G', or 'g'.
 **/
int memory_size2bits(char *mem_size)
{
    char unit = mem_size[strlen(mem_size) - 1];
    u64 amount = atoi(mem_size);
    int mem_bits = 0;
    switch (unit)
    {
    case 'm':
    case 'M':
        amount *= 1024 * 1024;
        break;
    case 'g':
    case 'G':
        amount *= 1024 * 1024 * 1024;
        break;
    default:
        FAILURE_1(Logic, "Unrecognized memory size: '%s'", mem_size);
        break;
    }
    while ((amount & 1) == 0 && amount != 0)
    {
        ++mem_bits;
        amount >>= 1;
    }
    return mem_bits;
}

/**
 * Convert memory bits to memory size.
 *
 * Throws a CLogicException when 'mem_bits' is not in the range LOW_MEM_BITS .. HI_MEM_BITS.
 **/
string memory_bits2size(int mem_bits)
{
    if (mem_bits < LOW_MEM_BITS || mem_bits > HI_MEM_BITS)
        FAILURE_1(Logic, "Memory bits out of range: '%d'", mem_bits);

    // from es40-cfg.cpp
    string unit = "";
    int j = mem_bits;
    if (mem_bits < 30)
    {
        // Megabyte-range.
        j -= 20;
        unit = "M";
    }
    else
    {
        // Gigabyte range.
        j -= 30;
        unit = "G";
    }
    return (i2s(1 << j) + unit);
}

/**
 * Read theConfig from a file named 'filename'.
 */
void read_configuration(const char *filename)
{
    input_snapshot = read_config_file(filename);
    configuration_document.reset(new ConfigDocument(input_snapshot->text));
    theConfig = configuration_document->configuration();
}

/**
 * Patch the original document, retaining comments and unedited source bytes.
 * Existing files are backed up and replaced atomically by the shared save layer.
 */
void write_configuration(const char *filename)
{
    const auto text = configuration_document->render(theConfig);
    if (!strcmp(filename, "-"))
    {
#ifdef _WIN32
        const int mode = _setmode(_fileno(stdout), _O_BINARY);
#endif
        bool failed = fwrite(text.data(), 1, text.size(), stdout) != text.size();
        failed = fflush(stdout) != 0 || failed;
#ifdef _WIN32
        _setmode(_fileno(stdout), mode);
#endif
        if (failed) FAILURE(Configuration, "failed to write configuration to stdout");
        return;
    }
    auto destination = output_snapshot ? *output_snapshot : read_config_file(filename, true);
    // Saving back to the loaded file must also detect changes made while editing.
    if (input_snapshot)
    {
        std::error_code ignored;
        if (input_snapshot->path == destination.path ||
            (destination.exists && std::filesystem::equivalent(input_snapshot->path, destination.path, ignored)))
        {
            destination.exists = input_snapshot->exists;
            destination.text = input_snapshot->text;
        }
    }
    save_config_file(destination, text);
    // A session that keeps editing after this save compares its next save
    // against the file it just wrote, not the one it originally loaded.
    input_snapshot = read_config_file(destination.path, true);
    output_snapshot.reset();
}

/**
 * Start a new machine: the smallest configuration the emulator accepts.
 */
void new_configuration()
{
    char minimal_config[] = "sys0 = tsunami { cpu0 = ev68cb {} }";
    theConfig = new CConfigurator(nullptr, NULL, NULL, minimal_config, strlen(minimal_config));
    configuration_document.reset(new ConfigDocument(""));
    configuration_document->bind(theConfig);
}

// Model access for frontends that edit the device tree directly instead of
// walking the menus (es40_cfg_gtk). Errors arrive as std::exception.
void editor_load(const char *filename)
{
    editor_release();
    try { read_configuration(filename); }
    catch (CException& e) { editor_release(); throw std::runtime_error(e.displayText()); }
    sys0 = theConfig->find_child("sys0");
    if (!sys0) { editor_release(); throw std::runtime_error("No 'sys0' in the configuration."); }
}
void editor_new()
{
    editor_release();
    new_configuration();
    sys0 = theConfig->find_child("sys0");
}
void editor_save(const char *filename)
{
    try { write_configuration(filename); }
    catch (CException& e) { throw std::runtime_error(e.displayText()); }
}
void editor_release()
{
    delete theConfig;
    theConfig = sys0 = nullptr;
    configuration_document.reset(); input_snapshot.reset(); output_snapshot.reset();
}

// Keep extra attributes and nested blocks when editing an existing device.
static CConfigurator *edit_node(CConfigurator *parent, CConfigurator *node,
                                const std::string& name, const char *type)
{
    if (!node) return new CConfigurator(parent, (char *)name.c_str(), (char *)type);
    node->set_myName((char *)name.c_str());
    node->set_myValue(type);
    return node;
}

void show_help(const char *title)
{
    const char *text[] = {
        "The program consists of menus and forms.",
        "",
        "In menus, the following keys can be used:",
        "<UP>, <DOWN>: go to previous/next item, wrapping around",
        "<ENTER>: select item",
        "<" MYKEY_HELP_STR ">: Display item description",
        "<" MYKEY_EXIT_STR ">: exit menu.",
        "<" MYKEY_QUIT_STR ">: quit menu.",
        "",
        "Before saving results at program exit, the consistency of data is checked.",
        "",
        "Forms contain labels and entry fields, which can have various types.",
        "The follwoing keys are recognized:",
        "<UP> / <DOWN>, <ENTER>: go to previous/next field, wrapping around",
        "<TAB>, <RIGHT> / <LEFT>: in choice fields, go to next/previous choice",
        "<" MYKEY_HELP_STR ">: Display field description",
        "<" MYKEY_EXIT_STR ">: exit form",
        "<" MYKEY_QUIT_STR ">: quit form without saving results.",
        "Any other printable character is entered in the current field.",
        "",
        "On leaving a form field, the validity of data entered into the field may be checked.",
        "On leaving a form via <" MYKEY_EXIT_STR ">, the validity of data may be checked."};
    string body;
    for (const char *line : text) { body += line; body += '\n'; }
    show_text(title, body.c_str());
}

/**
 * Combine bus, slot to a PCI slot string
 */
inline string pcislot_str(int bus, int slot)
{
    return i2s(bus) + "." + i2s(slot);
}

/**
 * Return the config object in PCI slot "<bus>.<slot>",
 * or nullptr if there's no object in that slot.
 **/
CConfigurator *get_pcislot(const char *bus_slot)
{
    return sys0->find_child((string("pci") + bus_slot).c_str());
}

/**
 * Return the config object in PCI slot "<bus>.<slot>",
 * or nullptr if there's no object in that slot.
 **/
CConfigurator *get_pcislot(int bus, int slot)
{
    return get_pcislot(pcislot_str(bus, slot).c_str());
}

/**
 * Test whether there's a config object in PCI slot "<bus>.<slot>".
 */
bool is_pcislot_free(int bus, int slot)
{
    return (get_pcislot(bus, slot) == nullptr);
}

/**
 * Return the first PCI slot "<bus>.<slot>" which does not have a config object.
 *
 * The calling procedure is responsible for freeing up the storage of the return value.
 */
char *find_first_free_pcislot(void)
{
    for (int bus = 0; bus < MAX_PCI_BUS; ++bus)
        for (int slot = 1; slot <= MAX_PCI_SLOT[bus]; ++slot)
            if (is_pcislot_free(bus, slot))
                return strdup(pcislot_str(bus, slot).c_str());
    return NULL;
}

/**
 * Return the name of the config object in PCI slot "<bus>.<slot>",
 * or "-" if there's no object in that slot.
 */
char *device_in_pcislot(int bus, int slot)
{
    string pcislot = string("pci") + pcislot_str(bus, slot);
    CConfigurator *c = sys0->find_child(pcislot.c_str());
    if (c == nullptr)
        return (char *)"-";
    else
        return c->get_myValue();
}

/**
 * General validation routines
 **/

void validation_yes(EditorField *field)
{
    const char *choices[] = {
        STR_YES,
        NULL};
    editor_choices(field, choices);
}

void validation_yes_no(EditorField *field)
{
    const char *choices[] = {
        STR_NO,
        STR_YES,
        NULL};
    editor_choices(field, choices);
}

void validation_optional_datetime(EditorField *field)
{
    // Preceeding and trailing optional blanks added for trimming the field,
    // see form_field_validation(3X) manpage
    editor_pattern(field,
                   "^ *"
                   "([0-8]{4}-[0-8]{2}-[0-8]{2})?"
                   "( [0-8]{2}:[0-8]{2}:[0-8]{2})?"
                   " *$");
}

void validation_file(EditorField *field)
{
    /* NYI */
}

void validation_mac(EditorField *field)
{
    editor_pattern(field,
                   "^ *"
                   "[0-9A-Fa-f]{2}(-[0-9A-Fa-f]{2}){5}"
                   " *$");
}

void validation_pcislot(EditorField *field)
{
    // Only include free PCI slots, plus the field's set slot
    int mybus = 99, myslot = 99;
    sscanf(field->value.c_str(), "%d.%d", &mybus, &myslot);
    vector<char *> choices;
    for (int bus = 0; bus < MAX_PCI_BUS; ++bus)
        for (int slot = 1; slot <= MAX_PCI_SLOT[bus]; ++slot)
            if (is_pcislot_free(bus, slot) || (bus == mybus && slot == myslot))
                choices.push_back(strdup(pcislot_str(bus, slot).c_str()));
    choices.push_back(NULL);

    editor_choices(field, choices.data());

    for (size_t i = 0; i < choices.size() - 1; ++i)
        free(choices[i]);
}

/**
 * General helper routines
 **/

int fentry_index(FormEntry_t *entry, int num_entries, const char *name)
{
    for (int i = 0; i < num_entries; ++i)
        if (!strcmp(entry[i].label, name))
            return i;

    FAILURE_1(Logic, "Form entry '%s' not found", name);
}

/**
 * Routine for adding disks
 **/

void validation_disk_type(EditorField *field)
{
    const char *choices[] = {
        "file",
        "device",
        "ramdisk",
        NULL};
    editor_choices(field, choices);
}

void validation_disk_file(EditorField *field)
{
    // can be filename, device name, or empty for ramdisk
    /* NYI */
}

void validation_disk_hd_cd(EditorField *field)
{
    const char *choices[] = {
        "disk",
        "cd-rom",
        NULL};
    editor_choices(field, choices);
}

void validation_disk_autocreate_size(EditorField *field)
{
    // Preceeding and trailing optional blanks added for trimming the field,
    // see form_field_validation(3X) manpage
    // (..)? to allow leaving field empty
    editor_pattern(field, "^ *([0-9]+[KkMmGg])? *$");
}

void validation_disk_format(EditorField *field)
{
    const char *choices[] = {"auto", "raw", "qcow2", NULL};
    editor_choices(field, choices);
}

void validation_disk_autocreate_format(EditorField *field)
{
    const char *choices[] = {"raw", "qcow2", NULL};
    editor_choices(field, choices);
}

bool check_add_disks(FormEntry_t entry[], int num_entries, FormValues_t values)
{
    int idx = fentry_index(entry, num_entries, "Type");
    char *disk_type = values[idx];

    // A file-backed CD-ROM may start empty; media is inserted from the GUI later.
    const bool empty_cdrom = !strcmp(disk_type, "file") &&
        !strcmp(values[fentry_index(entry, num_entries, "Harddisk or CD-ROM")], "cd-rom");
    idx = fentry_index(entry, num_entries, "File / Device name");
    if ((!strcmp(disk_type, "file") || !strcmp(disk_type, "device")) &&
        !strcmp(values[idx], "") && !empty_cdrom)
    {
        string msg = string("No ") + disk_type + " name has been specified.";
        show_text("Error", msg.c_str());
        return false;
    }

    idx = fentry_index(entry, num_entries, "Autocreate size");
    if (!strcmp(disk_type, "ramdisk") && !strcmp(values[idx], ""))
    {
        show_text("Error", "Autocreate size can't be empty for type 'ramdisk'.");
        return false;
    }

    const char *format = values[fentry_index(entry, num_entries, "Image format (auto, raw, qcow2)")];
    const char *create_format = values[fentry_index(entry, num_entries, "Autocreate format (raw, qcow2)")];
    const bool non_raw = (strcmp(format, "auto") && strcmp(format, "raw")) || strcmp(create_format, "raw");
    if (non_raw && (strcmp(disk_type, "file") ||
        !strcmp(values[fentry_index(entry, num_entries, "Harddisk or CD-ROM")], "cd-rom")))
    {
        show_text("Error", "Non-raw image formats require a file-backed hard disk.");
        return false;
    }

    return true;
}

/**
 * Add disk 'disk_name' to the controller 'parent' of the configuration.
 **/
void add_disks(const char *title, const char *disk_name, CConfigurator *parent)
{
    FormEntry_t entry[] = {
        {"Type", "file", NULL, // no name - config entry myValue
         "Disks can be emulated in several ways.",
         validation_disk_type},
        {"File / Device name", "", NULL, // "file" | "device",
         "Enter the path to the file or device to use for this disk.\n"
         "This parameter is ignored for type 'ramdisk'.",
         validation_disk_file},
        {"Image format (auto, raw, qcow2)", "auto", "format",
         "Use Tab or Left/Right to select a format.\n"
         "Auto detects an existing image by its header; unrecognized files are raw.\n"
         "Non-raw image formats require a fixed hard disk. Auto is the default.",
         validation_disk_format},
        {"Autocreate format (raw, qcow2)", "raw", "autocreate_format",
         "Format to create when a file is missing and Autocreate size is set.\n"
         "Use Tab or Left/Right to select a format.\n"
         "Choose a supported disk image format; raw is the default. Existing files are unchanged.\n"
         "For a missing file this must match any explicit Image format override.",
         validation_disk_autocreate_format},
        {"Harddisk or CD-ROM", "disk", "cdrom",
         "Do you want the OS to see this as a hard-disk, or as a cd-rom?\n"
         "This parameter is ignored for type 'ramdisk' and floppy drives, as they always are disks.",
         validation_disk_hd_cd},
        {"Autocreate size", "", NULL, // "autocreate_size" | "size"
         "For type 'file': Do you want the program to create it? And in what size?\n"
         "Leave blank to not autocreate the file.\n"
         "For type 'ramdisk': Required to be non-empty.\n"
         "For type 'device': Ignored."
         "The file/ramdisk will be created the first time the emulator runs.\n"
         "Format: number followed by K (kilo), M (mega), or G (giga)",
         validation_disk_autocreate_size},
        {"Read-only?", STR_NO, "read_only",
         "Should the disk be set to read-only?\n"
         "This parameter is ignored for CD-ROMs (always true),\n"
         "and for ramdisks (always false)",
         validation_yes_no},
        {"Disk model number", "", "model_number",
         "Would you like to set a disk model number?",
         NULL},
        {"Revision number", "", "rev_number",
         "Would you like to set a revision number?",
         NULL},
        {"Serial number", "", "serial_number",
         "Would you like to set a serial number?",
         NULL}};
    int num_entries = ARRAY_SIZE(entry);
    CConfigurator *c;
    FormValues_t values, preset;
    int idx;
    char *disk_type;

    c = parent->find_child(disk_name);

    preset = (FormValues_t)calloc(num_entries, sizeof(char *));

    idx = fentry_index(entry, num_entries, "Type");
    preset[idx] = disk_type = (char *)((c != nullptr) ? c->get_myValue() : entry[idx].preset);

    for (int i = 0; i < num_entries; ++i)
    {
        if (i == idx)
            continue;

        if (!strcmp(entry[i].label, "Harddisk or CD-ROM") && c != nullptr)
        {
            preset[i] = (char *)(c->get_bool_value(entry[i].name) ? "cd-rom" : "disk");
            continue;
        }

        if (entry[i].name != NULL)
        {
            char *p;
            if (c != nullptr && (p = c->get_text_value(entry[i].name)) != NULL)
                preset[i] = p;
            else
                preset[i] = (char *)entry[i].preset;
            continue;
        }

        if (c != nullptr)
        {
            if (!strcmp(entry[i].label, "File / Device name"))
            {
                if (!strcmp(disk_type, "file") || !strcmp(disk_type, "device"))
                    preset[i] = c->get_text_value(disk_type, c->get_bool_value("cdrom") ? "" : "?");
                else
                    preset[i] = (char *)entry[i].preset;
                continue;
            }
            if (!strcmp(entry[i].label, "Autocreate size"))
            {
                if (!strcmp(disk_type, "ramdisk"))
                    preset[i] = c->get_text_value("size", "?");
                else if (!strcmp(disk_type, "file"))
                    preset[i] = c->get_text_value("autocreate_size", "");
                else
                    preset[i] = (char *)entry[i].preset;
                continue;
            }

            FAILURE_1(Logic, "No handling of preset '%s'", entry[i].label);
        }

        preset[i] = (char *)entry[i].preset;
    }

    values = show_form(title, entry, preset, num_entries, check_add_disks);

    if (values != NULL)
    {
        idx = fentry_index(entry, num_entries, "Type");
        disk_type = values[idx];
        bool is_ramdisk = !strcmp(disk_type, "ramdisk");

        idx = fentry_index(entry, num_entries, "Harddisk or CD-ROM");
        bool is_cdrom = !strcmp(values[idx], "cd-rom");
        bool is_floppy = !strcmp(parent->get_myName(), "fdc0");
        const char *format = values[fentry_index(entry, num_entries, "Image format (auto, raw, qcow2)")];
        const char *create_format = values[fentry_index(entry, num_entries, "Autocreate format (raw, qcow2)")];
        if (is_floppy && ((strcmp(format, "auto") && strcmp(format, "raw")) ||
                         strcmp(create_format, "raw")))
        {
            show_text("Error", "Floppy drives require raw images.");
            for (int i = 0; i < num_entries; ++i)
                free(values[i]);
            free(values);
            free(preset);
            return;
        }
        if (is_cdrom && (is_ramdisk || is_floppy))
        {
            free(values[idx]);
            values[idx] = strdup("disk");
        }

        idx = fentry_index(entry, num_entries, "Read-only?");
        if (is_ramdisk)
        {
            free(values[idx]);
            values[idx] = strdup(STR_NO);
        }
        else if (is_cdrom)
        {
            free(values[idx]);
            values[idx] = strdup(STR_YES);
        }

        // Preserve a manually configured overlay without exposing it in the form.
        const std::string overlay_file = c ? c->get_text_value("overlay_file", "") : "";
        const bool type_changed = c && strcmp(c->get_myValue(), disk_type);
        c = edit_node(parent, c, disk_name, disk_type);
        if (type_changed)
            for (const char *key : {"file", "device", "size", "autocreate_size", "format", "autocreate_format"})
                c->remove_value((char *)key);
        if (strcmp(disk_type, "file") || is_cdrom || is_floppy) c->remove_value((char *)"overlay_file");
        if (!overlay_file.empty() && !strcmp(disk_type, "file") && !is_cdrom && !is_floppy)
            c->set_value(strdup("overlay_file"), strdup(overlay_file.c_str()));

        for (int i = 0; i < num_entries; ++i)
        {
            if (!strcmp(entry[i].label, "Harddisk or CD-ROM"))
            {
                if (!strcmp(values[i], "cd-rom"))
                    set_form_value(c, i, entry[i].name, STR_YES, type_changed);
                else
                    set_form_value(c, i, entry[i].name, STR_NO, type_changed);
                continue;
            }

            if (entry[i].name != NULL)
            {
                if ((!strcmp(entry[i].name, "format") ||
                     !strcmp(entry[i].name, "autocreate_format")) &&
                    strcmp(disk_type, "file"))
                    continue;
                set_form_value(c, i, entry[i].name, values[i], type_changed ||
                    (!strcmp(entry[i].name, "read_only") && changed_fields[fentry_index(entry, num_entries, "Harddisk or CD-ROM")]));
                continue;
            }

            if (!strcmp(entry[i].label, "File / Device name") &&
                (!strcmp(disk_type, "file") || !strcmp(disk_type, "device")))
            {
                if (values[i][0]) set_form_value(c, i, disk_type, values[i], type_changed);
                else c->remove_value((char *)disk_type); // Empty CD-ROM drive: no file key at all.
            }

            if (!strcmp(entry[i].label, "Autocreate size"))
            {
                const size_t length = strlen(values[i]);
                if (length) values[i][length - 1] = (char)toupper((unsigned char)values[i][length - 1]);
                if (is_ramdisk)
                    set_form_value(c, i, "size", values[i], type_changed);
                if (!strcmp(disk_type, "file"))
                {
                    if (strcmp(values[i], "")) set_form_value(c, i, "autocreate_size", values[i], type_changed);
                    else if (changed_fields[i]) c->remove_value((char *)"autocreate_size");
                }
            }
        }

        // Clean up
        for (int i = 0; i < num_entries; ++i)
            free(values[i]);
        free(values);
    }

    free(preset);
}

/**
 * Add SCSI disks (up to ID 'max_scsi_id') to the controller 'parent' of the configuration.
 **/
void add_scsi_disks(const char *title, const int max_scsi_id, CConfigurator *parent)
{
    vector<MenuEntry_t> entry;

    entry.push_back({"none", "Stop adding disks", NULL});

    for (int i = 0; i < max_scsi_id; ++i)
    {
        string desc = "Target " + i2s(i);
        string text = "disk0." + i2s(i);
        CConfigurator *c = parent->find_child(text.c_str());
        if (c != nullptr)
            text = text + "  " + c->get_myValue();
        else
            text = text + "  -";
        entry.push_back({strdup(text.c_str()), strdup(desc.c_str()), NULL});
    }
    int num_entries = entry.size();

    while (true)
    {
        int sel = show_menu(title, entry.data(), num_entries, MENU_4TH_LEVEL);

        if (sel <= 0)
            break;

        char disk_name[sizeof("disk0.xx")];
        strncpy(disk_name, entry[sel].text, sizeof(disk_name));
        disk_name[(max_scsi_id < 10) ? 7 : 8] = '\0';

        string subtitle = string(title) + ": " + disk_name;
        add_disks(subtitle.c_str(), disk_name, parent);

        // Update display of disk entry
        CConfigurator *c = parent->find_child(disk_name);
        free((void *)entry[sel].text);
        string text = disk_name + string("  ") + (c ? c->get_myValue() : "-");
        entry[sel].text = strdup(text.c_str());
    }

    // Clean up
    for (int i = 1; i <= max_scsi_id; ++i)
    {
        free((void *)entry[i].text);
        free((void *)entry[i].description);
    }
}

/**
 * GUI configuration
 **/

#if defined(HAVE_SDL) || defined(_WIN32) || defined(HAVE_X11)
void validation_hotkey(EditorField *field)
{
    // TODO: I found no way to match case-insensitively?!
    // see src/sdl.cpp parse_sdl_hotkey() et al.
    editor_pattern(field,
                   "^ *"
                   "(none|"
                   "((Ctrl|Alt|Shift|GUI|Control|Option|Command|Cmd|Super|Win|Windows|Meta)"
                   "(\\+(Ctrl|Alt|Shift|GUI|Control|Option|Command|Cmd|Super|Win|Windows|Meta))*"
                   "\\+(F[0-9]+|PageUp|PageDown|Insert|Delete|Escape|Home|End|Keypad \\+|Keypad -|PgUp|PgDn|PageDn|Ins|Del|Esc|KeypadPlus|KpPlus|KeypadMinus|KpMinus|[A-Z0-9])"
                   ")) *$");
}

#define HOTKEY_HELP                                                                       \
    "Values are case-insensitive combinations such as \"Ctrl+F11\" or \"GUI+Shift+M\".\n" \
    "Modifiers, if any, precede exactly one non-modifier key.\n"                          \
    "Supported modifiers are Ctrl, Alt, Shift and GUI; Control, Option, Command,\n"       \
    "Cmd, Super, Win, Windows and Meta are accepted aliases. The key name uses\n"         \
    "SDL's names, including F1-F24, PageUp, PageDown, Home, End, Insert, Delete,\n"       \
    "Escape, letters and digits; PgUp, PgDn, Esc, Del and Ins are also accepted.\n"       \
    "Only the listed modifiers may be held.\n"                                            \
    "Set a binding to \"none\" to disable that keyboard binding.\n"                       \
    "Invalid or duplicate bindings are reported at runtime and\n"                         \
    "the affected actions are disabled."

bool check_gui_keyboard(FormEntry_t entry[], int num_entries, FormValues_t values)
{
    if (!strcmp(values[fentry_index(entry, num_entries, "keyboard use mapping?")], STR_YES) &&
        !strcmp(values[fentry_index(entry, num_entries, "keyboard map")], ""))
    {
        show_text("Error", "No keyboard map has been specified.");
        return false;
    }
    return true;
}

// Shared GUI keyboard and general hotkey settings.
void edit_gui_keyboard(const char *title)
{
    FormEntry_t entry[] = {
        {"keyboard use mapping?", STR_NO, "keyboard.use_mapping",
         "Use a keymap file to translate host keys to guest scancodes.\n"
         "Enable this if you use a non-US keyboard layout.\n"
         "If set to 'yes', keyboard.map must be filled out.",
         validation_yes_no},
        {"keyboard map", "keys.map", "keyboard.map",
         "The keymap file to use when keyboard.use_mapping is enabled.",
         validation_file},
        {"hotkey.media", "Ctrl+F11", "hotkey.media",
         "Host key combination used to bring up the removable media selector.\n" HOTKEY_HELP,
         validation_hotkey},
        {"hotkey.ctrl_alt_delete", "Ctrl+Alt+End", "hotkey.ctrl_alt_delete",
         "Host key combination used to send Ctrl+Alt+Delete to the guest.\n" HOTKEY_HELP,
         validation_hotkey}};
    const int num_entries = ARRAY_SIZE(entry);
    CConfigurator *c;
    FormValues_t preset;

    c = theConfig->find_child("gui");
    preset = (FormValues_t)calloc(num_entries, sizeof(char *));

    for (int i = 0; i < num_entries; ++i)
    {
        char *p;
        if (entry[i].name != NULL && c != nullptr && (p = c->get_text_value(entry[i].name)) != NULL)
            preset[i] = p;
        else
            preset[i] = (char *)entry[i].preset;
    }

    FormValues_t values = show_form(title, entry, preset, num_entries, check_gui_keyboard);

    if (values != NULL)
    {
        for (int i = 0; i < num_entries; ++i)
            set_form_value(c, i, entry[i].name, values[i]);

        // Clean up
        for (int i = 0; i < num_entries; ++i)
            free(values[i]);
        free(values);
    }
    free(preset);
}

void validation_gui_mousespeed(EditorField *field)
{
    editor_number(field, EditorField::Type::Number, 1, 0.1, 10.0);
}

// Shared GUI mouse settings.
void edit_gui_mouse(const char *title)
{
    FormEntry_t entry[] = {
        {"mouse speed factor", "1.0", "mouse.speed",
         "Multiplier applied to host mouse motion before it is passed to the guest.\n"
         "Use a value below 1.0 to slow the guest pointer down (e.g. 0.5 for half speed),\n"
         "or above 1.0 (up to 10.0) to speed it up.",
         validation_gui_mousespeed},
        {"mouse invert x?", STR_NO, "mouse.invert_x",
         "Reverse the direction of host mouse motion on the horizontal axis.",
         validation_yes_no},
        {"mouse invert y?", STR_NO, "mouse.invert_y",
         "Reverse the direction of host mouse motion on the vertical axis.",
         validation_yes_no},
        {"absolute pointer input? (normal desktop: no)", STR_NO, "mouse.absolute",
         "Choose no for normal desktop operation (recommended default).\n"
         "Choose yes if your input source supplies absolute pointer coordinates.\n"
         "This uses successive host window coordinates while captured.\n"
         "The guest still receives relative PS/2 movement; its pointer is not\n"
         "guaranteed to match an exact host pixel. Speed and inversion still apply.",
         validation_yes_no},
        {"hotkey.mouse_capture", "Ctrl+F10", "hotkey.mouse_capture",
         "Host key combination used to toggle mouse capture.\n" HOTKEY_HELP,
         validation_hotkey}};
    const int num_entries = ARRAY_SIZE(entry);
    CConfigurator *c;
    FormValues_t preset;

    c = theConfig->find_child("gui");
    preset = (FormValues_t)calloc(num_entries, sizeof(char *));

    for (int i = 0; i < num_entries; ++i)
    {
        char *p;
        if (entry[i].name != NULL && c != nullptr && (p = c->get_text_value(entry[i].name)) != NULL)
        {
            // Boolean configuration values also accept true/false and 1/0.
            preset[i] = p;
            if (entry[i].validation_callback == validation_yes_no)
            {
                if (!strcasecmp(p, "yes") || !strcasecmp(p, "true") ||
                    !strcmp(p, "1"))
                    preset[i] = (char *)STR_YES;
                else if (!strcasecmp(p, "no") || !strcasecmp(p, "false") ||
                         !strcmp(p, "0"))
                    preset[i] = (char *)STR_NO;
            }
        }
        else
            preset[i] = (char *)entry[i].preset;
    }

    FormValues_t values = show_form(title, entry, preset, num_entries, NULL);

    if (values != NULL)
    {
        for (int i = 0; i < num_entries; ++i)
            set_form_value(c, i, entry[i].name, values[i]);

        // Clean up
        for (int i = 0; i < num_entries; ++i)
            free(values[i]);
        free(values);
    }
    free(preset);
}

void validation_gui_scaleratio(EditorField *field)
{
    editor_number(field, EditorField::Type::Integer, 1, 0, 10);
}

// Shared GUI video settings.
void edit_gui_video(const char *title)
{
    vector<FormEntry_t> entry = {
        {"video linear", STR_YES, "video.linear",
         "Filtering used when the guest display is scaled to the window size:\n"
         "true = linear (smooth), false = nearest neighbor (sharp pixels).",
         validation_yes_no},
        {"video scale ratio", "0", "video.scale_ratio",
         "Integer scale factor for the emulator window (1 - 10). For example, "
         "2 renders the guest's 640x480 display in a 1280x960 window. The default\n"
         "is 0, which selects the backend's default scale.",
         validation_gui_scaleratio},
        {"video scale change enable?", STR_NO, "video.scale_change_enable",
         "If enabled, the display scale ratio can be adjusted on the fly\n"
         "while the emulator is running, without restarting.\n"
         "The change is not persisted back to the config file.\n"
         "The hotkey bindings take effect only\n"
         "when runtime display scale changes are enabled.",
         validation_yes_no},
        {"hotkey.scale_up", "Ctrl+PageUp", "hotkey.scale_up",
         "Host key combination used to increase runtime window scale by 1 (clamped at 8x).\n" HOTKEY_HELP,
         validation_hotkey},
        {"hotkey.scale_down", "Ctrl+PageDown", "hotkey.scale_down",
         "Host key combination used to decrease runtime window scale by 1 (clamped at 1x).\n" HOTKEY_HELP,
         validation_hotkey},
        {"hotkey.reset_window", "Ctrl+Alt+Home", "hotkey.reset_window",
         "Host key combination used to reset window size.\n" HOTKEY_HELP,
         validation_hotkey}};
    CConfigurator *c;
    FormValues_t preset;

    c = theConfig->find_child("gui");
    if (c != nullptr && !strcmp(c->get_myValue(), "X11"))
        entry.push_back({"private colormap?", STR_YES, "private_colormap",
                         "Use a private X11 colormap.", validation_yes_no});
    const int num_entries = static_cast<int>(entry.size());
    preset = (FormValues_t)calloc(num_entries, sizeof(char *));

    for (int i = 0; i < num_entries; ++i)
    {
        char *p;
        if (entry[i].name != NULL && c != nullptr && (p = c->get_text_value(entry[i].name)) != NULL)
            preset[i] = p;
        else
            preset[i] = (char *)entry[i].preset;
    }

    FormValues_t values = show_form(title, entry.data(), preset, num_entries, NULL);

    if (values != NULL)
    {
        for (int i = 0; i < num_entries; ++i)
            set_form_value(c, i, entry[i].name, values[i]);

        // Clean up
        for (int i = 0; i < num_entries; ++i)
            free(values[i]);
        free(values);
    }
    free(preset);
}

static void edit_gui_settings(const char *title, const char *backend)
{
    MenuEntry_t entry[] = {
        {"Edit keyboard settings", "Edit keyboard and general settings.", edit_gui_keyboard},
        {"Edit mouse settings", "Edit mouse settings.", edit_gui_mouse},
        {"Edit video settings", "Edit video settings.", edit_gui_video}};
    const int num_entries = ARRAY_SIZE(entry);
    edit_node(theConfig, theConfig->find_child("gui"), "gui", backend);

    show_menu(title, entry, num_entries, MENU_3RD_LEVEL);
}
#if defined(HAVE_SDL)
void edit_gui_sdl(const char *title) { edit_gui_settings(title, "sdl"); }
#endif
#if defined(_WIN32)
void edit_gui_win32(const char *title) { edit_gui_settings(title, "win32"); }
#endif
#if defined(HAVE_X11)
void edit_gui_x11(const char *title) { edit_gui_settings(title, "X11"); }
#endif // HAVE_X11
#endif // HAVE_SDL || _WIN32 || HAVE_X11

// Menu for GUI
void edit_gui(const char *title)
{
    MenuEntry_t entry[] = {
        {"none", "No GUI. Graphics cards will not be supported.", NULL}
#if defined(HAVE_SDL)
        ,
        {"sdl", "Simple Directmedia Layer. Preferred GUI mechanism.", edit_gui_sdl}
#endif
#if defined(HAVE_X11)
        ,
        {"X11", "Unix X-Windows GUI support.", edit_gui_x11}
#endif
#if defined(_WIN32)
        ,
        {"win32", "Native Windows GUI.", edit_gui_win32}
#endif
    };
    int num_entries = ARRAY_SIZE(entry);

    if (num_entries == 1)
    {
        show_text(title, "No graphical output backend is available in this build.");
        return;
    }

    // Say which backend the configuration uses now; the menu itself cannot show it.
    static std::string marked[ARRAY_SIZE(entry)];
    CConfigurator *current = theConfig->find_child("gui");
    const char *active = current ? current->get_myValue() : "none";
    for (int i = 0; i < num_entries; ++i)
    {
        if (strcasecmp(entry[i].text, active)) continue;
        marked[i] = std::string("Current choice.  ") + entry[i].description;
        entry[i].description = marked[i].c_str();
    }

    int sel = show_menu(title, entry, num_entries, MENU_2ND_LEVEL);

    if (sel == 0)
    {
        CConfigurator *c = theConfig->find_child("gui");
        if (c != nullptr)
            theConfig->remove_child(c->get_myName());
    }
}

/**
 * Tsunami system settings configuration
 **/

void validation_tsunami_memory(EditorField *field)
{
    char *choices[HI_MEM_BITS + 1 - LOW_MEM_BITS + 1];

    for (int i = LOW_MEM_BITS; i <= HI_MEM_BITS; ++i)
        choices[i - LOW_MEM_BITS] = strdup(memory_bits2size(i).c_str());
    choices[HI_MEM_BITS + 1 - LOW_MEM_BITS] = NULL;

    editor_choices(field, choices);

    for (int i = LOW_MEM_BITS; i <= HI_MEM_BITS; ++i)
        free(choices[i - LOW_MEM_BITS]);
}

bool check_tsunami(FormEntry_t entry[], int num_entries, FormValues_t values)
{
    if (!strcmp(values[fentry_index(entry, num_entries, "rom.srm file")], ""))
    {
        show_text("Error", "No ROM filename has been specified.");
        return false;
    }
    return true;
}

// Form for Tsunami
void edit_tsunami(const char *title)
{
    FormEntry_t entry[] = {
        {"rom.srm file",
#if defined(_WIN32)
         "rom\\cl67srmrom.exe",
#elif defined(__VMS)
         "[.ROM]CL67SRMROM.EXE",
#else
         "rom/cl67srmrom.exe",
#endif
         "rom.srm",
         "Location of the SRM ROM image.\n"
         "This file is required.",
         validation_file},
        {"rom.flash file",
#if defined(_WIN32)
         "rom\\flash.rom",
#elif defined(__VMS)
         "[.ROM]FLASH.ROM",
#else
         "rom/flash.rom",
#endif
         "rom.flash",
         "Location of the Flash ROM image",
         validation_file},
        {"rom.dpr file",
#if defined(_WIN32)
         "rom\\dpr.rom",
#elif defined(__VMS)
         "[.ROM]DPR.ROM",
#else
         "rom/dpr.rom",
#endif
         "rom.dpr",
         "Location of the DPR EEPROM image.",
         validation_file},
        {"rom.toy file",
#if defined(_WIN32)
         "rom\\toy.rom",
#elif defined(__VMS)
         "[.ROM]TOY.ROM",
#else
         "rom/toy.rom",
#endif
         "rom.toy",
         "Location of the CMOS/TOY NVRAM image.\n"
         "Preserves SRM CMOS settings (such as heap_expand)\n"
         "across emulator restarts, like the battery-backed\n"
         "CMOS on real hardware.",
         validation_file},
        {"memory size", "256M", NULL, // "memory.bits"
         "Amount of RAM memory.\n"
         "Your system should have enough free memory\n"
         "to emulate the amount you choose here.",
         validation_tsunami_memory},
        {"fixed date", "", "time",
         "Set a fixed date and time when the VM starts.\n"
         "By default (empty value), the VM's date and time is initialized\n"
         "to the current host date and time at startup.\n"
         "You can set a fixed date and time instead,\n"
         "Format: 'YYYY-MM-DD' or 'YYYY-MM-DD HH:MM:SS'",
         validation_optional_datetime},
        {"arc_year_compat?", STR_NO, "arc_year_compat",
         "Should the reported year be compatible with Windows?\n"
         "This only affects the year reported to the guest.\n"
         "Select 'yes' if you are planning to run Windows OSes.",
         validation_yes_no},
        {"Exit on PAL_halt?", STR_NO, "exit_on_pal_halt",
         "Should the VM power off on the guest's request?",
         validation_yes_no}};
    const int num_entries = ARRAY_SIZE(entry);
    FormValues_t preset;
    int idx;

    preset = (FormValues_t)calloc(num_entries, sizeof(char *));

    idx = fentry_index(entry, num_entries, "memory size");
    string memory_size; // keeps the memory size preset alive until show_form() returns
    for (int i = 0; i < num_entries; ++i)
    {
        char *p;
        if (i == idx && (p = sys0->get_text_value("memory.bits", NULL)))
        {
            memory_size = memory_bits2size(atoi(p));
            preset[i] = (char *)memory_size.c_str();
        }
        else if (entry[i].name != NULL && (p = sys0->get_text_value(entry[i].name)) != NULL)
            preset[i] = p;
        else
            preset[i] = (char *)entry[i].preset;
    }

    FormValues_t values = show_form(title, entry, preset, num_entries, check_tsunami);

    if (values != NULL)
    {
        idx = fentry_index(entry, num_entries, "memory size");
        set_form_value(sys0, idx, "memory.bits", i2s(memory_size2bits(values[idx])).c_str());

        for (int i = 0; i < num_entries; ++i)
            if (entry[i].name != NULL)
            {
                if (!strcmp(entry[i].name, "time") && !values[i][0])
                { if (changed_fields[i]) sys0->remove_value((char *)entry[i].name); }
                else set_form_value(sys0, i, entry[i].name, values[i]);
            }

        // Clean up
        for (int i = 0; i < num_entries; ++i)
            free(values[i]);
        free(values);
    }
    free(preset);
}

/**
 * EV68 CPU configuration
 **/

void validation_ev68cb_cpuspeed(EditorField *field)
{
    // Preceeding and trailing optional blanks added for trimming the field,
    // see form_field_validation(3X) manpage
    // (..)? to allow leaving field empty
    editor_number(field, EditorField::Type::Integer, 2, 10, 1250);
}

void validation_ev68cb_max_ticks(EditorField *field)
{
    editor_number(field, EditorField::Type::Integer, 1, 1, INT_MAX);
}

// Form for EV68CB
void edit_ev68cb(const char *title)
{
    vector<FormEntry_t> entry; // (enabled, speed, max_instr_per_tick [,nohle]) * MAX_CPUS

    for (int i = 0; i < MAX_CPUS; ++i)
    {
        const string cpu = "cpu" + i2s(i);
        const string enabled = cpu + " enabled?";
        if (i == 0)
            entry.push_back({strdup(enabled.c_str()), STR_YES, NULL,
                             "CPU 0 is always enabled.",
                             validation_yes});
        else
            entry.push_back({strdup(enabled.c_str()), STR_NO, NULL,
                             "Enable CPU?\n"
                             "If not enabled, all parameters refering to this CPU will be ignored.",
                             validation_yes_no});

        const string speed = cpu + ".speed";
        entry.push_back({strdup(speed.c_str()), "500", "speed",
                         "The CPU speed reported to the guest platform (in MHz, ranging from 10 to 1250).\n"
                         "This does not affect the speed of the emulation.",
                         validation_ev68cb_cpuspeed});
#ifndef ES40_JIT
        const string nohle = cpu + ".palcode.vms.nohle?";
        entry.push_back({strdup(nohle.c_str()), STR_NO, "palcode.vms.nohle",
                         "Disable the high-level emulation (HLE) of the OpenVMS PALcode\n"
                         "and run the real SRM PALcode instead.",
                         validation_yes_no});
#endif
        const string max_ticks = cpu + ".timer.max_instr_per_tick";
        entry.push_back({strdup(max_ticks.c_str()), "1250000", "timer.max_instr_per_tick",
                         "Upper bound on guest instructions retired between 1024 Hz interval-timer\n"
                         "ticks. Real Alpha silicon topped out at 1.25 GHz (EV68).\n"
                         "A CPU that reaches this bound before the next wall-clock tick is due is\n"
                         "held (slept) until the tick lands, so the guest never sees more than this\n"
                         "many instructions per tick and its tick-counted clock stays wall-clock accurate.",
                         validation_ev68cb_max_ticks});
    }
    const int num_entries = entry.size();
    const int entries_per_cpu = num_entries / MAX_CPUS;
    CConfigurator *c;
    FormValues_t preset;
    int idx;
    char speed[5+1];

    preset = (FormValues_t)calloc(num_entries, sizeof(char *));

    for (int i = 0; i < MAX_CPUS; ++i)
    {
        const string cpu = "cpu" + i2s(i);
        const string enabled = cpu + " enabled?";
        idx = fentry_index(entry.data(), num_entries, enabled.c_str());
        c = sys0->find_child(cpu.c_str());
        for (int k = i * entries_per_cpu; k < (i + 1) * entries_per_cpu; ++k)
            if (entry[k].name != NULL)
            {
                char *p;
                if (c != nullptr && (p = c->get_text_value(entry[k].name)) != NULL)
                {
                    if (!strcmp(entry[k].name, "speed"))
                    {
                        // Cut off the trailing "M"
                        size_t len = strlen(p) - 1;
                        if (len >= sizeof(speed))
                            len = sizeof(speed) - 1;
                        strncpy(speed, p, len);
                        speed[len] = '\0';
                        preset[k] = strdup(speed);
                    }
                    else
                        preset[k] = p;
                }
                else
                    preset[k] = (char *)entry[k].preset;
            }
            else
            {
                // Just a safeguard...
                if (k != idx)
                    FAILURE_1(Logic, "Field is not '%s'", enabled.c_str());
                if (c != nullptr)
                    preset[k] = (char *)STR_YES;
                else
                    /* Always use entry presets for CPUs not enabled.
                     * We can't decide by looking at sys0's children
                     * whether the missing of their entries is intentional or not yet configured.
                     */
                    preset[k] = (char *)entry[k].preset;
            }
    }

    CConfigurator *original_cpus[MAX_CPUS];
    for (int i = 0; i < MAX_CPUS; ++i)
        original_cpus[i] = sys0->find_child(("cpu" + i2s(i)).c_str());
    FormValues_t values = show_form(title, entry.data(), preset, num_entries, NULL);

    if (values != NULL)
    {
        for (int i = 0, j = 0; i < MAX_CPUS; ++i)
        {
            const string enabled = "cpu" + i2s(i) + " enabled?";
            idx = fentry_index(entry.data(), num_entries, enabled.c_str());
            if (!strcmp(values[idx], STR_YES))
            {
                const string cpu = "cpu" + i2s(j++);
                c = edit_node(sys0, original_cpus[i], cpu, "ev68cb");
                for (int k = i * entries_per_cpu; k < (i + 1) * entries_per_cpu; ++k)
                    if (entry[k].name != NULL)
                    {
                        if (!strcmp(entry[k].name, "speed"))
                        {
                            // Add the trailing "M"
                            snprintf(speed, sizeof(speed), "%sM", values[k]);
                            set_form_value(c, k, entry[k].name, speed);
                        }
                        else
                            set_form_value(c, k, entry[k].name, values[k]);
                    }
            }
            else
            {
                c = original_cpus[i];
                if (c != nullptr)
                    sys0->remove_child(c->get_myName());
            }
        }

        // Clean up
        for (int i = 0; i < num_entries; ++i)
            free(values[i]);
        free(values);
    }

    for (int i = 0; i < num_entries; ++i)
        free((void *)entry[i].label);
    free(preset);
}

/**
 * ALI 1543 configuration
 **/

void validation_ali_console(EditorField *field)
{
    const char *choices[] = {
        "serial",
        "graphics",
        NULL};
    editor_choices(field, choices);
}

// Form for ALI 1543
void edit_ali(const char *title)
{
    FormEntry_t entry[] = {
        {"Console Output", "serial", "vga_console",
         "Where would you like console output to go?\n"
         "This is the SRM 'console' variable.\n"
         "WARNING: for the 'graphics' option to work,\n"
         "you need to configure a VGA card and a GUI.",
         validation_ali_console},
        {"LPT Output", "", "lpt.outfile",
         "Where would you like printer output to go?\n"
         "Output from the printer port will be saved to this file.\n"
         "Leave blank if not wanted.",
         validation_file},
        {"M7101 PMU enabled?", STR_YES, NULL,
         "Enable the M7101 power-management / ACPI device at PCI 0:17?",
         validation_yes_no},
        {"USB controller enabled?", STR_YES, NULL,
         "Enable the USB OHCI controller at PCI 0:19?",
         validation_yes_no}};
    const int num_entries = ARRAY_SIZE(entry);
    CConfigurator *c;
    FormValues_t preset;
    int idx;

    c = sys0->find_child(PCI_SLOT_ALI);
    preset = (FormValues_t)calloc(num_entries, sizeof(char *));

    char *p;
    idx = fentry_index(entry, num_entries, "Console Output"); // name == "vga_console"
    if (c != nullptr && (p = c->get_text_value(entry[idx].name)) != NULL)
        preset[idx] = (char *)(c->get_bool_value(entry[idx].name) ? "graphics" : "serial"); // true/yes/1 all mean graphics
    else
        preset[idx] = (char *)entry[idx].preset;

    idx = fentry_index(entry, num_entries, "LPT Output");
    if (c != nullptr && (p = c->get_text_value(entry[idx].name)) != NULL)
        preset[idx] = p;
    else
        preset[idx] = (char *)entry[idx].preset;

    /* Always use entry presets for PMU and USB controller.
     * We can't decide by looking at sys0's children
     * whether the missing of their entries is intentional or not yet configured.
     */
    idx = fentry_index(entry, num_entries, "M7101 PMU enabled?");
    preset[idx] = (char *)(c ? (sys0->find_child(PCI_SLOT_PMU) ? STR_YES : STR_NO) : entry[idx].preset);
    idx = fentry_index(entry, num_entries, "USB controller enabled?");
    preset[idx] = (char *)(c ? (sys0->find_child(PCI_SLOT_USB) ? STR_YES : STR_NO) : entry[idx].preset);

    FormValues_t values = show_form(title, entry, preset, num_entries, NULL);

    if (values != NULL)
    {
        c = sys0->find_child(PCI_SLOT_ALI);
        c = edit_node(sys0, c, PCI_SLOT_ALI, "ali");

        idx = fentry_index(entry, num_entries, "Console Output");
        set_form_value(c, idx, entry[idx].name, !strcmp(values[idx], "graphics") ? STR_YES : STR_NO);

        idx = fentry_index(entry, num_entries, "LPT Output");
        if (strcmp(values[idx], "")) set_form_value(c, idx, entry[idx].name, values[idx]);
        else if (changed_fields[idx]) c->remove_value((char *)entry[idx].name);

        idx = fentry_index(entry, num_entries, "M7101 PMU enabled?");
        if (!strcmp(values[idx], STR_YES))
        {
            c = sys0->find_child(PCI_SLOT_PMU);
            if (c == nullptr)
                c = new CConfigurator(sys0, (char *)PCI_SLOT_PMU, (char *)"ali_pmu");
        }
        else
        {
            sys0->remove_child(PCI_SLOT_PMU);
        }

        idx = fentry_index(entry, num_entries, "USB controller enabled?");
        if (!strcmp(values[idx], STR_YES))
        {
            c = sys0->find_child(PCI_SLOT_USB);
            if (c == nullptr)
                c = new CConfigurator(sys0, (char *)PCI_SLOT_USB, (char *)"ali_usb");
        }
        else
        {
            sys0->remove_child(PCI_SLOT_USB);
        }

        // Clean up
        for (int i = 0; i < num_entries; ++i)
            free(values[i]);
        free(values);
    }
    free(preset);
}

/**
 * PCI bus configuration
 **/

void set_pcicard_values_default(CConfigurator *c, FormEntry_t entry[], int num_entries, FormValues_t values)
{
    int idx = fentry_index(entry, num_entries, "PCI slot");
    string name = string("pci") + values[idx];
    c->set_myName((char*)name.c_str());

    for (int i = 0; i < num_entries; ++i)
        if (i != idx && entry[i].name != NULL)
            set_form_value(c, i, entry[i].name, values[i]);
}

void edit_pcicard(
    const char *title,
    const char *type,
    FormEntry_t entry[],
    int num_entries,
    bool (*check_values)(FormEntry_t[], int, FormValues_t) = NULL,
    void (*set_values)(CConfigurator*, FormEntry_t[], int, FormValues_t) = set_pcicard_values_default,
    void (*add_children)(CConfigurator*) = NULL
)
{
    vector<MenuEntry_t> choices = {
        {"Add new card", "", NULL}
    };
    for (auto c : sys0->find_children_myValue(type)) {
        choices.push_back({c->get_myName(), "", NULL});
    }
    int sel = 0;
    if (choices.size() > 1)
    {
        string title = string("Select ") + type + " card in PCI slot to edit";
        sel = show_menu(title.c_str(), choices.data(), choices.size(), MENU_2ND_LEVEL);
    }
    if (sel < 0)
        return;

    CConfigurator *c;
    bool is_new_card = (sel == 0);
    int idx = fentry_index(entry, num_entries, "PCI slot");
    if (is_new_card)
    {
        c = new CConfigurator(sys0, (char *)PCI_SLOT_TEMP, (char *)type);
        entry[idx].preset = find_first_free_pcislot();
        if (entry[idx].preset == NULL)
        {
            show_text(title, "No free PCI slots");
            return;
        }
    }
    else
    {
        c = sys0->find_child(choices[sel].text);
        entry[idx].preset = strdup(c->get_myName() + strlen("pci"));
    }

    FormValues_t preset = (FormValues_t)calloc(num_entries, sizeof(char *));
    for (int i = 0; i < num_entries; ++i)
    {
        if (i == idx)
            preset[i] = (char *)entry[i].preset;
        else
        {
            char *p = !is_new_card && entry[i].name ? c->get_text_value(entry[i].name) : NULL;
            preset[i] = p ? p : (char *)entry[i].preset;
        }
    }

    FormValues_t values = show_form(title, entry, preset, num_entries, check_values);
    if (values != NULL)
    {
        set_values(c, entry, num_entries, values);

        for (int i = 0; i < num_entries; ++i)
            free(values[i]);
        free(values);

        if (add_children != NULL)
            add_children(c);
    }
    else if (is_new_card)
        sys0->remove_child(PCI_SLOT_TEMP);
    free(preset);
}

void show_pcislots(const char *title)
{
    string lines;
    for (int bus = 0; bus < MAX_PCI_BUS; ++bus)
        for (int slot = 1; slot <= MAX_PCI_SLOT[bus]; ++slot)
        {
            string o = string("pci") + pcislot_str(bus, slot) + ": " + device_in_pcislot(bus, slot);
            lines.append(o);
            if (bus == 0 || slot < 6)
                lines.append("\n");
        }

    show_text(title, lines.c_str());
}

bool check_pci_vga_s3(FormEntry_t entry[], int num_entries, FormValues_t values)
{
    return strcmp(values[fentry_index(entry, num_entries, "rom file")], "");
}

// Form for S3 VGA PCI card
void edit_pci_vga_s3(const char *title)
{
    FormEntry_t entry[] = {
        {"PCI slot", NULL, NULL,
         "Which PCI slot should the S3 Trio64 card be on?\n"
         "Use bus 0 for a firmware graphics console.",
         validation_pcislot},
        {"rom file",
#if defined(_WIN32)
         "rom\\VGABIOSFILE.bin",
#elif defined(__VMS)
         "[.ROM]VGABIOSFILE.bin",
#else
         "rom/VGABIOSFILE.bin",
#endif
         "rom",
         "Where can the VGA BIOS ROM image be found?\n"
         "Real S3 Trio64 VGA BIOS images work, GNU vgabios project also works,\n"
         "but may not enable or support all S3 features.\n"
         "This file is required.",
         validation_file},
        {"Wake card at reset?", STR_NO, "wake_on_reset",
         "The Trio64 leaves reset asleep until its own S3 BIOS wakes it.\n"
         "Answer yes with a generic VGA BIOS such as GNU vgabios\n"
         "(Debian/Ubuntu package \"vgabios\", /usr/share/vgabios/vgabios.bin),\n"
         "which never sends that wake-up sequence. Leave no with a real S3 BIOS.",
         validation_yes_no}
    };

    edit_pcicard(title, "s3", entry, ARRAY_SIZE(entry), check_pci_vga_s3);
}

bool check_pci_vga_gloria(FormEntry_t entry[], int num_entries, FormValues_t values)
{
    if (!strcmp(values[fentry_index(entry, num_entries, "rom file")], ""))
        return false;
    CConfigurator *occupant = get_pcislot(values[fentry_index(entry, num_entries, "PCI slot")]);
    if (occupant && occupant != sys0->find_child_myValue("gloria"))
    {
        show_text("Error", "That PCI slot belongs to another device. Choose a free slot.");
        return false;
    }
    return true;
}

void validation_gloria_rom_layout(EditorField *field)
{
    const char *choices[] = {"exact", "mirror32", "pad_ff", NULL};
    editor_choices(field, choices);
}

// Preserve advanced board settings when editing the basic display/ROM options.
void edit_pci_vga_gloria(const char *title)
{
    FormEntry_t entry[] = {
        {"PCI slot", NULL, NULL,
         "Which PCI slot should the Gloria Synergy card be on?\n"
         "Use bus 0 for a firmware graphics console.",
         validation_pcislot},
        {"rom file", "rom/SYN80700.PAN", "rom",
         "Path to the PCI GLoria option ROM.\n"
         "AGP GLoria images are not supported by this PCI card model.",
         validation_file},
        {"ROM layout", "exact", "rom_layout",
         "exact requires a complete 64 KiB ROM dump.\n"
         "mirror32 repeats a 32 KiB image in the upper half.\n"
         "pad_ff maps a 32 KiB image with the upper half filled with 0xFF.",
         validation_gloria_rom_layout},
        {"subsystem IDs from ROM?", STR_NO, "subsystem_from_rom",
         "Use the subsystem IDs at the end of the decoded ROM aperture.\n"
         "Do not combine this with explicit subsystem IDs or a pad_ff image.",
         validation_yes_no},
        {"allow DMA?", STR_YES, "allow_dma",
         "Allow the graphics card to read commands and data from guest memory\n"
         "and transfer output data back to guest memory.\n"
         "Transfers run when the guest enables PCI bus mastering.",
         validation_yes_no}
    };

    edit_pcicard(title, "gloria", entry, ARRAY_SIZE(entry), check_pci_vga_gloria);
}

bool check_pci_vga_radeon(FormEntry_t entry[], int num_entries, FormValues_t values)
{
    if (!strcmp(values[fentry_index(entry, num_entries, "rom file")], ""))
        return false;
    CConfigurator *occupant = get_pcislot(values[fentry_index(entry, num_entries, "PCI slot")]);
    if (occupant && occupant != sys0->find_child_myValue("radeon"))
    {
        show_text("Error", "That PCI slot belongs to another device. Choose a free slot.");
        return false;
    }
    return true;
}

void validation_radeon_rom_layout(EditorField *field)
{
    const char *choices[] = {"exact", "pad_ff", NULL};
    editor_choices(field, choices);
}

// Preserve advanced board settings when editing the basic display/ROM options.
void edit_pci_vga_radeon(const char *title)
{
    FormEntry_t entry[] = {
        {"PCI slot", NULL, NULL,
         "Which PCI slot should the Radeon 7500 card be on?\n"
         "Use bus 0 for a firmware graphics console.",
         validation_pcislot},
        {"rom file", "rom/r7500pci.rom", "rom",
         "Path to the PCI Radeon 7500 (1002:5157) option ROM.\n"
         "AGP images are not supported by this PCI card model.",
         validation_file},
        {"ROM layout", "pad_ff", "rom_layout",
         "exact requires a power-of-two ROM dump.\n"
         "pad_ff pads a smaller image, such as a 48 KiB PCI ROM, with 0xFF.",
         validation_radeon_rom_layout}
    };

    edit_pcicard(title, "radeon", entry, ARRAY_SIZE(entry), check_pci_vga_radeon);
}

bool check_pci_vga_powerstorm(FormEntry_t entry[], int num_entries, FormValues_t values)
{
    if (!strcmp(values[fentry_index(entry, num_entries, "rom file")], ""))
        return false;
    CConfigurator *occupant = get_pcislot(values[fentry_index(entry, num_entries, "PCI slot")]);
    if (occupant && occupant != sys0->find_child_myValue("powerstorm"))
    {
        show_text("Error", "That PCI slot belongs to another device. Choose a free slot.");
        return false;
    }
    return true;
}

void validation_powerstorm_rom_layout(EditorField *field)
{
    const char *choices[] = {"exact", "pad_ff", NULL};
    editor_choices(field, choices);
}

void validation_powerstorm_model(EditorField *field)
{
    const char *choices[] = {"300", "350", NULL};
    editor_choices(field, choices);
}

// Preserve advanced board settings when editing the basic display/ROM options.
void edit_pci_vga_powerstorm(const char *title)
{
    FormEntry_t entry[] = {
        {"PCI slot", NULL, NULL,
         "Which PCI slot should the PowerStorm card be on?\n"
         "Use bus 0 for a firmware graphics console.",
         validation_pcislot},
        {"rom file", "rom/BIOS.BIN", "rom",
         "Path to the PowerStorm 300/350 (10BA:0304) option ROM.\n"
         "Standard VGA and native 24-bit 2D are supported.",
         validation_file},
        {"ROM layout", "exact", "rom_layout",
         "exact requires a power-of-two ROM dump, such as the 64 KiB BIOS.\n"
         "pad_ff pads a smaller image with 0xFF.",
         validation_powerstorm_rom_layout},
        {"model", "300", "model",
         "300: subsystem 0E11:4D31, 15 MiB framebuffer, 16 MiB texture.\n"
         "350: subsystem 1011:4D35, 30 MiB framebuffer, 32 MiB texture.\n"
         "Memory population and board straps follow the selected model.",
         validation_powerstorm_model}
    };

    edit_pcicard(title, "powerstorm", entry, ARRAY_SIZE(entry), check_pci_vga_powerstorm);
}

// Form for VGA PCI card
void edit_pci_vga(const char *title)
{
    MenuEntry_t entry[] = {
        {"none", "No graphics card", NULL},
        {"s3", "S3 Trio 64", edit_pci_vga_s3},
        {"gloria", "ELSA GLoria Synergy (Permedia 2)", edit_pci_vga_gloria},
        {"radeon", "ATI Radeon 7500 (RV200)", edit_pci_vga_radeon},
        {"powerstorm", "Digital PowerStorm 300/350 (REALimage 2100)", edit_pci_vga_powerstorm}
#if 0 && defined(HAVE_CIRRUS)
        ,
        {"cirrus", "Cirrus CL-GD542x", edit_pci_vga_cirrus}
#endif
    };
    int num_entries = ARRAY_SIZE(entry);

    int sel = show_menu(title, entry, num_entries, MENU_3RD_LEVEL);

    if (sel == 0)
    {
        // Remove any VGA cards that may have been added
        CConfigurator *c;
        for (int i = 1; i < num_entries; ++i)
            while ((c = sys0->find_child_myValue(entry[i].text)) != nullptr)
                sys0->remove_child(c->get_myName());
    }
}

void validation_pci_dec21143_type(EditorField *field)
{
    vector<const char *> choices;

#ifdef HAVE_PCAP
    choices.push_back("pcap");
#endif
#ifdef HAVE_TAP_NET
    choices.push_back("tap");
#endif
#ifdef HAVE_VMNET
    choices.push_back("vmnet");
#endif
#ifdef HAVE_SLIRP
    choices.push_back("slirp");
#endif
    choices.push_back(NULL);

    editor_choices(field, choices.data());
}

void validation_pci_dec21143_adapter(EditorField *field)
{
    vector<const char *> choices;

    choices.push_back("list");

#ifdef HAVE_PCAP
    /* Get a list of network interfaces and
     * add them to the list.
     */
    pcap_if_t *alldevs = nullptr;
    pcap_if_t *d;
    char errbuf[PCAP_ERRBUF_SIZE];

    if (pcap_findalldevs(&alldevs, errbuf) == -1)
    {
        /* No devices to add. */
        string msg = "Error in pcap_findalldevs:\n" + string(errbuf);
        show_text("Error", msg.c_str());
    }
    else
    {
        for (d = alldevs; d; d = d->next)
            choices.push_back(d->name);
    }
#endif
    choices.push_back(NULL);

    editor_choices(field, choices.data());
#ifdef HAVE_PCAP
    if (alldevs) pcap_freealldevs(alldevs);
#endif
}

void validation_pci_dec21143_queue(EditorField *field)
{
    editor_number(field, EditorField::Type::Integer, 1, 1, INT_MAX);
}

// The emulator falls back to pcap, and to asking for an adapter on its
// console, when a NIC has no type. Always write the type so the file says
// what the user chose and the machine starts the same way from the editor,
// a terminal or a service.
void set_nic_values(CConfigurator *c, FormEntry_t entry[], int num_entries, FormValues_t values)
{
    set_pcicard_values_default(c, entry, num_entries, values);
    int idx = fentry_index(entry, num_entries, "type");
    set_form_value(c, idx, "type", values[idx], true);
}

// Form for DEC21143 PCI NIC
void edit_pci_dec21143(const char *title)
{
#if !defined(HAVE_PCAP) && !defined(HAVE_TAP_NET) && !defined(HAVE_VMNET) && !defined(HAVE_SLIRP)
    show_text("Error", "This program has been compiled with no network support.");
    return;
#else
    FormEntry_t entry[] = {
        {"PCI slot", NULL, NULL,
         "Which PCI slot should the NIC be on?\n"
         "Only free PCI slots are listed.",
         validation_pcislot},
        {"type",
         // slirp first: it needs no privileges, no host interface and no
         // console prompt, so a machine started from the editor just works.
#if defined(HAVE_SLIRP)
         "slirp",
#elif defined(HAVE_PCAP)
         "pcap",
#elif defined(HAVE_TAP_NET)
         "tap",
#elif defined(HAVE_VMNET)
         "vmnet",
#else
         "none",
#endif
         "type",
         "How should this NIC connect to the host network?"
#ifdef HAVE_PCAP
         "\n"
         "'pcap' captures/injects packets on an existing host adapter\n"
         "(works everywhere, but usually requires a bridge/promiscuous\n"
         "setup on the host to reach a wired LAN)."
#endif
#ifdef HAVE_TAP_NET
         "\n"
         "'tap' uses a TAP virtual network device (Linux/FreeBSD/NetBSD only),\n"
         "which plays more nicely with host firewalling and bridging."
#endif
#ifdef HAVE_VMNET
         "\n"
         "'vmnet' (MacOS only) uses the MacOS vmnet framework which uses\n"
         "a host's network interface as a bridge to effectively place\n"
         "the Alpha system on the same subnet as the host."
#endif
#ifdef HAVE_SLIRP
         "\n"
         "'slirp' gives the guest a private user-space network with NAT,\n"
         "DHCP and DNS. Needs no host privileges and no pre-created\n"
         "interface, but the guest is not reachable from the host LAN."
#endif
         ,
         validation_pci_dec21143_type},
#if defined(HAVE_PCAP)
        {"adapter", "list", "adapter",
         "For type 'pcap' or 'tap': the host network interface to use.\n"
         "'list' makes the emulator print the interfaces and ask on its\n"
         "console at start-up; that needs a terminal, so pick a real\n"
         "interface here when the machine is started from this editor.\n"
         "Capturing on a host interface needs root or CAP_NET_RAW.",
         validation_pci_dec21143_adapter},
#endif
        {"mac", "08-00-2B-E5-40-00", "mac",
         "What should the NIC's MAC address be?\n"
         "This should be unique on your network.",
         validation_mac},
        {"queue", "1024", "queue",
         "The number of frames the receive queue can hold between the host\n"
         "capture interface and the emulated NIC. Frames that arrive while the\n"
         "queue is full are dropped (and reported on the console).",
         validation_pci_dec21143_queue},
        {"crc?", STR_NO, "crc",
         "Calculate a real ethernet CRC (FCS) for frames delivered to the\n"
         "guest. When false, a zeroed placeholder CRC is appended instead,\n"
         "which saves host CPU; guests normally never check it",
         validation_yes_no},
        {"trace packets?", STR_NO, "trace_packets",
         "Dump every transmitted and received packet to the console,\n"
         "for network debugging. Very noisy.",
         validation_yes_no}
#ifdef HAVE_SLIRP
        ,
        {"network", "10.0.2.0/24", "network",
         "For type 'slirp': the private IPv4 subnet the guest lives on\n"
         "(address/prefix, e.g. 10.0.2.0/24).",
         NULL},
        {"gateway", "10.0.2.2", "gateway",
         "For type 'slirp': the address the guest uses as its gateway.\n"
         "Must be inside the network above.",
         NULL},
        {"dhcp start", "10.0.2.15", "dhcp_start",
         "For type 'slirp': first address handed out by the built-in\n"
         "DHCP server.",
         NULL},
        {"dhcp end", "10.0.2.30", "dhcp_end",
         "For type 'slirp': last address handed out by the built-in\n"
         "DHCP server.",
         NULL},
        {"dns", "", "dns",
         "For type 'slirp': comma-separated DNS servers handed to the\n"
         "guest. Leave empty to use the host's own resolvers.",
         NULL},
        {"dhcp lease", "3600", "dhcp_lease",
         "For type 'slirp': DHCP lease length, in seconds.",
         NULL}
#endif
        /*
        ,
        {"autonegotiate_delay?", STR_NO, "autonegotiate_delay",
        "Hidden option: defer SIA autonegotiation completion ~50ms.",
        validation_yes_no}
        */
#if 0 && defined(HAVE_VMNET)
        ,
        {"drop_privileges?", STR_YES, "drop_privileges",
         "For vmnet interfaces, controls whether privileges are dropped after the\n"
         "interface is initialized. (ES40 starts with privileges enabled but\n"
         "doesn't need privileges once the network interfaces are initialized.)\n"
         "If you configure multiple network interfaces with vmnet, set this\n"
         "variable to false on all but the interface in the highest PCI slot.",
         validation_yes_no}
#endif
    };

    edit_pcicard(title, "dec21143", entry, ARRAY_SIZE(entry), NULL, set_nic_values);
#endif // defined(HAVE_PCAP) || defined(HAVE_TAP_NET) || defined(HAVE_VMNET) || defined(HAVE_SLIRP)
}

void add_pci_sym53c810_disks(CConfigurator *c)
{
    add_scsi_disks("Add disks", 7, c);
}

// Menu for Symbios 53c819 SCSI PCI card
void edit_pci_sym53c810(const char *title)
{
    FormEntry_t entry[] = {
        {"PCI slot", NULL, NULL,
         "Which PCI slot should the Symbios 53C810 narrow SCSI controller be on?\n"
         "Only free PCI slots are listed.",
         validation_pcislot}
    };

    edit_pcicard(title, "sym53c810", entry, ARRAY_SIZE(entry), NULL, set_pcicard_values_default, add_pci_sym53c810_disks);
}

bool check_pci_lsi53c1020_settings(FormEntry_t entry[], int num_entries, FormValues_t values)
{
    if (!strcmp(values[fentry_index(entry, num_entries, "persistant flash?")], STR_YES) &&
        !strcmp(values[fentry_index(entry, num_entries, "flash file")], ""))
    {
        show_text("Error", "No flash filename has been specified.");
        return false;
    }

    if (!strcmp(values[fentry_index(entry, num_entries, "initial LSI BIOS and IOC firmware images?")], STR_YES))
    {
        if (!strcmp(values[fentry_index(entry, num_entries, "rom file")], ""))
        {
            show_text("Error", "No ROM filename has been specified.");
            return false;
        }

        if (!strcmp(values[fentry_index(entry, num_entries, "firmware file")], ""))
        {
            show_text("Error", "No firmware filename has been specified.");
            return false;
        }
    }
    return true;
}

void set_pci_lsi53c1020_values(CConfigurator *c, FormEntry_t entry[], int num_entries, FormValues_t values)
{
    int idx = fentry_index(entry, num_entries, "PCI slot");
    c->set_value(strdup("pci_slot"), strdup(values[idx]));

    idx = fentry_index(entry, num_entries, "persistant flash?");
    if (!strcmp(values[idx], STR_YES))
    {
        idx = fentry_index(entry, num_entries, "flash file");
        set_form_value(c, idx, entry[idx].name, values[idx], true);
    }

    idx = fentry_index(entry, num_entries, "initial LSI BIOS and IOC firmware images?");
    if (!strcmp(values[idx], STR_YES))
    {
        idx = fentry_index(entry, num_entries, "rom file");
        set_form_value(c, idx, entry[idx].name, values[idx], true);

        idx = fentry_index(entry, num_entries, "firmware file");
        set_form_value(c, idx, entry[idx].name, values[idx], true);
    }
}

void add_pci_lsi53c1020_disks(CConfigurator *c)
{
    add_scsi_disks("Add disks", 16, c);
}

// Form for LSI 53c1020 SCSI PCI card settings
void edit_pci_lsi53c1020(const char *title)
{
    FormEntry_t entry[] = {
        {"PCI slot", NULL, NULL,
         "Which PCI slot should the LSI 53C1020 Fusion-MPT Ultra320 SCSI controller be on?\n"
         "Only free PCI slots are listed.",
         validation_pcislot},
        {"persistant flash?", STR_NO, NULL,
         "Do you want the LSI card flash to persist between emulator runs?\n"
         "The optional backing file is a raw 512 KiB image of the card's flash.\n"
         "If it does not exist, ES40 creates it when possible. Without a flash backing\n"
         "file, BIOS and IOC firmware changes remain volatile.",
         validation_yes_no},
        {"flash file",
#if defined(_WIN32)
         "rom\\lsi53c1020.flash",
#elif defined(__VMS)
         "[.ROM]LSI53C1020.FLASH",
#else
         "rom/lsi53c1020.flash",
#endif
         "flash",
         "Where should the LSI card flash image be stored?\n"
         "Use a different 512 KiB backing file for each emulated controller.\n"
         "An existing image always takes precedence over seed files.",
         validation_file},
        {"initial LSI BIOS and IOC firmware images?", STR_NO, NULL,
         "These files seed the card only when no persistent flash imagehas been loaded.\n"
         "They are not reapplied on later starts, so changes made by firmware or\n"
         "an operating system remain intact. The behavioral controller starts without either seed.",
         validation_yes_no},
        {"rom file",
#if defined(_WIN32)
         "rom\\mptps.rom",
#elif defined(__VMS)
         "[.ROM]MPTPS.ROM",
#else
         "rom/mptps.rom",
#endif
         "rom",
         "Where can the LSI PCI BIOS image be found?\n"
         "Use the unmodified mptps.rom file from a compatible LSI firmware package.\n"
         "It is used only when initializing card flash. AlphaBIOS can execute this\n"
         "x86 ROM and expose attached disks through SCSIBIOS; SRM does not use it.",
         validation_file},
        {"firmware file",
#if defined(_WIN32)
         "rom\\it_1030.fw",
#elif defined(__VMS)
         "[.ROM]IT_1030.FW",
#else
         "rom/it_1030.fw",
#endif
         "firmware",
         "Where can the LSI IOC firmware image be found?\n"
         "Use the supplied it_1030.fw compatibility image for the plain 53C1020;\n"
         "the T image is for 53C1020A/1030T controllers. The image is used only\n"
         "when initializing flash, and does not change the emulated card's 53C1020 identity.",
         validation_file}
    };

    edit_pcicard(title, "lsi53c1020", entry, ARRAY_SIZE(entry),
        check_pci_lsi53c1020_settings, set_pci_lsi53c1020_values, add_pci_sym53c810_disks);
}

// Form for Ensoniq ES1370 PCI card
void edit_pci_es1370(const char *title)
{
    FormEntry_t entry[] = {
        {"PCI slot", NULL, NULL,
         "Which PCI slot should the Ensoniq AudioPCI ES1370 sound card be on?\n"
         "Only free PCI slots are listed.",
         validation_pcislot}
    };

    edit_pcicard(title, "es1370", entry, ARRAY_SIZE(entry));
}

void validation_audio_latency(EditorField *field)
{
    editor_number(field, EditorField::Type::Integer, 1, 10, 500);
}

// Form for Ensoniq ES1371 PCI card
void edit_pci_es1371(const char *title)
{
    FormEntry_t entry[] = {
        {"PCI slot", NULL, NULL,
         "Which PCI slot should the Ensoniq AudioPCI ES1371 sound card be on?\n"
         "Only free PCI slots are listed.",
         validation_pcislot},
        {"Audio latency (ms)", "40", "latency_ms",
         "How much playback audio should be queued on the host?\n"
         "Use 10 to 500 milliseconds. The default is 40.",
         validation_audio_latency}
    };

    edit_pcicard(title, "es1371", entry, ARRAY_SIZE(entry));
}

// Menu for PCI bus
void edit_pci(const char *title)
{
    MenuEntry_t entry[] = {
        {"List PCI bus", "Show a list of devices on the PCI bus.", show_pcislots},
        {"Add VGA", "Add a VGA card", edit_pci_vga},
        {"Add NIC", "Add a DEC 21143 Network Interface Card", edit_pci_dec21143},
        {"Add Symbios SCSI", "Add a Symbios 53C810 narrow SCSI controller (most tested/proven for OS boot/install)", edit_pci_sym53c810},
        {"Add LSI SCSI", "Add a LSI 53C1020 Fusion-MPT Ultra320 SCSI controller (NOT SRM BOOT CAPABLE!)", edit_pci_lsi53c1020},
        {"Add ES1370 Audio", "Add an Ensoniq AudioPCI ES1370 sound card (works only with Windows NT 4.0 guest)", edit_pci_es1370},
        {"Add ES1371 Audio", "Add an Ensoniq AudioPCI ES1371 sound card with an AC'97 codec (requires an ES1371 guest driver)", edit_pci_es1371}
    };
    int num_entries = ARRAY_SIZE(entry);

    show_menu(title, entry, num_entries, MENU_2ND_LEVEL);
}

/**
 * Serial lines configuration
 **/

void validation_serial_port(EditorField *field)
{
    editor_number(field, EditorField::Type::Integer, 1, 1, USHRT_MAX);
}

bool check_serial(FormEntry_t entry[], int num_entries, FormValues_t values)
{
    for (int i = 0; i < 2; ++i)
    {
        string serial_prefix = "serial" + i2s(i) + ".";
        int disabled_idx = fentry_index(entry, num_entries, (serial_prefix + "disabled").c_str());
        int null_idx = fentry_index(entry, num_entries, (serial_prefix + "null_attach").c_str());
        if (!strcmp(values[disabled_idx], STR_YES) || !strcmp(values[null_idx], STR_YES))
            continue;

        int address_idx = fentry_index(entry, num_entries, (serial_prefix + "listen_address").c_str());
        vector<string> listen_addresses;
        string error;
        if (!serial_parse_listen_addresses(values[address_idx], listen_addresses, error))
        {
            string message = serial_prefix + "listen_address: " + error;
            show_text("Error", message.c_str());
            return false;
        }
        string address_value;
        if (string(values[address_idx]).find_first_not_of(" \t\r\n") != string::npos)
        {
            for (const string& address : listen_addresses)
            {
                if (!address_value.empty())
                    address_value += ", ";
                address_value += address;
            }
        }
        free(values[address_idx]);
        values[address_idx] = strdup(address_value.c_str());

        string serial_program = serial_prefix + "program";
        int idx = fentry_index(entry, num_entries, serial_program.c_str());
        if (strchr(values[idx], '"'))
        {
            show_text("Error", "No double quotes are allowed in the program paths.");
            return false;
        }
    }
    return true;
}

// Form for serial lines
void edit_serial(const char *title)
{
    const int num_attr = 8;
    FormEntry_t entry[num_attr * 2];

    for (int i = 0; i < 2; ++i)
    {
        string serial_name = "serial" + i2s(i);
        string serial_prefix = serial_name + ".";
        entry[num_attr * i] = {
            strdup((serial_prefix + "disabled").c_str()), STR_NO, "disabled",
            "Make the guest see no UART at this address at all, so drivers skip\n"
            "the port completely. No Telnet port is opened. Unlike null_attach,\n"
            "the UART appears absent rather than present-but-idle.\n"
            "If enabled, all other parameters refering to this serial port will be ignored.",
            validation_yes_no};

        entry[num_attr * i + 1] = {
            strdup((serial_prefix + "null_attach").c_str()), STR_NO, "null_attach",
            "If 'yes' is selected, the UART exists on the bus and presents itself\n"
            "to the guest as a healthy idle 16550 (THRE/TSRE, CTS/DSR), but no telnet listener\n"
            "is opened and any bytes the guest transmits are silently discarded.\n"
            "Useful since two are required by the platform firmwares in case you don't need them.\n"
            "If enabled, all other parameters refering to this serial port will be ignored.",
            validation_yes_no};

        const string port_value = i2s(21264 + i);
        entry[num_attr * i + 2] = {
            strdup((serial_prefix + "port").c_str()), strdup(port_value.c_str()), "port",
            "The telnet port the serial device will listen on (in the range 1..65535).",
            validation_serial_port};

        entry[num_attr * i + 3] = {
            strdup((serial_prefix + "listen_address").c_str()), "", "listen_address",
            "Comma-separated numeric IPv4/IPv6 addresses.\n"
            "Blank: all IPv4 and IPv6 interfaces.\n"
            "Use 0.0.0.0 for IPv4 only, :: for IPv6 only, or 127.0.0.1, ::1 for local access.\n"
            "IPv6 scopes: fe80::1%3. No hostnames or ports.\n"
            "Update client arguments; IPv6 URL hosts need brackets.",
            NULL};

        entry[num_attr * i + 4] = {
            strdup((serial_prefix + "raw_mode").c_str()), STR_NO, "raw_mode",
            "Pass the byte stream through unmodified: no Telnet protocol (IAC)\n"
            "processing and no throttling to emulated line speed. Required when\n"
            "the port carries a binary protocol such as windbg (KD) or kgdb\n"
            "instead of a terminal session.",
            validation_yes_no};

        entry[num_attr * i + 5] = {
            strdup((serial_prefix + "wait_for_connection").c_str()), STR_YES, "wait_for_connection",
            "Hold the emulator at start-up until a telnet client connects to this port.\n"
            "Choose 'no' for a machine whose console is the graphics window, or that\n"
            "runs unattended: the port then accepts a client at any time and output\n"
            "sent before one connects is dropped.",
            validation_yes_no};

        entry[num_attr * i + 6] = {
            strdup((serial_prefix + "program").c_str()),
#if defined(_WIN32)
            "C:\\Program Files\\Putty\\Putty.exe",
#else
            "putty",
#endif
            NULL,
            "The program that should be started automatically to connect to the serial device listener.\n"
            "Enter the path to a program to start this to create an automatic connection with the serial port.\n"
            "No double quotes are allowed in the path.\n"
            "Set to an empty value to establish the connection manually.\n"
            "In that case, the arguments parameter is ignored.",
            validation_file};

        const string arguments_value = "telnet://localhost:" + port_value;
        entry[num_attr * i + 7] = {
            strdup((serial_prefix + "arguments").c_str()), strdup(arguments_value.c_str()), NULL,
            "Client arguments. Choose one listen address.\n"
            "For wildcards, use localhost (IPv4) or [::1] (IPv6).\n"
            "Bracket IPv6 URL hosts, e.g. telnet://[::1]:21264.",
            NULL};
    }
    const int num_entries = ARRAY_SIZE(entry);
    CConfigurator *c;
    FormValues_t preset;
    int idx;

    preset = (FormValues_t)calloc(num_entries, sizeof(char *));

    const string dquotes = "\"";
    for (int i = 0; i < 2; ++i)
    {
        string serial_name = "serial" + i2s(i);
        c = sys0->find_child(serial_name.c_str());
        for (int k = i * num_attr; k < (i + 1) * num_attr; ++k)
        {
            if (entry[k].name != NULL)
            {
                char *p;
                if (c != nullptr && (p = c->get_text_value(entry[k].name)) != NULL)
                    preset[k] = p;
                else
                    preset[k] = (char *)entry[k].preset;
                continue;
            }

            if (c == nullptr)
            {
                preset[k] = (char *)entry[k].preset;
                continue;
            }

            char *action = c->get_text_value("action", NULL);
            if (action == NULL)
            {
                preset[k] = (char *)"";
                continue;
            }

            // Split action into program & arguments
            static char program[2][256], arguments[2][256];
            string action_str = action;
            string::size_type p1, p2, p3; // p1=start of program, p2=end of program, p3=after end of program
            if (!action_str.empty() && action_str[0] == '"')
            {
                // Model values are decoded; the source writer escapes them once.
                const string quote = dquotes;
                p1 = quote.length();
                p2 = action_str.find(quote, p1);
                if (p2 == string::npos)
                    FAILURE_1(Configuration, "No closing double quotes in %s", entry[k].label);
                p3 = p2 + quote.length();
            }
            else
            {
                p1 = 0;
                p2 = action_str.find(" ");
                if (p2 == string::npos)
                    p2 = action_str.length();
                p3 = p2;
            }
            if (k % num_attr == 6)
            {
                memset(program[i], '\0', sizeof(program[i]));
                strncpy(program[i], action_str.substr(p1, p2 - p1).c_str(), sizeof(program[i]) - 1);
                preset[k] = program[i];
            }
            else if (k % num_attr == 7)
            {
                memset(arguments[i], '\0', sizeof(arguments[i]));
                if (p3 < action_str.length())
                    strncpy(arguments[i], action_str.substr(p3 + 1).c_str(), sizeof(arguments[i]) - 1);
                preset[k] = arguments[i];
            }
            else
                FAILURE_1(Logic, "No preset for %s", entry[k].label);
        }
    }

    FormValues_t values = show_form(title, entry, preset, num_entries, check_serial);

    if (values != NULL)
    {
        for (int i = 0; i < 2; ++i)
        {
            string serial_name = "serial" + i2s(i);
            string serial_prefix = serial_name + ".";
            c = sys0->find_child(serial_name.c_str());
            bool edited = false;
            for (int k = i * num_attr; k < (i + 1) * num_attr; ++k) edited = edited || changed_fields[k];
            if (!c && !edited) continue;
            c = edit_node(sys0, c, serial_name, "serial");

            idx = fentry_index(entry, num_entries, (serial_prefix + "disabled").c_str());
            set_form_value(c, idx, entry[idx].name, values[idx]);
            if (!strcmp(values[idx], STR_YES)) continue;

            idx = fentry_index(entry, num_entries, (serial_prefix + "null_attach").c_str());
            set_form_value(c, idx, entry[idx].name, values[idx]);
            if (!strcmp(values[idx], STR_YES)) continue;

            idx = fentry_index(entry, num_entries, (serial_prefix + "port").c_str());
            set_form_value(c, idx, entry[idx].name, values[idx]);

            idx = fentry_index(entry, num_entries, (serial_prefix + "listen_address").c_str());
            if (values[idx][0]) set_form_value(c, idx, entry[idx].name, values[idx]);
            else if (changed_fields[idx]) c->remove_value((char *)entry[idx].name);

            idx = fentry_index(entry, num_entries, (serial_prefix + "raw_mode").c_str());
            set_form_value(c, idx, entry[idx].name, values[idx]);

            idx = fentry_index(entry, num_entries, (serial_prefix + "wait_for_connection").c_str());
            set_form_value(c, idx, entry[idx].name, values[idx]);

            idx = fentry_index(entry, num_entries, (serial_prefix + "program").c_str());
            if (strcmp(values[idx], ""))
            {
                int idx2 = fentry_index(entry, num_entries, (serial_prefix + "arguments").c_str());
                // TODO: Check program path to determine whether dquotes are necessary
                string action = dquotes + values[idx] + dquotes + " " + values[idx2];
                set_form_value(c, idx, "action", action.c_str(), changed_fields[idx2]);
            }
            else if (changed_fields[idx]) c->remove_value((char *)"action");
        }

        // Clean up
        for (int i = 0; i < num_entries; ++i)
            free(values[i]);
        free(values);
    }

    // Clean up
    for (int i = 0; i < num_entries; ++i)
    {
        free((void *)entry[i].label);
        if (i % num_attr == 2 || i % num_attr == 7)
            free((void *)entry[i].preset);
    }
    free(preset);
}

/**
 * Floppy configuration
 **/

// Menu for floppy disks
void edit_floppy(const char *title)
{
    MenuEntry_t entry[] = {
        {"empty", "Don't add drives to the floppy controller", NULL},
        {"disk0.0", "Drive A: / DVA0:", NULL},
        {"disk0.1", "Drive B: / DVA1:", NULL}};
    int num_entries = ARRAY_SIZE(entry);

    CConfigurator *c = sys0->find_child("fdc0");
    if (c == nullptr)
        c = new CConfigurator(sys0, (char *)"fdc0", (char *)"floppy");

    while (true)
    {
        int sel = show_menu(title, entry, num_entries, MENU_2ND_LEVEL);

        if (sel < 0)
            break;
        if (sel == 0)
        {
            c->remove_all_children();
            break;
        }

        string subtitle = string(title) + ": " + entry[sel].text;
        add_disks(subtitle.c_str(), entry[sel].text, c);
    }
}

/**
 * IDE bus configuration
 **/

// Form for IDE bus settings
void edit_ide_settings(const char *title)
{
    FormEntry_t entry[] = {
        {"dma?", STR_YES, "dma",
         "Allow the guest to use (busmaster) DMA transfers on this\n"
         "IDE controller. Set to false to force PIO-only operation.",
         validation_yes_no}};
    int num_entries = ARRAY_SIZE(entry);
    CConfigurator *c;
    FormValues_t preset;
    int idx;

    c = sys0->find_child(PCI_SLOT_IDE);
    preset = (FormValues_t)calloc(num_entries, sizeof(char *));

    for (int i = 0; i < num_entries; ++i)
    {
        char *p;
        if (entry[i].name != NULL && c != nullptr && (p = c->get_text_value(entry[i].name)) != NULL)
            preset[i] = p;
        else
            preset[i] = (char *)entry[i].preset;
    }

    FormValues_t values = show_form(title, entry, preset, num_entries, NULL);

    if (values != NULL)
    {
        idx = fentry_index(entry, num_entries, "dma?");
        set_form_value(c, idx, entry[idx].name, values[idx]);

        // Clean up
        for (int i = 0; i < num_entries; ++i)
            free(values[i]);
        free(values);
    }
    free(preset);
}

// Menu for IDE bus disks
// One line saying what a disk slot holds, for slot menus.
static std::string describe_disk_slot(CConfigurator *disk)
{
    if (!disk) return "empty";
    const std::string type = disk->get_myValue() ? disk->get_myValue() : "";
    if (type == "ramdisk") return std::string("RAM disk ") + disk->get_text_value("size", "");
    const bool cdrom = disk->get_bool_value("cdrom");
    const char *path = disk->get_text_value(type == "device" ? "device" : "file", "");
    std::string result = cdrom ? "CD-ROM" : (type == "device" ? "host device" : "disk image");
    result += path && *path ? std::string(": ") + path : (cdrom ? ", no media" : ", no file");
    if (disk->get_bool_value("read_only")) result += " (read-only)";
    return result;
}

void edit_ide_disks(const char *title)
{
    static const char *const slot_names[] = {"primary master", "primary slave", "secondary master", "secondary slave"};
    static std::string descriptions[4];
    MenuEntry_t entry[] = {
        {"empty", "Remove all disks from the IDE controller.", NULL},
        {"disk0.0", NULL, NULL},
        {"disk0.1", NULL, NULL},
        {"disk1.0", NULL, NULL},
        {"disk1.1", NULL, NULL}};
    int num_entries = ARRAY_SIZE(entry);

    CConfigurator *c = sys0->find_child(PCI_SLOT_IDE);

    while (true)
    {
        for (int i = 1; i < num_entries; ++i)
        {
            descriptions[i - 1] = std::string(slot_names[i - 1]) + " \xe2\x80\x94 " +
                describe_disk_slot(c ? c->find_child(entry[i].text) : nullptr);
            entry[i].description = descriptions[i - 1].c_str();
        }
        int sel = show_menu(title, entry, num_entries, MENU_3RD_LEVEL);

        if (sel < 0)
            break;
        if (sel == 0)
        {
            c->remove_all_children();
            break;
        }

        string subtitle = string(title) + ": " + entry[sel].text;
        add_disks(subtitle.c_str(), entry[sel].text, c);
    }
}

// Menu for IDE bus
void edit_ide(const char *title)
{
    MenuEntry_t entry[] = {
        {"Settings", "Edit the IDE controller settings.", edit_ide_settings},
        {"Add disks", "Add disks to the IDE controller.", edit_ide_disks}};
    int num_entries = ARRAY_SIZE(entry);

    CConfigurator *c = sys0->find_child(PCI_SLOT_IDE);
    if (c == nullptr)
        c = new CConfigurator(sys0, (char *)PCI_SLOT_IDE, (char *)"ali_ide");

    show_menu(title, entry, num_entries, MENU_2ND_LEVEL);
}

/**
 * MPU-401 configuration (Windows only)
 **/

#ifdef _WIN32
void validation_mpu401_midiout(EditorField *field)
{
    editor_number(field, EditorField::Type::Integer, 1, 0, SHRT_MAX); // What is the maximum MIDI channel number?
}

// Form for MPU-401
void edit_mpu401(const char *title)
{
    FormEntry_t entry[] = {
        {"midi_out", "0", "midi_out",
         "The host MIDI output device number to play on.\n"
         "The default is 0 (the Windows default MIDI device).",
         validation_mpu401_midiout}};
    const int num_entries = ARRAY_SIZE(entry);
    CConfigurator *c;
    FormValues_t preset;
    int idx;

    c = sys0->find_child("mpu0");
    preset = (FormValues_t)calloc(num_entries, sizeof(char *));

    for (int i = 0; i < num_entries; ++i)
    {
        char *p;
        if (entry[i].name != NULL && c != nullptr && (p = c->get_text_value(entry[i].name)) != NULL)
            preset[i] = p;
        else
            preset[i] = (char *)entry[i].preset;
    }

    FormValues_t values = show_form(title, entry, preset, num_entries, NULL);

    if (values != NULL)
    {
        c = edit_node(sys0, c, "mpu0", "mpu401");

        idx = fentry_index(entry, num_entries, "midi_out");
        set_form_value(c, idx, entry[idx].name, values[idx]);

        // Clean up
        for (int i = 0; i < num_entries; ++i)
            free(values[i]);
        free(values);
    }
    free(preset);
}
#endif

/**
 * Menu to remove system components from the configuration
 */
void remove_component_add_choice(vector<MenuEntry_t> &choices, CConfigurator *c, string indent)
{
    char *myName = c->get_myName();
    string text;
    SelectFuncPtr remove_callback = NULL;
    
    if (!strcmp(myName, "sys0") || !strcmp(myName, "cpu0")) 
        text = indent + "(" + myName + "=" + c->get_myValue() + ")";
    else
    {
        text = indent + myName + "=" + c->get_myValue();
        // ToDo: construct a remove_callback that removes c
        // (capturing lambda doesn't work - std::function + std::bind ?)
        auto doesnotwork = [c](const char *) {
            c->get_myParent()->remove_child(c->get_myName());
        };
    }
    choices.push_back({strdup(text.c_str()), "", remove_callback});
    for (int i = 0; i < c->editor_child_count(); ++i)
        remove_component_add_choice(choices, c->editor_child(i), indent + "  ");
}

void remove_component(const char *title)
{
    int sel;
    do
    {
        vector<MenuEntry_t> choices;

        CConfigurator *theGui = theConfig->find_child("gui");
        if (theGui != nullptr)
            remove_component_add_choice(choices, theGui, "");
        remove_component_add_choice(choices, sys0, "");

        sel = show_menu(title, choices.data(), choices.size(), MENU_2ND_LEVEL);

        for (int i = 0; i < choices.size(); ++i)
            free((char*)choices[i].text);
        choices.clear();
    } while (sel >= 0);
}

/**
 * Main menu
 *
 * Returns true is results are to be saved.
 **/
bool main_menu(void)
{
    MenuEntry_t entry[] = {
        {"Edit GUI settings",
         "You need a GUI if you want to use an emulated graphics card.\n"
         "You don't need this for most OS'es. If you don't need this,\n"
         "we recommend that you select 'none'.",
         edit_gui},
        {"Edit System settings", "", edit_tsunami},
        {"Edit CPU settings", "", edit_ev68cb},
        {"Edit ALI 1543 settings", "", edit_ali},
        {"Edit PCI devices", "", edit_pci},
        {"Edit Serial devices", "", edit_serial},
        {"Edit Floppy devices", "", edit_floppy},
        {"Edit IDE devices", "", edit_ide}
#ifdef _WIN32
        ,
        {"MPU-401 device", "", edit_mpu401}
#endif
        ,
        {"Remove components",
         "Remove system components from the configuration.\n"
         "You can not remove the entries for sys0 or cpu0.\n"
         "Warning: this is a dummy and does not yet work.",
         remove_component},
        {NULL, NULL, NULL},
        {"Help", "Help with program usage", show_help},
        {"Quit", "Quit the program without writing to output file", NULL},
        {"Exit", "Save the results to the output file, and exit the program", NULL}};
    int num_entries = ARRAY_SIZE(entry);
    CConfigurator *c;
    bool save_results;

    bool check_failed;
    do
    {
        int sel = show_menu("Main menu", entry, num_entries, MENU_1ST_LEVEL);

        check_failed = false;
        save_results = (sel == MENU_SEL_EXIT || sel == num_entries - 1);
        if (save_results)
        {
            // Find any VGA card
            CConfigurator *vgacard;
            vgacard = sys0->find_child_myValue("s3");
            if (vgacard == nullptr)
                vgacard = sys0->find_child_myValue("gloria");
            if (vgacard == nullptr)
                vgacard = sys0->find_child_myValue("radeon");
            if (vgacard == nullptr)
                vgacard = sys0->find_child_myValue("powerstorm");
#if defined(HAVE_CIRRUS)
            if (vgacard == nullptr)
                vgacard = sys0->find_child_myValue("cirrus");
#endif

            c = sys0->find_child(PCI_SLOT_ALI);
            if (c != nullptr)
            {
                char *vga_console = c->get_text_value("vga_console", STR_NO);
                if (!strcmp(vga_console, STR_YES) && vgacard == nullptr)
                {
                    show_text("Error",
                              "You set vga_console=" STR_YES " (in the ALI settings)\n"
                              "without configuring a VGA card (on the PCI bus).");
                    check_failed = true;
                }
            }

            if (vgacard != nullptr)
            {
                c = theConfig->find_child("gui");
                if (c == nullptr)
                {
                    show_text("Error", "You configured a VGA card without configuring a GUI.");
                    check_failed = true;
                }
            }
        }
    } while (check_failed);

    return save_results;
}

enum OverwriteChoice
{
    OVERWRITE_BACK, // return to the main menu
    OVERWRITE_YES,  // write the file
    OVERWRITE_QUIT  // leave without writing
};

/**
 * Ask before replacing an existing output file.
 * A missing file or stdout ("-") needs no confirmation.
 **/
OverwriteChoice confirm_overwrite(const char *filename)
{
    if (!strcmp(filename, "-"))
        return OVERWRITE_YES;

    output_snapshot = read_config_file(filename, true);
    if (!output_snapshot->exists) return OVERWRITE_YES;

    string title = string(filename) + " exists. Overwrite?";
    MenuEntry_t entry[] = {
        {"No", "Return to the main menu without writing the file.", NULL},
        {"Yes", "Replace the existing file with this configuration.\n"
                "Comments and untouched settings are kept.\n"
                "The previous file is backed up as <filename>.bak.",
         NULL},
        {"Quit without saving", "Leave the program without writing the file.", NULL}};

    switch (show_menu(title.c_str(), entry, ARRAY_SIZE(entry), MENU_2ND_LEVEL))
    {
    case 1:
        return OVERWRITE_YES;
    case 2:
        return OVERWRITE_QUIT;
    default: // "No", or F2
        return OVERWRITE_BACK;
    }
}

int run_config_editor(int argc, char **argv, EditorUI& ui)
{
    editor_ui = &ui;
    configuration_document.reset(); input_snapshot.reset(); output_snapshot.reset();
    const char *out_filename = NULL;
    const char *in_filename = NULL;

    if (argc >= 2 && argv[1][0] != '\0')
    {
        if (!strcmp(argv[1], "-h") || !strcmp(argv[1], "-?"))
        {
            printf("Usage: %s [input-filename] [output-filename]\n"
                   "Without input-filename, es40.cfg in the current directory is read if present.\n"
                   "output-filename defaults to the input file, or es40.cfg for a new configuration.\n",
                   argv[0]);
            return 0;
        }
        in_filename = argv[1];
    }
    else if (argc < 2)
    {
        // No input file given: edit the es40.cfg in the current directory.
        // An explicit empty argument asks for a new machine instead.
        if (FILE *existing = fopen("es40.cfg", "rb"))
        {
            fclose(existing);
            in_filename = "es40.cfg";
        }
    }

    if (in_filename != NULL)
    {
        try
        {
            read_configuration(in_filename); // initializes theConfig
        }
        catch (CException &e)
        {
            // Don't fall back to a fresh configuration: saving it would overwrite the file.
            printf("Failure reading %s: %s\n", in_filename, e.displayText().c_str());
            return 1;
        }
        catch (const std::exception& e)
        {
            fprintf(stderr, "Failure reading %s: %s\n", in_filename, e.what());
            return 1;
        }
    }
    else
        new_configuration();
    sys0 = theConfig->find_child("sys0");
    if (sys0 == nullptr)
    {
        fprintf(stderr, "No 'sys0' in the configuration.\n");
        delete theConfig; theConfig = nullptr;
        return 1;
    }

    if (argc >= 3)
    {
        if (argv[2][0] == '\0')
        {
            fprintf(stderr, "Output filename is empty.\n");
            delete theConfig; theConfig = sys0 = nullptr;
            return 1;
        }
        out_filename = argv[2];
    }
    else
        out_filename = in_filename ? in_filename : "es40.cfg";

    bool initialized = false;
    int result = 0;
    std::string selected_output;
    try
    {
        initialized = true;
        ui.initialize();
        ui.banner();

        while (main_menu())
        {
            OverwriteChoice choice;
            if (ui.select_output(selected_output))
            {
                // The frontend confirmed any overwrite; drop a snapshot of an earlier target.
                out_filename = selected_output.c_str();
                output_snapshot.reset();
                choice = OVERWRITE_YES;
            }
            else
                choice = confirm_overwrite(out_filename);
            if (choice == OVERWRITE_YES)
            {
                write_configuration(out_filename);
                if (ui.keep_editing_after_save()) continue;
            }
            if (choice != OVERWRITE_BACK)
                break;
            // Back: edits kept and the file untouched.
        }
    }
    catch (const EditorClosed&) {}
    catch (CException &e)
    {
        if (initialized) { ui.shutdown(); initialized = false; }
        fprintf(stderr, "Failure: %s\n", e.displayText().c_str());
        result = 1;
    }
    catch (const std::exception& e)
    {
        if (initialized) { ui.shutdown(); initialized = false; }
        fprintf(stderr, "Failure: %s\n", e.what());
        result = 1;
    }

    if (initialized) ui.shutdown();
    delete theConfig; theConfig = sys0 = nullptr;
    editor_ui = nullptr;
    return result;
}

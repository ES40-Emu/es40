/* ES40 emulator -- GTK 3 configuration editor frontend.
 * Copyright (C) 2026 by the ES40 Emulator Project
 * SPDX-License-Identifier: BSD-1-Clause
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDERS AND CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

// Native desktop frontend for Linux/BSD.
//
// The machine is shown as a device tree on the left (system, processors,
// display, chipset, serial ports, floppy, IDE, and every PCI card with its
// disks) and the selected device's settings on the right. Settings apply to
// the in-memory model as they are edited; Save writes the file, patching the
// original so comments and settings the editor does not know survive. Devices
// are added and removed with the buttons under the tree, the context menu or
// the Delete key. The machine can be started and stopped from the header bar:
// the emulator runs as a child process in the configuration's directory with
// its output in <name>.log beside it, and a systemd unit for the same command
// can be generated for unattended use.
//
// Only the model (ConfigEditor.cpp: load, new, save, memory helpers) is shared
// with the other editors; the menu walkthrough they use is not.

#include "StdAfx.h" // u64 and the C runtime that Configurator.h relies on
#include "ConfigEditor.h"
#include "Configurator.h"
#include "io/SerialListenAddress.h"
#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include <ifaddrs.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#include <algorithm>
#include <climits>
#include <cstdio>
#include <cstring>
#include <exception>
#include <functional>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr int MAX_CPUS = 4;
// Bus 0 slots 1-4 and bus 1 slots 1-6 take cards; 0.7/0.15/0.17/0.19 are the chipset.
constexpr int PCI_BUSES = 2;
constexpr int PCI_SLOTS[PCI_BUSES] = {4, 6};
constexpr int LOW_MEM_BITS = 25, HI_MEM_BITS = 35; // 32 MB .. 32 GB
constexpr const char *HOTKEY_PATTERN =
    "^ *(none|((Ctrl|Alt|Shift|GUI|Control|Option|Command|Cmd|Super|Win|Windows|Meta)"
    "(\\+(Ctrl|Alt|Shift|GUI|Control|Option|Command|Cmd|Super|Win|Windows|Meta))*"
    "\\+(F[0-9]+|PageUp|PageDown|Insert|Delete|Escape|Home|End|Keypad \\+|Keypad -|PgUp|PgDn|PageDn|Ins|Del|Esc|KeypadPlus|KpPlus|KeypadMinus|KpMinus|[A-Z0-9]))) *$";
constexpr const char *HOTKEY_HELP =
    "Modifiers (Ctrl, Alt, Shift, GUI) joined with + and one key, e.g. Ctrl+F11. \"none\" disables it.";

// Configuration text is byte-oriented; GTK needs valid UTF-8 for display.
std::string utf8(const char *source)
{
    if (!source) return {};
    if (g_utf8_validate(source, -1, nullptr)) return source;
    if (gchar *converted = g_locale_to_utf8(source, -1, nullptr, nullptr, nullptr))
    {
        std::string result = converted; g_free(converted); return result;
    }
    gchar *repaired = g_utf8_make_valid(source, -1);
    std::string result = repaired; g_free(repaired); return result;
}
std::string utf8(const std::string& source) { return utf8(source.c_str()); }
std::string escape(const std::string& text)
{
    gchar *escaped = g_markup_escape_text(utf8(text).c_str(), -1);
    std::string result = escaped; g_free(escaped); return result;
}
std::string basename_of(const std::string& path) { return path.substr(path.find_last_of('/') + 1); }
std::string stem_of(const std::string& name)
{
    const size_t dot = name.rfind('.');
    return dot == std::string::npos || dot == 0 ? name : name.substr(0, dot);
}
std::string lower(std::string text) { for (auto& c : text) c = (char)tolower((unsigned char)c); return text; }
bool starts_with(const std::string& text, const char *prefix) { return text.rfind(prefix, 0) == 0; }

// Machines opened before, most recent first, in $XDG_CONFIG_HOME/es40/recent-machines.
std::string recent_list_path()
{
    const std::string directory = std::string(g_get_user_config_dir()) + "/es40";
    g_mkdir_with_parents(directory.c_str(), 0700);
    return directory + "/recent-machines";
}
std::vector<std::string> read_recent_machines()
{
    std::vector<std::string> result;
    gchar *contents = nullptr;
    if (!g_file_get_contents(recent_list_path().c_str(), &contents, nullptr, nullptr)) return result;
    std::istringstream lines(contents);
    g_free(contents);
    for (std::string line; std::getline(lines, line);)
        if (!line.empty() && g_file_test(line.c_str(), G_FILE_TEST_IS_REGULAR)) result.push_back(line);
    return result;
}
void remember_machine(const std::string& path, bool forget = false)
{
    gchar *absolute = g_canonicalize_filename(path.c_str(), nullptr);
    const std::string entry = absolute ? absolute : path;
    g_free(absolute);
    std::vector<std::string> machines = read_recent_machines();
    machines.erase(std::remove(machines.begin(), machines.end(), entry), machines.end());
    if (!forget) machines.insert(machines.begin(), entry);
    if (machines.size() > 8) machines.resize(8);
    std::string text;
    for (const auto& machine : machines) text += machine + "\n";
    g_file_set_contents(recent_list_path().c_str(), text.c_str(), (gssize)text.size(), nullptr);
}

// ---- model helpers ----------------------------------------------------------
// find_child() searches the whole tree; device names repeat (disk0.0 on every controller).
CConfigurator *child_of(CConfigurator *parent, const std::string& name)
{
    if (!parent) return nullptr;
    for (int i = 0; i < parent->editor_child_count(); ++i)
        if (name == parent->editor_child(i)->get_myName()) return parent->editor_child(i);
    return nullptr;
}
std::vector<CConfigurator *> children_of(CConfigurator *parent)
{
    std::vector<CConfigurator *> result;
    if (parent) for (int i = 0; i < parent->editor_child_count(); ++i) result.push_back(parent->editor_child(i));
    return result;
}
std::string text_of(CConfigurator *node, const char *key, const char *fallback = "")
{
    const char *value = node ? node->get_text_value(key) : nullptr;
    return value ? value : fallback;
}
bool bool_of(CConfigurator *node, const char *key, bool fallback = false)
{
    return node ? node->get_bool_value(key, fallback) : fallback;
}
std::string value_of(CConfigurator *node) { return node && node->get_myValue() ? node->get_myValue() : ""; }
std::string name_of(CConfigurator *node) { return node && node->get_myName() ? node->get_myName() : ""; }
void set_text(CConfigurator *node, const char *key, const std::string& value)
{ node->set_value(strdup(key), strdup(value.c_str())); }
void clear_key(CConfigurator *node, const char *key) { if (node) node->remove_value((char *)key); }
// Empty optional values are left out of the file rather than written as "".
void set_optional(CConfigurator *node, const char *key, const std::string& value)
{ if (value.empty()) clear_key(node, key); else set_text(node, key, value); }
void set_bool(CConfigurator *node, const char *key, bool value) { set_text(node, key, value ? "true" : "false"); }
CConfigurator *ensure_child(CConfigurator *parent, const std::string& name, const char *type)
{
    if (CConfigurator *existing = child_of(parent, name)) return existing;
    return new CConfigurator(parent, (char *)name.c_str(), (char *)type);
}
void remove_node(CConfigurator *node)
{
    if (node && node->get_myParent()) node->get_myParent()->remove_child(node->get_myName());
}
std::string pci_slot_of(const std::string& name) { return starts_with(name, "pci") ? name.substr(3) : name; }
bool is_card_slot(const std::string& name)
{
    int bus = -1, slot = -1;
    if (sscanf(name.c_str(), "pci%d.%d", &bus, &slot) != 2) return false;
    return bus >= 0 && bus < PCI_BUSES && slot >= 1 && slot <= PCI_SLOTS[bus];
}
std::vector<std::string> free_pci_slots(const std::string& keep = {})
{
    std::vector<std::string> result;
    for (int bus = 0; bus < PCI_BUSES; ++bus)
        for (int slot = 1; slot <= PCI_SLOTS[bus]; ++slot)
        {
            const std::string name = "pci" + std::to_string(bus) + "." + std::to_string(slot);
            if (name == keep || !child_of(sys0, name)) result.push_back(name.substr(3));
        }
    return result;
}
std::string memory_label(int bits)
{
    const std::string size = memory_bits2size(bits); // "256M", "1G"
    return size.substr(0, size.size() - 1) + (size.back() == 'G' ? " GB" : " MB");
}
std::vector<std::string> host_interfaces()
{
    std::vector<std::string> result;
    struct ifaddrs *list = nullptr;
    if (getifaddrs(&list)) return result;
    for (struct ifaddrs *entry = list; entry; entry = entry->ifa_next)
        if (entry->ifa_name && std::find(result.begin(), result.end(), entry->ifa_name) == result.end() && strcmp(entry->ifa_name, "lo"))
            result.push_back(entry->ifa_name);
    freeifaddrs(list);
    return result;
}

// ---- firmware images ----------------------------------------------------------
// A programmed 2 MiB ES40 flash carries a CPQ header at the start of its SRM partition.
constexpr size_t FLASH_SIZE = 2 * 1024 * 1024;
constexpr size_t FLASH_CPQ_OFFSET = 0x10000 + 0x14;
constexpr const char *FREE_VGABIOS = "/usr/share/vgabios/vgabios.bin"; // GNU vgabios, Debian/Ubuntu package "vgabios"
constexpr const char *FIRMWARE_SOURCES =
    "SRM is HP's firmware and cannot be shipped with the emulator. Sources: the HP Alpha Systems Firmware "
    "Update CD V7.3 (the ES40 image on it is CL67SRMROM.EXE), or the ready-programmed flash image at "
    "https://archive.org/details/es40-flasher (flash-programmed.zip). Import accepts the .exe, a 2 MB "
    "flash.rom, or the .zip/.iso that contains one, and puts it in the rom folder beside this machine.";
std::string read_head(const std::string& path, size_t count)
{
    std::string result;
    FILE *file = fopen(path.c_str(), "rb");
    if (!file) return result;
    result.resize(count);
    result.resize(fread(result.data(), 1, count, file));
    fclose(file);
    return result;
}
long long file_size(const std::string& path)
{
    struct stat info;
    return stat(path.c_str(), &info) ? -1 : (long long)info.st_size;
}
bool is_flash_image(const std::string& path)
{
    if (file_size(path) != (long long)FLASH_SIZE) return false;
    const std::string head = read_head(path, FLASH_CPQ_OFFSET + 4);
    return head.size() == FLASH_CPQ_OFFSET + 4 && head.compare(FLASH_CPQ_OFFSET, 4, std::string("CPQ\0", 4)) == 0;
}
bool is_archive(const std::string& path)
{
    const std::string head = read_head(path, 0x8006);
    if (head.size() >= 4 && head.compare(0, 4, "PK\x03\x04") == 0) return true;      // zip
    if (head.size() >= 6 && head.compare(0, 6, "7z\xbc\xaf\x27\x1c") == 0) return true; // 7z
    return head.size() >= 0x8006 && head.compare(0x8001, 5, "CD001") == 0;           // ISO 9660
}
bool looks_like_srm_update(const std::string& path)
{
    const std::string name = lower(basename_of(path));
    return name.find("srmrom") != std::string::npos && name.size() > 4 && name.compare(name.size() - 4, 4, ".exe") == 0;
}
// Runs a command, returns its exit status and output.
int run_command(const std::vector<std::string>& command, std::string& output)
{
    std::vector<char *> argv;
    for (const auto& a : command) argv.push_back(const_cast<char *>(a.c_str()));
    argv.push_back(nullptr);
    gchar *out = nullptr, *err = nullptr; gint status = -1;
    GError *error = nullptr;
    const gboolean ok = g_spawn_sync(nullptr, argv.data(), nullptr, G_SPAWN_SEARCH_PATH, nullptr, nullptr, &out, &err, &status, &error);
    output = out ? out : "";
    if (err && *err) output += err;
    g_free(out); g_free(err);
    if (!ok) { output = error ? error->message : "could not run command"; if (error) g_error_free(error); return -1; }
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}
bool have_program(const char *name) { gchar *found = g_find_program_in_path(name); const bool have = found != nullptr; g_free(found); return have; }
// Entries of a zip/iso/7z, through 7z, unzip or isoinfo, whichever exists.
bool archive_entries(const std::string& path, std::vector<std::string>& entries, std::string& error)
{
    std::string output;
    const std::string head = read_head(path, 4);
    const bool zip = head.compare(0, 4, "PK\x03\x04") == 0;
    if (have_program("7z"))
    {
        if (run_command({"7z", "l", "-ba", "-slt", path}, output) == 0)
        {
            std::istringstream lines(output);
            for (std::string line; std::getline(lines, line);)
                if (starts_with(line, "Path = ")) entries.push_back(line.substr(7));
            return true;
        }
    }
    if (zip && have_program("unzip"))
    {
        if (run_command({"unzip", "-Z1", path}, output) == 0)
        {
            std::istringstream lines(output);
            for (std::string line; std::getline(lines, line);) if (!line.empty()) entries.push_back(line);
            return true;
        }
    }
    if (!zip && have_program("isoinfo"))
    {
        if (run_command({"isoinfo", "-f", "-i", path}, output) == 0)
        {
            std::istringstream lines(output);
            for (std::string line; std::getline(lines, line);) if (!line.empty()) entries.push_back(line);
            return true;
        }
    }
    error = output.empty() ? "No tool to read the archive: install the 7zip package (sudo apt install 7zip), or extract it by hand and import the file inside." : output;
    return false;
}
// Extracts one entry into directory; returns the extracted file's path or "".
std::string archive_extract(const std::string& path, const std::string& entry, const std::string& directory, std::string& error)
{
    std::string output;
    const std::string target = directory + "/" + basename_of(entry);
    const bool zip = read_head(path, 4).compare(0, 4, "PK\x03\x04") == 0;
    if (have_program("7z") && run_command({"7z", "e", "-y", "-o" + directory, path, entry}, output) == 0 && g_file_test(target.c_str(), G_FILE_TEST_IS_REGULAR))
        return target;
    if (zip && have_program("unzip") && run_command({"unzip", "-o", "-j", "-d", directory, path, entry}, output) == 0 && g_file_test(target.c_str(), G_FILE_TEST_IS_REGULAR))
        return target;
    if (!zip && have_program("isoinfo"))
    {
        std::string iso_entry = entry;
        if (iso_entry.find(';') == std::string::npos) iso_entry += ";1";
        if (run_command({"isoinfo", "-i", path, "-x", iso_entry}, output) == 0 && !output.empty() &&
            g_file_set_contents(target.c_str(), output.data(), (gssize)output.size(), nullptr))
            return target;
    }
    error = output.empty() ? "Could not extract " + entry : output;
    return "";
}

// ---- a setting on the page -------------------------------------------------
struct Field
{
    enum class Kind { Heading, Note, Action, Text, File, Choice, Bool, Integer, Number };
    Kind kind = Kind::Text;
    std::string label, help, value;
    std::vector<std::string> choices;
    bool editable_choice = false;    // Choice: free text allowed as well
    double minimum = 0, maximum = 0; // Integer / Number
    int digits = 0;                  // Number
    std::function<bool(const std::string&)> validate;
    std::function<void(const std::string&)> store;
    bool rebuild = false;            // other settings depend on this one: redraw the page after a change
    std::string placeholder;
};
Field heading(const std::string& label) { Field f; f.kind = Field::Kind::Heading; f.label = label; return f; }
Field note(const std::string& text) { Field f; f.kind = Field::Kind::Note; f.label = text; return f; }
Field action(const std::string& label, const std::string& help, std::function<void()> run)
{ Field f; f.kind = Field::Kind::Action; f.label = label; f.help = help; f.store = [run](const std::string&) { run(); }; return f; }
Field text(const std::string& label, const std::string& value, const std::string& help, std::function<void(const std::string&)> store)
{ Field f; f.label = label; f.value = value; f.help = help; f.store = std::move(store); return f; }
Field pattern_text(const std::string& label, const std::string& value, const std::string& help, const char *pattern, std::function<void(const std::string&)> store)
{
    Field f = text(label, value, help, std::move(store));
    const std::regex expression(pattern, std::regex::extended);
    f.validate = [expression](const std::string& candidate) { return std::regex_match(candidate, expression); };
    return f;
}
Field file(const std::string& label, const std::string& value, const std::string& help, std::function<void(const std::string&)> store)
{ Field f = text(label, value, help, std::move(store)); f.kind = Field::Kind::File; return f; }
Field choice(const std::string& label, const std::string& value, std::vector<std::string> choices, const std::string& help, std::function<void(const std::string&)> store)
{
    Field f = text(label, value, help, std::move(store));
    f.kind = Field::Kind::Choice; f.choices = std::move(choices);
    return f;
}
Field boolean(const std::string& label, bool value, const std::string& help, std::function<void(bool)> store)
{
    Field f = text(label, value ? "true" : "false", help, [store](const std::string& v) { store(v == "true"); });
    f.kind = Field::Kind::Bool;
    return f;
}
Field integer(const std::string& label, const std::string& value, double minimum, double maximum, const std::string& help, std::function<void(const std::string&)> store)
{
    Field f = text(label, value, help, std::move(store));
    f.kind = Field::Kind::Integer; f.minimum = minimum; f.maximum = maximum;
    return f;
}
Field number(const std::string& label, const std::string& value, double minimum, double maximum, int digits, const std::string& help, std::function<void(const std::string&)> store)
{
    Field f = integer(label, value, minimum, maximum, help, std::move(store));
    f.kind = Field::Kind::Number; f.digits = digits;
    return f;
}
// Storing to a key of a device that may not exist in the file yet.
using Resolver = std::function<CConfigurator *()>;
// While a page is being built, every key it models is recorded here; keys the
// device has beyond those are shown in the page's Advanced section.
std::set<std::string> *page_keys = nullptr;
void handles(const std::string& key) { if (page_keys) page_keys->insert(key); }
std::function<void(const std::string&)> to_key(Resolver resolve, const char *key)
{ handles(key); return [resolve, key](const std::string& v) { set_text(resolve(), key, v); }; }
std::function<void(const std::string&)> to_optional_key(Resolver resolve, const char *key)
{ handles(key); return [resolve, key](const std::string& v) { set_optional(resolve(), key, v); }; }
std::function<void(bool)> to_bool_key(Resolver resolve, const char *key)
{ handles(key); return [resolve, key](bool v) { set_bool(resolve(), key, v); }; }

// ---- a row of the device tree ------------------------------------------------
struct Node
{
    enum class Kind { System, Cpu, Display, Chipset, SerialGroup, Serial, Floppy, Ide, Disk, Scsi, Vga, Nic, Audio, Other };
    Kind kind = Kind::Other;
    CConfigurator *cfg = nullptr; // null for a device the file does not have yet (serial1, fdc0 ...)
    std::string name;             // configuration name: sys0, cpu0, serial1, pci0.3, disk0.4
    std::string key;              // selection identity across rebuilds
    std::string title, subtitle, icon, identity;
    int parent = -1;
    bool removable = false;
};

// Disk slots of a controller with their labels and SRM device names.
struct Slot { std::string name, label, srm; };
std::vector<Slot> slots_of(CConfigurator *controller, const std::string& controller_name, const std::string& type)
{
    std::vector<Slot> result;
    if (controller_name == "fdc0")
        return {{"disk0.0", "Drive A:", "DVA0"}, {"disk0.1", "Drive B:", "DVA1"}};
    if (type == "ali_ide")
        return {{"disk0.0", "Primary master", "DQA0"}, {"disk0.1", "Primary slave", "DQA1"},
                {"disk1.0", "Secondary master", "DQB0"}, {"disk1.1", "Secondary slave", "DQB1"}};
    // SCSI: SRM letters controllers A, B, ... in bus/slot order.
    char letter = 'A';
    std::vector<std::string> scsi;
    for (auto card : children_of(sys0))
        if (value_of(card) == "sym53c810" || value_of(card) == "lsi53c1020") scsi.push_back(name_of(card));
    std::sort(scsi.begin(), scsi.end(), [](const std::string& a, const std::string& b)
    {
        int ab = 0, as = 0, bb = 0, bs = 0;
        sscanf(a.c_str(), "pci%d.%d", &ab, &as); sscanf(b.c_str(), "pci%d.%d", &bb, &bs);
        return ab != bb ? ab < bb : as < bs;
    });
    for (const auto& name : scsi) { if (name == controller_name) break; ++letter; }
    const int ids = type == "lsi53c1020" ? 16 : 7;
    for (int id = 0; id < ids; ++id)
        result.push_back({"disk0." + std::to_string(id), "SCSI ID " + std::to_string(id),
                          std::string("DK") + letter + std::to_string(id) + (id ? "00" : "")});
    (void)controller;
    return result;
}
std::string describe_disk(CConfigurator *disk, const std::string& directory)
{
    const std::string type = value_of(disk);
    if (type == "ramdisk") return "RAM disk, " + text_of(disk, "size", "?");
    const bool cdrom = bool_of(disk, "cdrom");
    const std::string path = text_of(disk, type == "device" ? "device" : "file");
    std::string result = cdrom ? "CD-ROM" : type == "device" ? "host device" : "disk image";
    result += path.empty() ? (cdrom ? ", no media" : ", no file") : ": " + path;
    const std::string host_path = path.empty() || g_path_is_absolute(path.c_str()) ? path : directory + "/" + path;
    if (!cdrom && type == "file" && !text_of(disk, "autocreate_size").empty() && !g_file_test(host_path.c_str(), G_FILE_TEST_EXISTS))
        result += " (created at first start, " + text_of(disk, "autocreate_size") + ")";
    if (!cdrom && bool_of(disk, "read_only")) result += ", read-only";
    return result;
}

} // namespace

class Editor
{
    // ---- state --------------------------------------------------------------
    GtkWidget *window = nullptr, *header = nullptr, *tree = nullptr, *page_box = nullptr, *page_scroller = nullptr;
    GtkWidget *page_title = nullptr, *page_identity = nullptr, *remove_page_button = nullptr;
    GtkWidget *add_button = nullptr, *remove_button = nullptr, *run_button = nullptr, *recent_menu = nullptr;
    GtkTreeStore *store = nullptr;
    std::vector<Node> nodes;
    std::vector<Field> fields;
    std::vector<GtkWidget *> inputs;
    std::set<GtkWidget *> invalid;
    std::string selected_key = "sys0";
    bool building = false, dirty = false;
    guint rebuild_source = 0;
    std::exception_ptr failure;

    std::string config_path, machine_name = "Untitled machine";

    // Running the machine: the emulator is a child process writing to a log beside the configuration.
    std::string run_log_path;
    GPid child = 0;
    guint child_watch = 0;
    int last_status = -1;

    // Every GTK callback funnels through here so C++ exceptions never cross the C stack.
    template <typename Function> void guarded(Function&& function)
    {
        try { function(); }
        catch (const std::exception& e) { message(GTK_MESSAGE_ERROR, "Something went wrong.", e.what()); }
        catch (...) { message(GTK_MESSAGE_ERROR, "Something went wrong.", "Unknown error."); }
    }
    static Editor *self(gpointer data) { return static_cast<Editor *>(data); }

public:
    Editor() = default;
    ~Editor()
    {
        if (child_watch) g_source_remove(child_watch);
        if (child) g_spawn_close_pid(child);
        if (rebuild_source) g_source_remove(rebuild_source);
        editor_release();
        if (window) { GtkWidget *w = window; window = nullptr; gtk_widget_destroy(w); }
    }

    void ensure_window();
    void run() { gtk_main(); }

    // Load a file, or start a new machine when none is given or it cannot be read.
    void open_machine(const std::string& path)
    {
        if (path.empty()) { new_machine(); return; }
        try
        {
            editor_load(path.c_str());
        }
        catch (const std::exception& e)
        {
            remember_machine(path, true);
            editor_new();
            set_path("");
            structure_changed();
            message(GTK_MESSAGE_ERROR, "Could not open \xe2\x80\x9c" + basename_of(path) + "\xe2\x80\x9d.", e.what());
            return;
        }
        set_path(path);
        remember_machine(path);
        refresh_recent_menu();
        dirty = false;
        selected_key = "sys0";
        structure_changed();
    }
    void new_machine()
    {
        editor_new();
        set_text(sys0, "rom.srm", "rom/cl67srmrom.exe");
        set_text(sys0, "rom.flash", "rom/flash.rom");
        set_text(sys0, "rom.dpr", "rom/dpr.rom");
        set_text(sys0, "rom.toy", "rom/toy.rom");
        set_text(sys0, "memory.bits", "28");
        set_text(ensure_child(sys0, "cpu0", "ev68cb"), "speed", "500M");
        set_bool(ensure_child(sys0, "pci0.7", "ali"), "vga_console", false);
        set_bool(ensure_child(sys0, "pci0.15", "ali_ide"), "dma", true);
        ensure_child(sys0, "fdc0", "floppy");
        CConfigurator *console = ensure_child(sys0, "serial0", "serial");
        set_text(console, "port", "21264");
        set_bool(console, "wait_for_connection", true);
        set_bool(ensure_child(sys0, "serial1", "serial"), "null_attach", true);
        set_path("");
        dirty = false;
        selected_key = "sys0";
        structure_changed();
    }

private:
    void touch() { dirty = true; set_title(); }
    void set_path(const std::string& path)
    {
        if (path.empty()) { config_path.clear(); machine_name = "Untitled machine"; }
        else
        {
            gchar *absolute = g_canonicalize_filename(path.c_str(), nullptr);
            config_path = absolute ? absolute : path;
            g_free(absolute);
            machine_name = basename_of(config_path);
        }
        set_title();
    }
    std::string config_directory() const
    {
        if (config_path.empty()) { gchar *cwd = g_get_current_dir(); std::string r = cwd; g_free(cwd); return r; }
        gchar *directory = g_path_get_dirname(config_path.c_str());
        std::string result = directory; g_free(directory); return result;
    }
    // Paths inside the configuration's directory are stored relative to it, so the machine can move.
    std::string relative_to_config(const std::string& path) const
    {
        const std::string directory = config_directory() + "/";
        return starts_with(path, directory.c_str()) ? path.substr(directory.size()) : path;
    }

    // ---- the device tree ----------------------------------------------------
    int add_node(Node node) { nodes.push_back(std::move(node)); return (int)nodes.size() - 1; }

    void collect_nodes()
    {
        nodes.clear();
        int cpus = 0;
        for (int i = 0; i < MAX_CPUS; ++i) if (child_of(sys0, "cpu" + std::to_string(i))) ++cpus;
        const int memory_bits = (int)sys0->get_num_value("memory.bits", false, 27);
        Node system; system.kind = Node::Kind::System; system.cfg = sys0; system.name = system.key = "sys0";
        system.title = "System"; system.icon = "computer-symbolic"; system.identity = "sys0 = tsunami";
        system.subtitle = memory_label(std::max(LOW_MEM_BITS, std::min(HI_MEM_BITS, memory_bits))) + " RAM, " +
                          std::to_string(cpus) + (cpus == 1 ? " processor" : " processors");
        const int system_index = add_node(system);
        for (int i = 0; i < MAX_CPUS; ++i)
        {
            CConfigurator *cpu = child_of(sys0, "cpu" + std::to_string(i));
            if (!cpu) continue;
            Node n; n.kind = Node::Kind::Cpu; n.cfg = cpu; n.name = n.key = name_of(cpu); n.parent = system_index;
            n.title = "CPU " + std::to_string(i); n.icon = "emblem-system-symbolic"; n.identity = n.name + " = ev68cb";
            std::string speed = text_of(cpu, "speed", "500M");
            while (!speed.empty() && !isdigit((unsigned char)speed.back())) speed.pop_back();
            n.subtitle = "Alpha EV68, " + speed + " MHz reported"; n.removable = i > 0;
            add_node(n);
        }

        CConfigurator *gui = child_of(theConfig, "gui");
        Node display; display.kind = Node::Kind::Display; display.cfg = gui; display.name = display.key = "gui";
        display.title = "Display"; display.icon = "video-display-symbolic";
        display.identity = gui ? "gui = " + value_of(gui) : "no gui section";
        display.subtitle = !gui ? "No window: serial console only" : lower(value_of(gui)) == "sdl" ? "SDL window" : value_of(gui) + " window";
        add_node(display);

        CConfigurator *ali = child_of(sys0, "pci0.7");
        Node chipset; chipset.kind = Node::Kind::Chipset; chipset.cfg = ali; chipset.name = chipset.key = "pci0.7";
        chipset.title = "Chipset"; chipset.icon = "preferences-system-symbolic"; chipset.identity = "pci0.7 = ali";
        chipset.subtitle = std::string("ALi M1543C, console on ") + (bool_of(ali, "vga_console") ? "graphics" : "serial port");
        if (!text_of(ali, "lpt.outfile").empty()) chipset.subtitle += ", printer to " + text_of(ali, "lpt.outfile");
        add_node(chipset);

        Node serials; serials.kind = Node::Kind::SerialGroup; serials.key = "serial"; serials.title = "Serial ports";
        serials.icon = "utilities-terminal-symbolic"; serials.subtitle = "Two 16550 UARTs, reached over telnet";
        const int serial_index = add_node(serials);
        for (int i = 0; i < 2; ++i)
        {
            Node n; n.kind = Node::Kind::Serial; n.name = n.key = "serial" + std::to_string(i); n.cfg = child_of(sys0, n.name);
            n.parent = serial_index; n.title = "COM" + std::to_string(i + 1); n.icon = "utilities-terminal-symbolic";
            n.identity = n.name + " = serial" + (n.cfg ? "" : " (defaults)");
            if (bool_of(n.cfg, "disabled")) n.subtitle = "Disabled: the guest sees no UART";
            else if (!n.cfg || bool_of(n.cfg, "null_attach")) n.subtitle = "Present but not connected";
            else
            {
                n.subtitle = "Telnet port " + text_of(n.cfg, "port", std::to_string(8000 + i).c_str());
                if (!bool_of(n.cfg, "wait_for_connection", true)) n.subtitle += ", starts without a client";
            }
            if (i == 0) n.subtitle += " (SRM console)";
            add_node(n);
        }

        auto add_disks = [&](int parent_index, CConfigurator *controller, const std::string& controller_name, const std::string& type)
        {
            for (const Slot& slot : slots_of(controller, controller_name, type))
            {
                CConfigurator *disk = child_of(controller, slot.name);
                if (!disk) continue;
                Node n; n.kind = Node::Kind::Disk; n.cfg = disk; n.name = slot.name; n.key = controller_name + "/" + slot.name;
                n.parent = parent_index; n.title = slot.label + "  (" + slot.srm + ")";
                n.icon = bool_of(disk, "cdrom") ? "media-optical-symbolic" : controller_name == "fdc0" ? "media-floppy-symbolic" : "drive-harddisk-symbolic";
                n.identity = controller_name + " / " + slot.name + " = " + value_of(disk);
                n.subtitle = describe_disk(disk, config_directory()); n.removable = true;
                add_node(n);
            }
        };

        CConfigurator *fdc = child_of(sys0, "fdc0");
        Node floppy; floppy.kind = Node::Kind::Floppy; floppy.cfg = fdc; floppy.name = floppy.key = "fdc0";
        floppy.title = "Floppy drives"; floppy.icon = "media-floppy-symbolic"; floppy.identity = "fdc0 = floppy";
        const int drives = fdc ? fdc->editor_child_count() : 0;
        floppy.subtitle = drives ? std::to_string(drives) + (drives == 1 ? " drive" : " drives") : "No drives";
        add_disks(add_node(floppy), fdc, "fdc0", "floppy");

        CConfigurator *ide = child_of(sys0, "pci0.15");
        Node idec; idec.kind = Node::Kind::Ide; idec.cfg = ide; idec.name = idec.key = "pci0.15";
        idec.title = "IDE controller"; idec.icon = "drive-multidisk-symbolic"; idec.identity = "pci0.15 = ali_ide";
        const int ide_disks = ide ? ide->editor_child_count() : 0;
        idec.subtitle = std::string("Two channels, DMA ") + (bool_of(ide, "dma", true) ? "enabled" : "disabled") +
                        ", " + std::to_string(ide_disks) + (ide_disks == 1 ? " drive" : " drives");
        add_disks(add_node(idec), ide, "pci0.15", "ali_ide");

        // PCI cards in slot order.
        std::vector<CConfigurator *> cards;
        for (auto card : children_of(sys0)) if (is_card_slot(name_of(card))) cards.push_back(card);
        std::sort(cards.begin(), cards.end(), [](CConfigurator *a, CConfigurator *b)
        {
            int ab = 0, as = 0, bb = 0, bs = 0;
            sscanf(a->get_myName(), "pci%d.%d", &ab, &as); sscanf(b->get_myName(), "pci%d.%d", &bb, &bs);
            return ab != bb ? ab < bb : as < bs;
        });
        for (auto card : cards)
        {
            const std::string type = value_of(card), slot = pci_slot_of(name_of(card));
            Node n; n.cfg = card; n.name = n.key = name_of(card); n.removable = true;
            n.identity = n.name + " = " + type;
            if (type == "sym53c810" || type == "lsi53c1020")
            {
                n.kind = Node::Kind::Scsi; n.icon = "drive-multidisk-symbolic";
                n.title = type == "sym53c810" ? "SCSI controller (Symbios 53C810)" : "SCSI controller (LSI 53C1020)";
                const int count = card->editor_child_count();
                n.subtitle = "PCI " + slot + ", " + std::to_string(count) + (count == 1 ? " disk" : " disks");
                if (type == "lsi53c1020") n.subtitle += ", not bootable from SRM";
                add_disks(add_node(n), card, n.name, type);
                continue;
            }
            if (type == "s3" || type == "gloria" || type == "radeon" || type == "powerstorm" || type == "cirrus")
            {
                n.kind = Node::Kind::Vga; n.icon = "video-display-symbolic";
                n.title = type == "s3" ? "S3 Trio64 graphics" : type == "gloria" ? "ELSA GLoria Synergy graphics" :
                          type == "radeon" ? "ATI Radeon 7500 graphics" : type == "powerstorm" ? "PowerStorm graphics" : "Cirrus graphics";
                n.subtitle = "PCI " + slot + ", BIOS " + text_of(card, "rom", "(none)");
            }
            else if (type == "dec21143")
            {
                n.kind = Node::Kind::Nic; n.icon = "network-wired-symbolic"; n.title = "DEC 21143 Ethernet";
                const std::string net = text_of(card, "type", "pcap"), adapter = text_of(card, "adapter");
                n.subtitle = "PCI " + slot + ", " + (net == "slirp" ? "user-mode NAT (slirp)" : net + (adapter.empty() || adapter == "list" ? ", adapter chosen at start" : " on " + adapter));
            }
            else if (type == "es1370" || type == "es1371")
            {
                n.kind = Node::Kind::Audio; n.icon = "audio-card-symbolic";
                n.title = type == "es1370" ? "Ensoniq ES1370 audio" : "Ensoniq ES1371 audio";
                n.subtitle = "PCI " + slot;
            }
            else
            {
                n.kind = Node::Kind::Other; n.icon = "application-x-addon-symbolic";
                n.title = type + " card"; n.subtitle = "PCI " + slot;
            }
            add_node(n);
        }
        // Anything else under sys0 that the editor does not model, so it is at least visible.
        for (auto other : children_of(sys0))
        {
            const std::string name = name_of(other);
            if (is_card_slot(name) || starts_with(name, "cpu") || starts_with(name, "serial") || name == "fdc0" ||
                name == "pci0.7" || name == "pci0.15" || name == "pci0.17" || name == "pci0.19") continue;
            Node n; n.kind = Node::Kind::Other; n.cfg = other; n.name = n.key = name; n.removable = true;
            n.title = name + " (" + value_of(other) + ")"; n.icon = "application-x-addon-symbolic";
            n.identity = name + " = " + value_of(other); n.subtitle = "Not understood by this editor; settings are shown as text";
            add_node(n);
        }
    }

    static std::string row_markup(const Node& n)
    {
        return "<b>" + escape(n.title) + "</b>\n<small><span alpha=\"65%\">" + escape(n.subtitle) + "</span></small>";
    }

    void fill_tree()
    {
        building = true;
        gtk_tree_store_clear(store);
        std::vector<GtkTreeIter> iters(nodes.size());
        GtkTreeIter *selected = nullptr;
        for (size_t i = 0; i < nodes.size(); ++i)
        {
            GtkTreeIter *parent = nodes[i].parent >= 0 ? &iters[nodes[i].parent] : nullptr;
            gtk_tree_store_append(store, &iters[i], parent);
            gtk_tree_store_set(store, &iters[i], 0, nodes[i].icon.c_str(), 1, row_markup(nodes[i]).c_str(), 2, (int)i, -1);
            if (nodes[i].key == selected_key) selected = &iters[i];
        }
        gtk_tree_view_expand_all(GTK_TREE_VIEW(tree));
        if (!selected && !iters.empty()) { selected = &iters[0]; selected_key = nodes[0].key; }
        if (selected)
        {
            GtkTreePath *path = gtk_tree_model_get_path(GTK_TREE_MODEL(store), selected);
            gtk_tree_view_set_cursor(GTK_TREE_VIEW(tree), path, nullptr, FALSE);
            gtk_tree_path_free(path);
        }
        building = false;
    }
    // Subtitles after a value change, without rebuilding rows (keeps focus in the page).
    void refresh_subtitles()
    {
        std::vector<Node> fresh_nodes;
        std::swap(fresh_nodes, nodes);
        collect_nodes();
        if (nodes.size() != fresh_nodes.size()) { std::swap(fresh_nodes, nodes); structure_changed(); return; }
        for (size_t i = 0; i < nodes.size(); ++i)
            if (nodes[i].key != fresh_nodes[i].key) { std::swap(fresh_nodes, nodes); structure_changed(); return; }
        gtk_tree_model_foreach(GTK_TREE_MODEL(store), [](GtkTreeModel *model, GtkTreePath *, GtkTreeIter *iter, gpointer data) -> gboolean
        {
            Editor *editor = self(data);
            int index = 0;
            gtk_tree_model_get(model, iter, 2, &index, -1);
            if (index >= 0 && index < (int)editor->nodes.size())
            {
                const Node& n = editor->nodes[index];
                gtk_tree_store_set(GTK_TREE_STORE(model), iter, 0, n.icon.c_str(), 1, row_markup(n).c_str(), -1);
            }
            return FALSE;
        }, this);
        update_page_header();
    }
    // Devices were added, removed or renamed: rebuild the tree and the page.
    void structure_changed()
    {
        if (!window) return;
        collect_nodes();
        fill_tree();
        build_page();
    }
    // The same, deferred: a widget that triggers a rebuild must not be destroyed inside its own signal.
    void structure_changed_later()
    {
        if (rebuild_source) return;
        rebuild_source = g_idle_add([](gpointer data) -> gboolean
        {
            Editor *editor = self(data);
            editor->rebuild_source = 0;
            editor->guarded([&] { editor->structure_changed(); });
            return G_SOURCE_REMOVE;
        }, this);
    }
    const Node *selected_node() const
    {
        for (const auto& n : nodes) if (n.key == selected_key) return &n;
        return nullptr;
    }
    // After adding or removing: the new selection is where keyboard work continues.
    void select(const std::string& key)
    {
        selected_key = key;
        structure_changed();
        if (tree) gtk_widget_grab_focus(tree);
    }

    // ---- settings of each device ---------------------------------------------
    std::vector<Field> fields_for(const Node& n)
    {
        switch (n.kind)
        {
        case Node::Kind::System: return system_fields();
        case Node::Kind::Cpu: return cpu_fields(n.cfg);
        case Node::Kind::Display: return display_fields();
        case Node::Kind::Chipset: return chipset_fields();
        case Node::Kind::SerialGroup: return {};
        case Node::Kind::Serial: return serial_fields(n.name);
        case Node::Kind::Floppy: return {};
        case Node::Kind::Ide: return ide_fields();
        case Node::Kind::Disk: return disk_fields(n.cfg);
        case Node::Kind::Scsi: return scsi_fields(n.cfg);
        case Node::Kind::Vga: return vga_fields(n.cfg);
        case Node::Kind::Nic: return nic_fields(n.cfg);
        case Node::Kind::Audio: return audio_fields(n.cfg);
        case Node::Kind::Other: return other_fields(n.cfg);
        }
        return {};
    }
    // Empty pages still say what the device is for and how to add to it.
    std::string page_note(const Node& n) const
    {
        switch (n.kind)
        {
        case Node::Kind::SerialGroup: return "The SRM console is on COM1. Select a port to change how it is reached.";
        case Node::Kind::Floppy: return "Add a drive with the Add button; images are raw 1.44 MB floppy files.";
        default: return "";
        }
    }

    std::vector<Field> system_fields()
    {
        Resolver r = [] { return sys0; };
        std::vector<Field> f;
        f.push_back(heading("Firmware"));
        f.push_back(note(FIRMWARE_SOURCES));
        f.push_back(action("Import firmware\xe2\x80\xa6", firmware_status(), [this] { import_firmware(); }));
        f.push_back(file("SRM console image", text_of(sys0, "rom.srm", "cl67srmrom.exe"),
            "The SRM firmware file (cl67srmrom.exe). Not needed when the flash image below already holds firmware.", to_key(r, "rom.srm")));
        f.push_back(file("Flash ROM image", text_of(sys0, "rom.flash", "flash.rom"),
            "Created at first start; keeps the firmware and the SRM environment between runs.", to_key(r, "rom.flash")));
        f.push_back(file("DPR EEPROM image", text_of(sys0, "rom.dpr", "dpr.rom"),
            "Dual-port RAM contents, created at first start.", to_key(r, "rom.dpr")));
        f.push_back(file("Clock NVRAM image", text_of(sys0, "rom.toy", "toy.rom"),
            "CMOS/TOY clock contents, so SRM settings such as heap_expand survive restarts.", to_key(r, "rom.toy")));
        f.push_back(heading("Memory and clock"));
        std::vector<std::string> sizes;
        for (int bits = LOW_MEM_BITS; bits <= HI_MEM_BITS; ++bits) sizes.push_back(memory_label(bits));
        const int bits = std::max(LOW_MEM_BITS, std::min(HI_MEM_BITS, (int)sys0->get_num_value("memory.bits", false, 27)));
        handles("memory.bits");
        f.push_back(choice("Memory", memory_label(bits), sizes, "RAM of the emulated machine. The host needs this much free memory.",
            [](const std::string& v)
            {
                for (int b = LOW_MEM_BITS; b <= HI_MEM_BITS; ++b)
                    if (memory_label(b) == v) { set_text(sys0, "memory.bits", std::to_string(b)); return; }
            }));
        f.push_back(pattern_text("Fixed start time", text_of(sys0, "time"),
            "YYYY-MM-DD or YYYY-MM-DD HH:MM:SS to start the guest clock at a fixed moment. Empty: the host clock.",
            "^ *([0-9]{4}-[0-9]{2}-[0-9]{2})?( [0-9]{2}:[0-9]{2}:[0-9]{2})? *$", to_optional_key(r, "time")));
        f.push_back(boolean("Windows-compatible year", bool_of(sys0, "arc_year_compat"),
            "Report the year the way Windows NT expects.", to_bool_key(r, "arc_year_compat")));
        f.push_back(boolean("Power off when the guest halts", bool_of(sys0, "exit_on_pal_halt"),
            "Exit the emulator on a PAL halt instead of returning to the SRM console.", to_bool_key(r, "exit_on_pal_halt")));
        f.push_back(heading("Processors"));
        int cpus = 0;
        for (int i = 0; i < MAX_CPUS; ++i) if (child_of(sys0, "cpu" + std::to_string(i))) ++cpus;
        f.push_back(note(std::to_string(cpus) + (cpus == 1 ? " processor" : " processors") + " (EV68, listed under System in the tree; the ES40 takes up to " +
                         std::to_string(MAX_CPUS) + "). Select one to set its reported clock and idle behaviour."));
        if (cpus < MAX_CPUS) f.push_back(action("Add processor", "Also under Add\xe2\x80\xa6 and in the tree's context menu.", [this] { add_cpu(); }));
        f.push_back(heading("Graphics"));
        f.push_back(boolean("Multiple graphics cards (experimental)", bool_of(sys0, "graphics.multihead"),
            "Lets SDL show more than one graphics card, each in its own PCI slot. X11 supports one output only.", to_bool_key(r, "graphics.multihead")));
        return f;
    }
    std::vector<Field> cpu_fields(CConfigurator *cpu)
    {
        Resolver r = [cpu] { return cpu; };
        std::vector<Field> f;
        std::string speed = text_of(cpu, "speed", "500M");
        while (!speed.empty() && !isdigit((unsigned char)speed.back())) speed.pop_back();
        handles("speed");
        f.push_back(integer("Reported clock (MHz)", speed, 10, 1250,
            "The speed the guest is told about. It does not change how fast the emulation runs.",
            [cpu](const std::string& v) { set_text(cpu, "speed", v + "M"); }));
        f.push_back(integer("Instructions per timer tick", text_of(cpu, "timer.max_instr_per_tick", "1250000"), 1, INT_MAX,
            "Upper bound on guest instructions between 1024 Hz timer ticks; the CPU sleeps when it gets there early, "
            "so the guest clock stays accurate.", to_key(r, "timer.max_instr_per_tick")));
#ifndef ES40_JIT
        f.push_back(boolean("Run the real OpenVMS PALcode", bool_of(cpu, "palcode.vms.nohle"),
            "Disable the high-level emulation of the OpenVMS PALcode.", to_bool_key(r, "palcode.vms.nohle")));
#endif
        f.push_back(heading("Idle behaviour (host CPU use)"));
        f.push_back(note("By default an idle guest still spins a host core at 100%. Napping sleeps the host thread while the guest waits for an interrupt."));
        f.push_back(boolean("Nap when the guest waits for an interrupt", bool_of(cpu, "idle_nap"),
            "For guests that issue CALL_PAL WTINT: VSI OpenVMS 8.4-2L2 and newer, NetBSD with a WTINT-aware kernel.", to_bool_key(r, "idle_nap")));
        f.push_back(choice("Detect idle loops of", text_of(cpu, "idle_detect", "off"), {"off", "vms", "tru64", "netbsd"},
            "Heuristic for guests that never issue WTINT (HP OpenVMS up to 8.4, Tru64 5.1B). Needs a JIT build. "
            "The name picks the heuristic; a wrong one can put a busy guest to sleep.", to_key(r, "idle_detect")));
        f.push_back(integer("Samples before sleeping", text_of(cpu, "idle_spin_count", "2"), 1, 1000,
            "How often the same idle-loop address must repeat before detection sleeps. Raise it to engage later.", to_key(r, "idle_spin_count")));
        f.push_back(integer("Nap poll interval (\xc2\xb5s)", text_of(cpu, "idle_nap_poll_us", "100"), 1, 100000,
            "Check interval of the portable sleep fallback; interrupts wake the CPU immediately regardless.", to_key(r, "idle_nap_poll_us")));
        return f;
    }
    std::vector<Field> display_fields()
    {
        std::vector<Field> f;
        std::vector<std::string> backends = {"none"};
#if defined(HAVE_SDL)
        backends.push_back("sdl");
#endif
#if defined(HAVE_X11)
        backends.push_back("X11");
#endif
        CConfigurator *gui = child_of(theConfig, "gui");
        std::string current = gui ? value_of(gui) : "none";
        for (const auto& b : backends) if (lower(b) == lower(current)) current = b;
        Field backend = choice("Window", current, backends,
            backends.size() == 1 ? "This build has no graphics backend; the console is the serial port."
                                 : "The window the emulated graphics card draws in. \"none\" leaves only the serial console.",
            [this](const std::string& v)
            {
                CConfigurator *existing = child_of(theConfig, "gui");
                if (v == "none") { if (existing) theConfig->remove_child("gui"); return; }
                if (existing) existing->set_myValue(v.c_str());
                else new CConfigurator(theConfig, (char *)"gui", (char *)v.c_str());
            });
        backend.rebuild = true;
        f.push_back(backend);
        if (!gui) return f;
        Resolver r = [] { return child_of(theConfig, "gui"); };
        f.push_back(heading("Keyboard"));
        f.push_back(boolean("Translate keys with a map file", bool_of(gui, "keyboard.use_mapping"),
            "Needed for non-US keyboard layouts.", to_bool_key(r, "keyboard.use_mapping")));
        f.push_back(file("Key map file", text_of(gui, "keyboard.map", "keys.map"), "Used when translation is on.", to_key(r, "keyboard.map")));
        f.push_back(pattern_text("Media menu hotkey", text_of(gui, "hotkey.media", "Ctrl+F11"), std::string("Opens the removable media chooser. ") + HOTKEY_HELP, HOTKEY_PATTERN, to_key(r, "hotkey.media")));
        f.push_back(pattern_text("Ctrl+Alt+Delete hotkey", text_of(gui, "hotkey.ctrl_alt_delete", "Ctrl+Alt+End"), std::string("Sends Ctrl+Alt+Delete to the guest. ") + HOTKEY_HELP, HOTKEY_PATTERN, to_key(r, "hotkey.ctrl_alt_delete")));
        f.push_back(heading("Mouse"));
        f.push_back(number("Speed factor", text_of(gui, "mouse.speed", "1.0"), 0.1, 10.0, 1, "Multiplier for host mouse motion.", to_key(r, "mouse.speed")));
        f.push_back(boolean("Invert horizontal motion", bool_of(gui, "mouse.invert_x"), "", to_bool_key(r, "mouse.invert_x")));
        f.push_back(boolean("Invert vertical motion", bool_of(gui, "mouse.invert_y"), "", to_bool_key(r, "mouse.invert_y")));
        f.push_back(boolean("Absolute pointer input", bool_of(gui, "mouse.absolute"),
            "Only for input sources that give absolute coordinates (tablets, some remote desktops). Off for a normal mouse.", to_bool_key(r, "mouse.absolute")));
        f.push_back(pattern_text("Capture hotkey", text_of(gui, "hotkey.mouse_capture", "Ctrl+F10"), std::string("Grabs and releases the mouse. ") + HOTKEY_HELP, HOTKEY_PATTERN, to_key(r, "hotkey.mouse_capture")));
        f.push_back(heading("Video"));
        f.push_back(boolean("Smooth scaling", bool_of(gui, "video.linear", true), "Linear filtering when the guest display is scaled; off gives sharp pixels.", to_bool_key(r, "video.linear")));
        f.push_back(integer("Scale factor", text_of(gui, "video.scale_ratio", "0"), 0, 10, "Window size as a multiple of the guest display; 0 lets the backend choose.", to_key(r, "video.scale_ratio")));
        f.push_back(boolean("Allow scaling while running", bool_of(gui, "video.scale_change_enable"), "Enables the hotkeys below.", to_bool_key(r, "video.scale_change_enable")));
        f.push_back(pattern_text("Scale up hotkey", text_of(gui, "hotkey.scale_up", "Ctrl+PageUp"), HOTKEY_HELP, HOTKEY_PATTERN, to_key(r, "hotkey.scale_up")));
        f.push_back(pattern_text("Scale down hotkey", text_of(gui, "hotkey.scale_down", "Ctrl+PageDown"), HOTKEY_HELP, HOTKEY_PATTERN, to_key(r, "hotkey.scale_down")));
        f.push_back(pattern_text("Reset window hotkey", text_of(gui, "hotkey.reset_window", "Ctrl+Alt+Home"), HOTKEY_HELP, HOTKEY_PATTERN, to_key(r, "hotkey.reset_window")));
        if (lower(value_of(gui)) == "x11")
            f.push_back(boolean("Private colormap", bool_of(gui, "private_colormap", true), "Use a private X11 colormap.", to_bool_key(r, "private_colormap")));
        return f;
    }
    std::vector<Field> chipset_fields()
    {
        Resolver r = [] { return ensure_child(sys0, "pci0.7", "ali"); };
        CConfigurator *ali = child_of(sys0, "pci0.7");
        std::vector<Field> f;
        handles("vga_console");
        f.push_back(choice("Console output", bool_of(ali, "vga_console") ? "graphics" : "serial", {"serial", "graphics"},
            "Where SRM and the guest console appear: on COM1 over telnet, or in the display window (needs a graphics card and a window).",
            [r](const std::string& v) { set_bool(r(), "vga_console", v == "graphics"); }));
        f.push_back(file("Printer output file", text_of(ali, "lpt.outfile"), "Parallel port output is appended to this file. Empty: discarded.", to_optional_key(r, "lpt.outfile")));
        f.push_back(boolean("Power management device (M7101)", child_of(sys0, "pci0.17") != nullptr, "ACPI/PMU device at PCI 0:17.",
            [](bool on) { if (on) ensure_child(sys0, "pci0.17", "ali_pmu"); else sys0->remove_child("pci0.17"); }));
        f.push_back(boolean("USB controller", child_of(sys0, "pci0.19") != nullptr, "OHCI controller at PCI 0:19.",
            [](bool on) { if (on) ensure_child(sys0, "pci0.19", "ali_usb"); else sys0->remove_child("pci0.19"); }));
        return f;
    }
    // "action" holds the program and its arguments; the page shows them apart.
    static void split_action(const std::string& action, std::string& program, std::string& arguments)
    {
        program.clear(); arguments.clear();
        if (action.empty()) return;
        size_t end;
        if (action[0] == '"')
        {
            end = action.find('"', 1);
            if (end == std::string::npos) { program = action.substr(1); return; }
            program = action.substr(1, end - 1); ++end;
        }
        else { end = action.find(' '); program = action.substr(0, end); }
        if (end != std::string::npos && end < action.size()) arguments = action.substr(end + (action[end] == ' ' ? 1 : 0));
    }
    std::vector<Field> serial_fields(const std::string& name)
    {
        const int index = name == "serial1" ? 1 : 0;
        Resolver r = [name] { return ensure_child(sys0, name, "serial"); };
        CConfigurator *port = child_of(sys0, name);
        // The emulator null-attaches a port the file does not mention, and listens on 8000+n when no port is given.
        const std::string default_port = std::to_string(8000 + index);
        const bool unconfigured = !port;
        std::vector<Field> f;
        Field disabled = boolean("Hidden from the guest", bool_of(port, "disabled"),
            "The guest sees no UART at this address and no telnet port is opened.", to_bool_key(r, "disabled"));
        disabled.rebuild = true; f.push_back(disabled);
        if (bool_of(port, "disabled")) return f;
        handles("null_attach"); handles("action");
        Field null_attach = boolean("Present but not connected", unconfigured || bool_of(port, "null_attach"),
            "An idle UART with nothing attached: no telnet port, output discarded. Firmware expects both ports to exist.",
            [r, index](bool on)
            {
                CConfigurator *node = r();
                set_bool(node, "null_attach", on);
                if (!on && text_of(node, "port").empty()) set_text(node, "port", std::to_string(21264 + index));
            });
        null_attach.rebuild = true; f.push_back(null_attach);
        if (unconfigured || bool_of(port, "null_attach")) return f;
        f.push_back(heading("Telnet listener"));
        f.push_back(integer("Port", text_of(port, "port", default_port.c_str()), 1, 65535, "TCP port a telnet client connects to.", to_key(r, "port")));
        Field listen = text("Listen addresses", text_of(port, "listen_address"),
            "Comma-separated IPv4/IPv6 addresses to listen on. Empty: all interfaces. 127.0.0.1, ::1 keeps it local.", to_optional_key(r, "listen_address"));
        listen.validate = [](const std::string& v)
        {
            if (v.find_first_not_of(" \t") == std::string::npos) return true;
            std::vector<std::string> addresses; std::string error;
            return serial_parse_listen_addresses(v, addresses, error);
        };
        f.push_back(listen);
        f.push_back(boolean("Wait for a client before starting", bool_of(port, "wait_for_connection", true),
            "Hold the machine at start-up until a telnet client connects, so no console output is missed. "
            "Turn off for a graphics console or an unattended machine.", to_bool_key(r, "wait_for_connection")));
        f.push_back(boolean("Raw byte stream", bool_of(port, "raw_mode"),
            "No telnet protocol handling and no line-speed throttling; for binary protocols such as kgdb or windbg.", to_bool_key(r, "raw_mode")));
        f.push_back(heading("Terminal program"));
        std::string program, arguments;
        split_action(text_of(port, "action"), program, arguments);
        auto store_action = [r](const std::string& program, const std::string& arguments)
        {
            if (program.empty()) { clear_key(r(), "action"); return; }
            set_text(r(), "action", "\"" + program + "\" " + arguments);
        };
        Field program_field = file("Start automatically", program,
            "A terminal program the emulator starts to connect to this port, e.g. putty or xterm. Empty: connect by hand.",
            [store_action, r](const std::string& v)
            {
                std::string program, arguments;
                split_action(text_of(r(), "action"), program, arguments);
                if (arguments.empty()) arguments = "telnet://localhost:" + text_of(r(), "port", "21264");
                store_action(v, arguments);
            });
        program_field.validate = [](const std::string& v) { return v.find('"') == std::string::npos; };
        f.push_back(program_field);
        f.push_back(text("Program arguments", arguments.empty() ? "telnet://localhost:" + text_of(port, "port", default_port.c_str()) : arguments,
            "Passed to the program, e.g. telnet://localhost:" + default_port + " for putty, or -e telnet localhost " + default_port + " for xterm.",
            [store_action, r](const std::string& v)
            {
                std::string program, arguments;
                split_action(text_of(r(), "action"), program, arguments);
                if (!program.empty()) store_action(program, v);
            }));
        return f;
    }
    std::vector<Field> ide_fields()
    {
        Resolver r = [] { return ensure_child(sys0, "pci0.15", "ali_ide"); };
        std::vector<Field> f;
        f.push_back(boolean("Bus-master DMA", bool_of(child_of(sys0, "pci0.15"), "dma", true),
            "Let the guest use DMA transfers on this controller. Off forces PIO.", to_bool_key(r, "dma")));
        return f;
    }
    std::vector<Field> disk_fields(CConfigurator *disk)
    {
        CConfigurator *controller = disk->get_myParent();
        const std::string controller_name = name_of(controller), controller_type = value_of(controller);
        const bool floppy = controller_name == "fdc0";
        const std::string type = value_of(disk);
        const bool ramdisk = type == "ramdisk";
        const bool cdrom = !floppy && !ramdisk && bool_of(disk, "cdrom");
        std::vector<Field> f;

        // Slot: only free ones plus the current.
        // read_only is implied by the Media choice for CD-ROMs and fixed for RAM disks, so it is never "advanced".
        for (const char *key : {"cdrom", "read_only", "file", "device", "size", "autocreate_size", "format", "autocreate_format", "overlay_file"}) handles(key);
        std::vector<std::string> labels; std::map<std::string, std::string> names;
        for (const Slot& slot : slots_of(controller, controller_name, controller_type))
            if (slot.name == name_of(disk) || !child_of(controller, slot.name))
            {
                const std::string label = slot.label + "  (" + slot.srm + ")";
                labels.push_back(label); names[label] = slot.name;
            }
        std::string current_label;
        for (const auto& entry : names) if (entry.second == name_of(disk)) current_label = entry.first;
        Field slot = choice(floppy ? "Drive" : "Position", current_label, labels, "Where the guest finds this drive; the SRM device name is in parentheses.",
            [this, disk, names](const std::string& v)
            {
                auto it = names.find(v);
                if (it == names.end()) return;
                disk->set_myName((char *)it->second.c_str());
                selected_key = name_of(disk->get_myParent()) + "/" + it->second;
            });
        slot.rebuild = true; f.push_back(slot);

        std::vector<std::string> types = {"file", "device"};
        if (!floppy) types.push_back("ramdisk");
        std::vector<std::string> type_labels = {"Image file", "Host device"};
        if (!floppy) type_labels.push_back("RAM disk");
        const std::string type_label = type == "device" ? "Host device" : type == "ramdisk" ? "RAM disk" : "Image file";
        Field kind = choice("Backed by", type_label, type_labels,
            "An image file on the host, a host block device (/dev/sr0, /dev/sdb), or memory that is lost at exit.",
            [this, disk, types, type_labels](const std::string& v)
            {
                for (size_t i = 0; i < type_labels.size(); ++i)
                    if (type_labels[i] == v) { change_disk_type(disk, types[i]); return; }
            });
        kind.rebuild = true; f.push_back(kind);

        if (!floppy && !ramdisk)
        {
            Field media = choice("Media", cdrom ? "CD-ROM" : "Hard disk", {"Hard disk", "CD-ROM"},
                "A CD-ROM is read-only; an ISO image or the host's optical drive goes here.",
                [disk](const std::string& v)
                {
                    const bool cd = v == "CD-ROM";
                    set_bool(disk, "cdrom", cd);
                    if (cd) { set_bool(disk, "read_only", true); clear_key(disk, "autocreate_size"); clear_key(disk, "format"); clear_key(disk, "autocreate_format"); }
                });
            media.rebuild = true; f.push_back(media);
        }
        Resolver r = [disk] { return disk; };
        if (type == "file")
        {
            f.push_back(file(cdrom ? "ISO image" : floppy ? "Floppy image" : "Image file", text_of(disk, "file"),
                cdrom ? "Empty means no media; it can be inserted from the emulator window later." :
                        "Relative paths are taken from the configuration's directory.", to_optional_key(r, "file")));
            if (!cdrom && !floppy)
            {
                f.push_back(pattern_text("Create if missing, with size", text_of(disk, "autocreate_size"),
                    "The emulator creates the image at first start, e.g. 2G or 500M. Empty: the file must exist.",
                    "^ *([0-9]+[KkMmGg])? *$", [disk](const std::string& v)
                    {
                        std::string size = v; size.erase(std::remove(size.begin(), size.end(), ' '), size.end());
                        if (!size.empty()) size.back() = (char)toupper((unsigned char)size.back());
                        set_optional(disk, "autocreate_size", size);
                    }));
                f.push_back(choice("Image format", text_of(disk, "format", "auto"), {"auto", "raw", "qcow2"},
                    "auto recognises an existing image by its header.", to_key(r, "format")));
                f.push_back(choice("Create as", text_of(disk, "autocreate_format", "raw"), {"raw", "qcow2"},
                    "Format of an image created by the emulator.", to_key(r, "autocreate_format")));
                f.push_back(file("Overlay file (optional)", text_of(disk, "overlay_file"),
                    "A QCOW2 file that receives all writes while the image above stays untouched. Created when missing.", to_optional_key(r, "overlay_file")));
            }
        }
        else if (type == "device")
            f.push_back(file("Host device", text_of(disk, "device"), cdrom ? "The host's optical drive, e.g. /dev/sr0." : "A host block device, e.g. /dev/sdb. The emulator needs permission to open it.", to_optional_key(r, "device")));
        else
            f.push_back(pattern_text("Size", text_of(disk, "size", "10M"), "Memory used by this disk, e.g. 10M. Contents vanish when the emulator exits.",
                "^ *[0-9]+[KkMmGg] *$", [disk](const std::string& v)
                {
                    std::string size = v; size.erase(std::remove(size.begin(), size.end(), ' '), size.end());
                    size.back() = (char)toupper((unsigned char)size.back());
                    set_text(disk, "size", size);
                }));
        if (!cdrom && !ramdisk)
            f.push_back(boolean("Read-only", bool_of(disk, "read_only"), "The guest cannot write to it.", to_bool_key(r, "read_only")));
        f.push_back(heading("Identification (optional)"));
        f.push_back(text("Model", text_of(disk, "model_number"), "Model string reported to the guest.", to_optional_key(r, "model_number")));
        f.push_back(text("Revision", text_of(disk, "rev_number"), "", to_optional_key(r, "rev_number")));
        f.push_back(text("Serial number", text_of(disk, "serial_number"), "", to_optional_key(r, "serial_number")));
        return f;
    }
    void change_disk_type(CConfigurator *disk, const std::string& type)
    {
        if (value_of(disk) == type) return;
        const std::string path = text_of(disk, value_of(disk) == "device" ? "device" : "file");
        disk->set_myValue(type.c_str());
        for (const char *key : {"file", "device", "size", "autocreate_size", "format", "autocreate_format", "overlay_file"}) clear_key(disk, key);
        if (type == "ramdisk") { set_text(disk, "size", "10M"); set_bool(disk, "cdrom", false); set_bool(disk, "read_only", false); }
        else if (!path.empty()) set_text(disk, type == "device" ? "device" : "file", path);
    }
    std::vector<Field> pci_slot_field(CConfigurator *card)
    {
        Field slot = choice("PCI slot", pci_slot_of(name_of(card)), free_pci_slots(name_of(card)),
            "bus.slot. Bus 0 is needed for a card SRM must use (a graphics console).",
            [this, card](const std::string& v)
            {
                card->set_myName((char *)("pci" + v).c_str());
                selected_key = "pci" + v;
            });
        slot.rebuild = true;
        return {slot};
    }
    std::vector<Field> scsi_fields(CConfigurator *card)
    {
        std::vector<Field> f = pci_slot_field(card);
        if (value_of(card) != "lsi53c1020") return f;
        Resolver r = [card] { return card; };
        f.push_back(heading("Card firmware"));
        f.push_back(file("Persistent flash image", text_of(card, "flash"),
            "A 512 KiB file that keeps the card's BIOS and firmware between runs; created when missing. Empty: changes are lost at exit.", to_optional_key(r, "flash")));
        f.push_back(file("Initial BIOS image", text_of(card, "rom"), "mptps.rom from an LSI firmware package, used once to seed the flash.", to_optional_key(r, "rom")));
        f.push_back(file("Initial IOC firmware", text_of(card, "firmware"), "it_1030.fw, used once to seed the flash.", to_optional_key(r, "firmware")));
        return f;
    }
    std::vector<Field> vga_fields(CConfigurator *card)
    {
        std::vector<Field> f = pci_slot_field(card);
        Resolver r = [card] { return card; };
        const std::string type = value_of(card);
        if (type == "s3")
        {
            f.push_back(note("The card needs a video BIOS. Free: GNU vgabios from the Debian/Ubuntu package vgabios "
                             "(sudo apt install vgabios), which needs \"Wake the card at reset\" on. "
                             "Or a dump of a real S3 Trio64 BIOS, such as 86c764x1.bin, with it off."));
            if (g_file_test(FREE_VGABIOS, G_FILE_TEST_IS_REGULAR))
                f.push_back(action("Use GNU vgabios", std::string("Installed at ") + FREE_VGABIOS + ".",
                    [this, card] { set_text(card, "rom", FREE_VGABIOS); set_bool(card, "wake_on_reset", true); touch(); structure_changed(); }));
            f.push_back(file("Video BIOS", text_of(card, "rom", "rom/VGABIOSFILE.bin"),
                "Relative paths are taken from the configuration's directory.", to_key(r, "rom")));
            f.push_back(boolean("Wake the card at reset", bool_of(card, "wake_on_reset"),
                "On for GNU vgabios, which never sends the S3 wake-up sequence. Off with a real S3 BIOS.", to_bool_key(r, "wake_on_reset")));
        }
        else if (type == "gloria")
        {
            f.push_back(file("Option ROM", text_of(card, "rom", "rom/SYN80700.PAN"), "PCI GLoria Synergy ROM; AGP images do not work.", to_key(r, "rom")));
            f.push_back(choice("ROM layout", text_of(card, "rom_layout", "exact"), {"exact", "mirror32", "pad_ff"},
                "exact: a full 64 KiB dump. mirror32: a 32 KiB image repeated. pad_ff: a 32 KiB image padded with 0xFF.", to_key(r, "rom_layout")));
            f.push_back(boolean("Subsystem IDs from the ROM", bool_of(card, "subsystem_from_rom"), "", to_bool_key(r, "subsystem_from_rom")));
            f.push_back(boolean("Allow DMA", bool_of(card, "allow_dma", true), "Let the card read commands and data from guest memory.", to_bool_key(r, "allow_dma")));
        }
        else if (type == "radeon")
        {
            f.push_back(file("Option ROM", text_of(card, "rom", "rom/r7500pci.rom"), "PCI Radeon 7500 (1002:5157) ROM; AGP images do not work.", to_key(r, "rom")));
            f.push_back(choice("ROM layout", text_of(card, "rom_layout", "pad_ff"), {"exact", "pad_ff"},
                "exact: a power-of-two dump. pad_ff: a smaller image padded with 0xFF.", to_key(r, "rom_layout")));
        }
        else if (type == "powerstorm")
        {
            f.push_back(file("Option ROM", text_of(card, "rom", "rom/BIOS.BIN"), "PowerStorm 300/350 ROM.", to_key(r, "rom")));
            f.push_back(choice("ROM layout", text_of(card, "rom_layout", "exact"), {"exact", "pad_ff"}, "", to_key(r, "rom_layout")));
            f.push_back(choice("Model", text_of(card, "model", "300"), {"300", "350"}, "300: 15 MiB framebuffer. 350: 30 MiB framebuffer.", to_key(r, "model")));
        }
        else
            f.push_back(file("Option ROM", text_of(card, "rom"), "", to_optional_key(r, "rom")));
        return f;
    }
    static std::vector<std::string> network_types()
    {
        std::vector<std::string> types;
#if defined(HAVE_SLIRP)
        types.push_back("slirp");
#endif
#if defined(HAVE_PCAP)
        types.push_back("pcap");
#endif
#if defined(HAVE_TAP_NET)
        types.push_back("tap");
#endif
#if defined(HAVE_VMNET)
        types.push_back("vmnet");
#endif
        return types;
    }
    std::vector<Field> nic_fields(CConfigurator *card)
    {
        std::vector<Field> f = pci_slot_field(card);
        Resolver r = [card] { return card; };
        std::vector<std::string> types = network_types();
        if (types.empty())
        {
            f.push_back(heading("This build has no network backend; the card cannot be connected."));
            return f;
        }
        const std::string type = text_of(card, "type", "pcap");
        if (std::find(types.begin(), types.end(), type) == types.end()) types.push_back(type);
        handles("adapter");
        Field connection = choice("Connection", type, types,
            "slirp: private NAT network, works without privileges, guest not reachable from the LAN. "
            "pcap: shares a host interface (needs cap_net_raw; not usable over Wi-Fi). "
            "tap: a host tap device you bridge or route.", to_key(r, "type"));
        connection.rebuild = true; f.push_back(connection);
        if (type == "pcap" || type == "tap")
        {
            Field adapter = choice(type == "tap" ? "TAP device" : "Host interface", text_of(card, "adapter"), host_interfaces(),
                type == "tap" ? "An existing tap device owned by you: sudo ip tuntap add dev tap0 mode tap user $USER"
                              : "The host interface to capture on. Leave empty only when starting from a terminal: the emulator then asks.",
                to_optional_key(r, "adapter"));
            adapter.editable_choice = true; adapter.placeholder = type == "tap" ? "tap0" : "eth0";
            f.push_back(adapter);
        }
        f.push_back(pattern_text("MAC address", text_of(card, "mac", "08-00-2B-E5-40-00"), "Must be unique on the network; 08-00-2B is DEC's prefix.",
            "^ *[0-9A-Fa-f]{2}(-[0-9A-Fa-f]{2}){5} *$", to_key(r, "mac")));
        if (type == "slirp")
        {
            f.push_back(heading("Private network"));
            f.push_back(text("Network", text_of(card, "network", "10.0.2.0/24"), "Subnet the guest lives on, as address/prefix.", to_optional_key(r, "network")));
            f.push_back(text("Gateway", text_of(card, "gateway", "10.0.2.2"), "The host, as seen by the guest.", to_optional_key(r, "gateway")));
            f.push_back(text("DHCP range start", text_of(card, "dhcp_start", "10.0.2.15"), "", to_optional_key(r, "dhcp_start")));
            f.push_back(text("DHCP range end", text_of(card, "dhcp_end", "10.0.2.30"), "", to_optional_key(r, "dhcp_end")));
            f.push_back(text("DNS servers", text_of(card, "dns"), "Comma-separated. Empty: the host's resolvers.", to_optional_key(r, "dns")));
            f.push_back(integer("DHCP lease (seconds)", text_of(card, "dhcp_lease", "3600"), 60, INT_MAX, "", to_optional_key(r, "dhcp_lease")));
        }
        f.push_back(heading("Advanced"));
        f.push_back(integer("Receive queue (frames)", text_of(card, "queue", "1024"), 1, INT_MAX, "Frames held between the host and the card; more are dropped.", to_key(r, "queue")));
        f.push_back(boolean("Compute Ethernet CRC", bool_of(card, "crc"), "Real frame checksums for the guest; costs host CPU and guests rarely check.", to_bool_key(r, "crc")));
        f.push_back(boolean("Trace packets on the console", bool_of(card, "trace_packets"), "Very noisy; for network debugging.", to_bool_key(r, "trace_packets")));
        return f;
    }
    std::vector<Field> audio_fields(CConfigurator *card)
    {
        std::vector<Field> f = pci_slot_field(card);
        if (value_of(card) == "es1371")
            f.push_back(integer("Playback latency (ms)", text_of(card, "latency_ms", "40"), 10, 500, "Audio queued on the host.", to_key([card] { return card; }, "latency_ms")));
        else
            f.push_back(heading("Works with the Windows NT 4.0 guest driver only."));
        return f;
    }
    std::vector<Field> other_fields(CConfigurator *node)
    {
        std::vector<Field> f;
        if (is_card_slot(name_of(node))) f = pci_slot_field(node);
        f.push_back(note("This editor has no page for this device; its settings are listed below as they appear in the file."));
        return f;
    }
    // Keys the device has that the page does not model, plus a way to add one.
    void append_advanced(const Node& n, const std::set<std::string>& modelled)
    {
        CConfigurator *node = n.cfg;
        if (!node) return;
        std::vector<std::string> extra;
        for (int i = 0; i < node->editor_value_count(); ++i)
            if (!modelled.count(node->editor_value_name(i))) extra.push_back(node->editor_value_name(i));
        fields.push_back(heading("Advanced"));
        fields.push_back(note(extra.empty() ? "No settings beyond the ones above. Any key the emulator documents in es40.cfg can be added here as it is written there."
                                            : "Settings in the file that this page has no field for, as written there. Empty a value to remove the key."));
        for (const auto& key : extra)
            fields.push_back(text(key, text_of(node, key.c_str()), "", [node, key](const std::string& v) { set_optional(node, key.c_str(), v); }));
        fields.push_back(action("Add setting\xe2\x80\xa6", "", [this, node] { add_setting(node); }));
    }
    void add_setting(CConfigurator *node)
    {
        GtkWidget *dialog = gtk_dialog_new_with_buttons("Add setting", GTK_WINDOW(window), GtkDialogFlags(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT),
            "_Cancel", GTK_RESPONSE_CANCEL, "_Add", GTK_RESPONSE_ACCEPT, nullptr);
        gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT);
        GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
        g_object_set(content, "margin", 12, "spacing", 8, nullptr);
        gtk_box_pack_start(GTK_BOX(content), dim_label("A key and value exactly as es40.cfg documents them for this device, e.g. trace and true."), FALSE, FALSE, 0);
        GtkWidget *grid = gtk_grid_new();
        gtk_grid_set_column_spacing(GTK_GRID(grid), 12); gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
        GtkWidget *key = gtk_entry_new(), *value = gtk_entry_new();
        gtk_entry_set_activates_default(GTK_ENTRY(key), TRUE); gtk_entry_set_activates_default(GTK_ENTRY(value), TRUE);
        gtk_widget_set_hexpand(key, TRUE); gtk_widget_set_hexpand(value, TRUE);
        gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Key"), 0, 0, 1, 1); gtk_grid_attach(GTK_GRID(grid), key, 1, 0, 1, 1);
        gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Value"), 0, 1, 1, 1); gtk_grid_attach(GTK_GRID(grid), value, 1, 1, 1, 1);
        gtk_box_pack_start(GTK_BOX(content), grid, FALSE, FALSE, 0);
        gtk_widget_show_all(content);
        gtk_widget_set_size_request(dialog, 460, -1);
        std::string name, text_value;
        if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT)
        {
            name = gtk_entry_get_text(GTK_ENTRY(key));
            text_value = gtk_entry_get_text(GTK_ENTRY(value));
        }
        gtk_widget_destroy(dialog);
        while (gtk_events_pending()) gtk_main_iteration();
        name.erase(std::remove_if(name.begin(), name.end(), [](unsigned char c) { return isspace(c); }), name.end());
        if (name.empty()) return;
        if (name.find_first_of("={};\"") != std::string::npos) { message(GTK_MESSAGE_ERROR, "That is not a valid key name.", "Keys look like idle_detect or hotkey.media."); return; }
        set_text(node, name.c_str(), text_value);
        touch();
        structure_changed();
    }

    // ---- adding and removing devices ------------------------------------------
    CConfigurator *controller_for_add() const
    {
        const Node *n = selected_node();
        if (!n) return nullptr;
        if (n->kind == Node::Kind::Disk) n = &nodes[n->parent];
        if (n->kind == Node::Kind::Floppy) return ensure_child(sys0, "fdc0", "floppy");
        if (n->kind == Node::Kind::Ide) return ensure_child(sys0, "pci0.15", "ali_ide");
        if (n->kind == Node::Kind::Scsi) return n->cfg;
        return nullptr;
    }
    bool can_add_disk() const
    {
        const Node *n = selected_node();
        if (!n) return false;
        if (n->kind == Node::Kind::Disk) n = &nodes[n->parent];
        if (n->kind != Node::Kind::Floppy && n->kind != Node::Kind::Ide && n->kind != Node::Kind::Scsi) return false;
        const std::string type = n->kind == Node::Kind::Floppy ? "floppy" : n->kind == Node::Kind::Ide ? "ali_ide" : value_of(n->cfg);
        for (const Slot& slot : slots_of(n->cfg, n->name, type)) if (!child_of(n->cfg, slot.name)) return true;
        return false;
    }
    void add_disk(bool cdrom)
    {
        CConfigurator *controller = controller_for_add();
        if (!controller) return;
        const std::string controller_name = name_of(controller), type = value_of(controller);
        for (const Slot& slot : slots_of(controller, controller_name, type))
        {
            if (child_of(controller, slot.name)) continue;
            if (controller_name == "pci0.15" && cdrom && slot.name == "disk0.0" && !child_of(controller, "disk1.0")) continue; // CDs go on the secondary channel by custom
            CConfigurator *disk = new CConfigurator(controller, (char *)slot.name.c_str(), (char *)"file");
            if (controller_name == "fdc0")
            {
                set_text(disk, "file", lower(slot.srm) + ".img");
                set_text(disk, "autocreate_size", "1440K");
                set_bool(disk, "read_only", false);
            }
            else if (cdrom)
            {
                set_bool(disk, "cdrom", true);
                set_bool(disk, "read_only", true);
            }
            else
            {
                set_text(disk, "file", lower(slot.srm) + ".img");
                set_text(disk, "autocreate_size", "2G");
                set_bool(disk, "cdrom", false);
                set_bool(disk, "read_only", false);
            }
            touch();
            select(controller_name + "/" + slot.name);
            return;
        }
        message(GTK_MESSAGE_INFO, "No free position on this controller.", "");
    }
    void add_cpu()
    {
        for (int i = 1; i < MAX_CPUS; ++i)
        {
            const std::string name = "cpu" + std::to_string(i);
            if (child_of(sys0, name)) continue;
            CConfigurator *cpu = new CConfigurator(sys0, (char *)name.c_str(), (char *)"ev68cb");
            set_text(cpu, "speed", text_of(child_of(sys0, "cpu0"), "speed", "500M"));
            touch();
            select(name);
            return;
        }
        message(GTK_MESSAGE_INFO, "The ES40 takes at most " + std::to_string(MAX_CPUS) + " processors.", "");
    }
    void add_card(const std::string& type)
    {
        const std::vector<std::string> slots = free_pci_slots();
        if (slots.empty()) { message(GTK_MESSAGE_INFO, "No free PCI slot.", "Remove a card first."); return; }
        // A graphics card that SRM should use needs bus 0; other cards take the first free slot too.
        const std::string name = "pci" + slots.front();
        CConfigurator *card = new CConfigurator(sys0, (char *)name.c_str(), (char *)type.c_str());
        if (type == "s3")
        {
            // The free BIOS when the host has it; otherwise the conventional name of a dumped one.
            const bool free_bios = g_file_test(FREE_VGABIOS, G_FILE_TEST_IS_REGULAR);
            set_text(card, "rom", free_bios ? FREE_VGABIOS : "rom/VGABIOSFILE.bin");
            set_bool(card, "wake_on_reset", free_bios);
        }
        else if (type == "gloria") { set_text(card, "rom", "rom/SYN80700.PAN"); }
        else if (type == "radeon") { set_text(card, "rom", "rom/r7500pci.rom"); }
        else if (type == "powerstorm") { set_text(card, "rom", "rom/BIOS.BIN"); }
        else if (type == "dec21143")
        {
            const std::vector<std::string> types = network_types();
            if (!types.empty()) set_text(card, "type", types.front());
            int count = 0;
            for (auto other : children_of(sys0)) if (value_of(other) == "dec21143") ++count;
            char mac[32]; snprintf(mac, sizeof mac, "08-00-2B-E5-40-%02X", (count - 1) & 0xff);
            set_text(card, "mac", mac);
        }
        touch();
        select(name);
    }
    void remove_selected()
    {
        const Node *n = selected_node();
        if (!n || !n->removable || !n->cfg) return;
        std::string next = n->parent >= 0 ? nodes[n->parent].key : "sys0";
        if (n->kind == Node::Kind::Cpu)
        {
            // Processors stay numbered without gaps.
            int index = atoi(n->name.c_str() + 3);
            sys0->remove_child(n->name.c_str());
            for (int i = index + 1; i < MAX_CPUS; ++i)
                if (CConfigurator *cpu = child_of(sys0, "cpu" + std::to_string(i)))
                    cpu->set_myName((char *)("cpu" + std::to_string(i - 1)).c_str());
        }
        else remove_node(n->cfg);
        touch();
        select(next);
    }

    // ---- the settings page --------------------------------------------------
    struct Binding { Editor *editor; int field; };
    static Binding *binding_of(GtkWidget *widget) { return static_cast<Binding *>(g_object_get_data(G_OBJECT(widget), "es40-binding")); }
    static void bind(GtkWidget *widget, Editor *editor, int field)
    {
        auto *b = new Binding{editor, field};
        g_object_set_data_full(G_OBJECT(widget), "es40-binding", b, [](gpointer p) { delete static_cast<Binding *>(p); });
    }
    static void on_input_changed(GtkWidget *widget, gpointer)
    {
        if (Binding *b = binding_of(widget)) b->editor->guarded([&] { b->editor->input_changed(b->field, widget); });
    }
    static void on_switch_changed(GObject *widget, GParamSpec *, gpointer) { on_input_changed(GTK_WIDGET(widget), nullptr); }
    // The mouse wheel scrolls the page; it must not change a dropdown or number the pointer happens to cross.
    static gboolean on_scroll_over_input(GtkWidget *widget, GdkEventScroll *event, gpointer data)
    {
        GtkWidget *inner = GTK_IS_COMBO_BOX(widget) && gtk_bin_get_child(GTK_BIN(widget)) ? gtk_bin_get_child(GTK_BIN(widget)) : widget;
        if (gtk_widget_has_focus(widget) || gtk_widget_has_focus(inner)) return FALSE; // A focused control may be wheeled on purpose.
        // Hand the wheel to the page instead, so it scrolls as if the pointer were beside the control.
        if (self(data)->page_scroller) gtk_widget_event(self(data)->page_scroller, (GdkEvent *)event);
        return TRUE;
    }
    static void on_action(GtkButton *button, gpointer)
    {
        if (Binding *b = binding_of(GTK_WIDGET(button)))
            b->editor->guarded([&] { if (b->field >= 0 && b->field < (int)b->editor->fields.size()) b->editor->fields[b->field].store(""); });
    }
    static void on_browse(GtkButton *button, gpointer)
    {
        if (Binding *b = binding_of(GTK_WIDGET(button))) b->editor->guarded([&] { b->editor->browse(b->field); });
    }
    static void on_remove_page(GtkButton *, gpointer data) { self(data)->guarded([&] { self(data)->confirm_remove(); }); }

    static GtkWidget *dim_label(const std::string& text, bool small = false)
    {
        GtkWidget *label = gtk_label_new(text.c_str());
        gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
        gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
        gtk_label_set_line_wrap_mode(GTK_LABEL(label), PANGO_WRAP_WORD_CHAR);
        gtk_style_context_add_class(gtk_widget_get_style_context(label), "dim-label");
        if (small) gtk_style_context_add_class(gtk_widget_get_style_context(label), "caption");
        return label;
    }
    static GtkWidget *scroller(GtkWidget *child, bool frame = true)
    {
        GtkWidget *scrolled = gtk_scrolled_window_new(nullptr, nullptr);
        gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
        if (frame) gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scrolled), GTK_SHADOW_IN);
        gtk_container_add(GTK_CONTAINER(scrolled), child);
        return scrolled;
    }

    void update_page_header()
    {
        const Node *n = selected_node();
        if (!n) return;
        gtk_label_set_text(GTK_LABEL(page_title), utf8(n->title).c_str());
        gtk_label_set_text(GTK_LABEL(page_identity), utf8(n->identity.empty() ? n->subtitle : n->subtitle + "  \xc2\xb7  " + n->identity).c_str());
        gtk_widget_set_visible(remove_page_button, n->removable);
        gtk_widget_set_sensitive(remove_button, n->removable);
        gtk_widget_set_sensitive(add_button, true);
    }

    void build_page()
    {
        if (!window) return;
        building = true;
        gtk_container_foreach(GTK_CONTAINER(page_box), [](GtkWidget *child, gpointer) { gtk_widget_destroy(child); }, nullptr);
        inputs.clear(); invalid.clear(); fields.clear();
        const Node *n = selected_node();
        if (!n) { building = false; return; }
        update_page_header();
        std::set<std::string> modelled;
        page_keys = &modelled;
        try { fields = fields_for(*n); } catch (...) { page_keys = nullptr; building = false; throw; }
        page_keys = nullptr;
        append_advanced(*n, modelled);
        const std::string note = page_note(*n);
        if (!note.empty())
        {
            GtkWidget *label = dim_label(note);
            g_object_set(label, "margin-top", 6, nullptr);
            gtk_box_pack_start(GTK_BOX(page_box), label, FALSE, FALSE, 0);
        }
        GtkWidget *grid = gtk_grid_new();
        gtk_grid_set_column_spacing(GTK_GRID(grid), 16);
        gtk_grid_set_row_spacing(GTK_GRID(grid), 4);
        gtk_box_pack_start(GTK_BOX(page_box), grid, FALSE, FALSE, 0);
        int row = 0;
        bool first = true;
        for (size_t i = 0; i < fields.size(); ++i)
        {
            Field& field = fields[i];
            if (field.kind == Field::Kind::Note)
            {
                GtkWidget *label = dim_label(utf8(field.label));
                g_object_set(label, "margin-top", 2, "margin-bottom", 4, nullptr);
                gtk_grid_attach(GTK_GRID(grid), label, 0, row++, 3, 1);
                inputs.push_back(nullptr);
                continue;
            }
            if (field.kind == Field::Kind::Action)
            {
                GtkWidget *button = gtk_button_new_with_mnemonic(utf8(field.label).c_str());
                gtk_widget_set_halign(button, GTK_ALIGN_START);
                bind(button, this, (int)i);
                g_signal_connect(button, "clicked", G_CALLBACK(on_action), nullptr);
                g_object_set(button, "margin-top", first ? 4 : 10, nullptr);
                gtk_grid_attach(GTK_GRID(grid), button, 1, row++, 2, 1);
                inputs.push_back(button);
                if (!field.help.empty()) gtk_grid_attach(GTK_GRID(grid), dim_label(utf8(field.help), true), 1, row++, 2, 1);
                first = false;
                continue;
            }
            if (field.kind == Field::Kind::Heading)
            {
                GtkWidget *label = gtk_label_new(nullptr);
                gtk_label_set_markup(GTK_LABEL(label), ("<b>" + escape(field.label) + "</b>").c_str());
                gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
                gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
                g_object_set(label, "margin-top", first ? 4 : 18, "margin-bottom", 2, nullptr);
                gtk_grid_attach(GTK_GRID(grid), label, 0, row++, 3, 1);
                inputs.push_back(nullptr);
                first = false;
                continue;
            }
            GtkWidget *caption = gtk_label_new(utf8(field.label).c_str());
            gtk_label_set_xalign(GTK_LABEL(caption), 1.0f);
            gtk_label_set_line_wrap(GTK_LABEL(caption), TRUE);
            gtk_label_set_max_width_chars(GTK_LABEL(caption), 26);
            gtk_label_set_justify(GTK_LABEL(caption), GTK_JUSTIFY_RIGHT);
            gtk_widget_set_valign(caption, GTK_ALIGN_CENTER);
            g_object_set(caption, "margin-top", first ? 4 : 10, nullptr);
            gtk_grid_attach(GTK_GRID(grid), caption, 0, row, 1, 1);

            GtkWidget *input = nullptr;
            const std::string value = utf8(field.value);
            switch (field.kind)
            {
            case Field::Kind::Choice:
                if (field.editable_choice)
                {
                    input = gtk_combo_box_text_new_with_entry();
                    for (const auto& c : field.choices) gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(input), utf8(c).c_str());
                    GtkWidget *entry = gtk_bin_get_child(GTK_BIN(input));
                    gtk_entry_set_text(GTK_ENTRY(entry), value.c_str());
                    if (!field.placeholder.empty()) gtk_entry_set_placeholder_text(GTK_ENTRY(entry), field.placeholder.c_str());
                }
                else
                {
                    input = gtk_combo_box_text_new();
                    int selected = -1;
                    for (size_t c = 0; c < field.choices.size(); ++c)
                    {
                        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(input), utf8(field.choices[c]).c_str());
                        if (selected < 0 && !g_ascii_strcasecmp(field.choices[c].c_str(), field.value.c_str())) selected = (int)c;
                    }
                    if (selected < 0)
                    {
                        // Keep an unexpected value visible instead of silently changing it.
                        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(input), value.c_str());
                        selected = (int)field.choices.size();
                    }
                    gtk_combo_box_set_active(GTK_COMBO_BOX(input), selected);
                }
                g_signal_connect(input, "changed", G_CALLBACK(on_input_changed), nullptr);
                break;
            case Field::Kind::Bool:
                input = gtk_switch_new();
                gtk_switch_set_active(GTK_SWITCH(input), field.value == "true");
                gtk_widget_set_halign(input, GTK_ALIGN_START);
                g_signal_connect(input, "notify::active", G_CALLBACK(on_switch_changed), nullptr);
                break;
            case Field::Kind::Integer:
            case Field::Kind::Number:
            {
                const double step = field.kind == Field::Kind::Number ? 0.1 : 1;
                input = gtk_spin_button_new_with_range(field.minimum, field.maximum, step);
                gtk_spin_button_set_digits(GTK_SPIN_BUTTON(input), field.digits);
                gtk_spin_button_set_value(GTK_SPIN_BUTTON(input), strtod(field.value.c_str(), nullptr));
                gtk_widget_set_halign(input, GTK_ALIGN_START);
                g_signal_connect(input, "value-changed", G_CALLBACK(on_input_changed), nullptr);
                break;
            }
            default:
                input = gtk_entry_new();
                gtk_entry_set_text(GTK_ENTRY(input), value.c_str());
                if (!field.placeholder.empty()) gtk_entry_set_placeholder_text(GTK_ENTRY(input), field.placeholder.c_str());
                g_signal_connect(input, "changed", G_CALLBACK(on_input_changed), nullptr);
                break;
            }
            bind(input, this, (int)i);
            if (field.kind == Field::Kind::Choice || field.kind == Field::Kind::Integer || field.kind == Field::Kind::Number)
                g_signal_connect(input, "scroll-event", G_CALLBACK(on_scroll_over_input), this);
            gtk_widget_set_hexpand(input, field.kind != Field::Kind::Bool && field.kind != Field::Kind::Integer && field.kind != Field::Kind::Number);
            gtk_widget_set_valign(input, GTK_ALIGN_CENTER);
            g_object_set(input, "margin-top", first ? 4 : 10, nullptr);
            gtk_grid_attach(GTK_GRID(grid), input, 1, row, field.kind == Field::Kind::File ? 1 : 2, 1);
            if (field.kind == Field::Kind::File)
            {
                GtkWidget *browse = gtk_button_new_with_mnemonic("_Browse\xe2\x80\xa6");
                bind(browse, this, (int)i);
                g_signal_connect(browse, "clicked", G_CALLBACK(on_browse), nullptr);
                gtk_widget_set_valign(browse, GTK_ALIGN_CENTER);
                g_object_set(browse, "margin-top", first ? 4 : 10, nullptr);
                gtk_grid_attach(GTK_GRID(grid), browse, 2, row, 1, 1);
            }
            inputs.push_back(input);
            ++row;
            if (!field.help.empty())
                gtk_grid_attach(GTK_GRID(grid), dim_label(utf8(field.help), true), 1, row++, 2, 1);
            first = false;
        }
        gtk_widget_show_all(page_box);
        building = false;
    }

    static std::string read_input(const Field& field, GtkWidget *input)
    {
        switch (field.kind)
        {
        case Field::Kind::Choice:
        {
            gchar *chosen = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(input));
            std::string result = chosen ? chosen : "";
            g_free(chosen);
            return result;
        }
        case Field::Kind::Bool: return gtk_switch_get_active(GTK_SWITCH(input)) ? "true" : "false";
        case Field::Kind::Integer: return std::to_string((long long)gtk_spin_button_get_value(GTK_SPIN_BUTTON(input)));
        case Field::Kind::Number:
        {
            char text[64]; snprintf(text, sizeof text, "%.*f", field.digits, gtk_spin_button_get_value(GTK_SPIN_BUTTON(input)));
            return text;
        }
        default: return gtk_entry_get_text(GTK_ENTRY(input));
        }
    }
    void mark(GtkWidget *input, bool bad, const char *why = nullptr)
    {
        GtkWidget *entry = GTK_IS_ENTRY(input) ? input : GTK_IS_COMBO_BOX(input) && gtk_combo_box_get_has_entry(GTK_COMBO_BOX(input)) ? gtk_bin_get_child(GTK_BIN(input)) : nullptr;
        if (bad) invalid.insert(input); else invalid.erase(input);
        if (!entry) return;
        GtkStyleContext *context = gtk_widget_get_style_context(entry);
        if (bad) gtk_style_context_add_class(context, "error"); else gtk_style_context_remove_class(context, "error");
        gtk_entry_set_icon_from_icon_name(GTK_ENTRY(entry), GTK_ENTRY_ICON_SECONDARY, bad ? "dialog-error-symbolic" : nullptr);
        if (bad) gtk_entry_set_icon_tooltip_text(GTK_ENTRY(entry), GTK_ENTRY_ICON_SECONDARY, why ? why : "This value is not valid and has not been applied.");
    }
    void input_changed(int index, GtkWidget *input)
    {
        if (building || index < 0 || index >= (int)fields.size()) return;
        Field& field = fields[index];
        const std::string value = read_input(field, input);
        if (field.validate && !field.validate(value)) { mark(input, true); return; }
        mark(input, false);
        if (value == field.value) return;
        field.value = value;
        field.store(value);
        touch();
        if (field.rebuild) structure_changed_later();
        else refresh_subtitles();
    }
    void browse(int index)
    {
        if (index < 0 || index >= (int)fields.size() || !inputs[index]) return;
        GtkWidget *dialog = gtk_file_chooser_dialog_new(("Choose " + lower(fields[index].label)).c_str(), GTK_WINDOW(window), GTK_FILE_CHOOSER_ACTION_OPEN,
            "_Cancel", GTK_RESPONSE_CANCEL, "_Choose", GTK_RESPONSE_ACCEPT, nullptr);
        const std::string current = gtk_entry_get_text(GTK_ENTRY(inputs[index]));
        std::string start = current;
        if (!start.empty() && !g_path_is_absolute(start.c_str())) start = config_directory() + "/" + start;
        if (!start.empty() && g_file_test(start.c_str(), G_FILE_TEST_EXISTS)) gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(dialog), start.c_str());
        else gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(dialog), config_directory().c_str());
        std::string chosen;
        if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT)
            if (gchar *filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog))) { chosen = filename; g_free(filename); }
        gtk_widget_destroy(dialog);
        while (gtk_events_pending()) gtk_main_iteration();
        if (chosen.empty()) return;
        gtk_entry_set_text(GTK_ENTRY(inputs[index]), utf8(relative_to_config(chosen)).c_str()); // "changed" applies it
    }

    // ---- firmware import --------------------------------------------------------
    std::string host_path(const std::string& path) const
    { return path.empty() || g_path_is_absolute(path.c_str()) ? path : config_directory() + "/" + path; }
    bool have_firmware() const
    {
        const std::string srm = host_path(text_of(sys0, "rom.srm", "cl67srmrom.exe"));
        const std::string flash = host_path(text_of(sys0, "rom.flash", "flash.rom"));
        return g_file_test(srm.c_str(), G_FILE_TEST_IS_REGULAR) || is_flash_image(flash);
    }
    std::string firmware_status() const
    {
        if (config_path.empty()) return "Save the machine first; the firmware is placed in a rom folder beside it.";
        const std::string flash = host_path(text_of(sys0, "rom.flash", "flash.rom"));
        if (is_flash_image(flash)) return "The flash image holds SRM: this machine boots.";
        if (g_file_test(host_path(text_of(sys0, "rom.srm", "cl67srmrom.exe")).c_str(), G_FILE_TEST_IS_REGULAR))
            return "The SRM image is in place; the flash is programmed from it at first start.";
        return "No firmware found yet: the machine cannot start.";
    }
    void import_firmware()
    {
        if (config_path.empty()) { message(GTK_MESSAGE_INFO, "Save the machine first.", "The firmware goes in a rom folder beside the configuration file."); return; }
        GtkWidget *dialog = gtk_file_chooser_dialog_new("Import firmware", GTK_WINDOW(window), GTK_FILE_CHOOSER_ACTION_OPEN,
            "_Cancel", GTK_RESPONSE_CANCEL, "_Import", GTK_RESPONSE_ACCEPT, nullptr);
        GtkFileFilter *filter = gtk_file_filter_new();
        gtk_file_filter_set_name(filter, "Firmware (cl67srmrom.exe, flash.rom, .zip, .iso)");
        for (const char *pattern : {"*.exe", "*.EXE", "*.rom", "*.ROM", "*.zip", "*.ZIP", "*.iso", "*.ISO", "*.7z"}) gtk_file_filter_add_pattern(filter, pattern);
        gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);
        GtkFileFilter *all = gtk_file_filter_new();
        gtk_file_filter_set_name(all, "All files");
        gtk_file_filter_add_pattern(all, "*");
        gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), all);
        gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(dialog), g_get_home_dir());
        std::string chosen;
        if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT)
            if (gchar *filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog))) { chosen = filename; g_free(filename); }
        gtk_widget_destroy(dialog);
        while (gtk_events_pending()) gtk_main_iteration();
        if (chosen.empty()) return;
        std::string report;
        if (import_firmware_file(chosen, report))
        {
            touch();
            structure_changed();
            message(GTK_MESSAGE_INFO, "Firmware imported.", report);
        }
        else if (!report.empty()) message(GTK_MESSAGE_ERROR, "Could not import \xe2\x80\x9c" + basename_of(chosen) + "\xe2\x80\x9d.", report);
    }
    // Copies the right file into <machine>/rom and points the configuration at it. Returns false with the reason, or "" when cancelled.
    bool import_firmware_file(const std::string& source, std::string& report)
    {
        if (is_archive(source))
        {
            std::vector<std::string> entries;
            if (!archive_entries(source, entries, report)) return false;
            std::string flash, srm;
            for (const auto& entry : entries)
            {
                const std::string name = lower(basename_of(entry));
                if (name == "flash.rom" || (name.size() > 4 && name.compare(name.size() - 4, 4, ".rom") == 0 && name.find("flash") != std::string::npos)) { if (flash.empty()) flash = entry; }
                else if (looks_like_srm_update(name)) { if (srm.empty()) srm = entry; }
            }
            const std::string wanted = !flash.empty() ? flash : srm;
            if (wanted.empty())
            {
                report = "Nothing in it is named like ES40 firmware (flash.rom or cl67srmrom.exe). It holds:\n";
                for (size_t i = 0; i < entries.size() && i < 25; ++i) report += "  " + entries[i] + "\n";
                if (entries.size() > 25) report += "  \xe2\x80\xa6 and " + std::to_string(entries.size() - 25) + " more";
                return false;
            }
            gchar *temporary = g_dir_make_tmp("es40-firmware-XXXXXX", nullptr);
            if (!temporary) { report = "Could not create a temporary directory."; return false; }
            const std::string directory = temporary; g_free(temporary);
            const std::string extracted = archive_extract(source, wanted, directory, report);
            bool ok = false;
            if (!extracted.empty())
            {
                ok = import_firmware_file(extracted, report);
                if (ok) report = "From " + basename_of(source) + ": " + wanted + "\n" + report;
                unlink(extracted.c_str());
            }
            rmdir(directory.c_str());
            return ok;
        }
        const std::string rom_directory = config_directory() + "/rom";
        if (is_flash_image(source))
        {
            const std::string target = rom_directory + "/flash.rom";
            if (g_file_test(target.c_str(), G_FILE_TEST_EXISTS) && !same_file(source, target))
            {
                GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(window), GtkDialogFlags(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT),
                    GTK_MESSAGE_QUESTION, GTK_BUTTONS_NONE, "Replace the machine's flash image?");
                gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog), "%s already exists and holds this machine's firmware and SRM environment variables. The old file is kept as flash.rom.bak.", utf8(target).c_str());
                gtk_dialog_add_buttons(GTK_DIALOG(dialog), "_Cancel", GTK_RESPONSE_CANCEL, "_Replace", GTK_RESPONSE_ACCEPT, nullptr);
                const int response = gtk_dialog_run(GTK_DIALOG(dialog));
                gtk_widget_destroy(dialog);
                while (gtk_events_pending()) gtk_main_iteration();
                if (response != GTK_RESPONSE_ACCEPT) { report.clear(); return false; }
                rename(target.c_str(), (target + ".bak").c_str());
            }
            if (!copy_file(source, target, report)) return false;
            set_text(sys0, "rom.flash", "rom/flash.rom");
            report = "Programmed flash image installed as rom/flash.rom. It already contains SRM, so no separate SRM image is needed.";
            return true;
        }
        if (!looks_like_srm_update(source))
        {
            GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(window), GtkDialogFlags(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT),
                GTK_MESSAGE_QUESTION, GTK_BUTTONS_NONE, "Use \xe2\x80\x9c%s\xe2\x80\x9d as the SRM update image?", utf8(basename_of(source)).c_str());
            gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog), "It is neither a programmed 2 MB flash image nor an archive, and is not named like CL67SRMROM.EXE. "
                "The emulator reads an SRM update image from the HP firmware CD; anything else will fail at start.");
            gtk_dialog_add_buttons(GTK_DIALOG(dialog), "_Cancel", GTK_RESPONSE_CANCEL, "_Use it", GTK_RESPONSE_ACCEPT, nullptr);
            const int response = gtk_dialog_run(GTK_DIALOG(dialog));
            gtk_widget_destroy(dialog);
            while (gtk_events_pending()) gtk_main_iteration();
            if (response != GTK_RESPONSE_ACCEPT) { report.clear(); return false; }
        }
        const std::string target = rom_directory + "/cl67srmrom.exe";
        if (!same_file(source, target) && !copy_file(source, target, report)) return false;
        set_text(sys0, "rom.srm", "rom/cl67srmrom.exe");
        if (text_of(sys0, "rom.flash").empty()) set_text(sys0, "rom.flash", "rom/flash.rom");
        report = "SRM update image installed as rom/cl67srmrom.exe. At the first start the emulator programs the flash image from it; later starts boot from the flash.";
        return true;
    }
    static bool same_file(const std::string& a, const std::string& b)
    {
        struct stat sa, sb;
        return !stat(a.c_str(), &sa) && !stat(b.c_str(), &sb) && sa.st_dev == sb.st_dev && sa.st_ino == sb.st_ino;
    }
    static bool copy_file(const std::string& source, const std::string& target, std::string& error)
    {
        gchar *directory = g_path_get_dirname(target.c_str());
        g_mkdir_with_parents(directory, 0755);
        g_free(directory);
        gchar *contents = nullptr; gsize length = 0; GError *failure = nullptr;
        if (!g_file_get_contents(source.c_str(), &contents, &length, &failure) ||
            !g_file_set_contents(target.c_str(), contents, (gssize)length, &failure))
        {
            error = failure ? failure->message : "copy failed";
            if (failure) g_error_free(failure);
            g_free(contents);
            return false;
        }
        g_free(contents);
        return true;
    }

    // ---- saving ---------------------------------------------------------------
    // Problems the emulator would stop on. Returns the text, or "" when fine.
    std::string check_machine() const
    {
        std::string problems;
        bool vga = false;
        for (auto card : children_of(sys0))
        {
            const std::string type = value_of(card);
            if (type == "s3" || type == "gloria" || type == "radeon" || type == "powerstorm" || type == "cirrus") vga = true;
        }
        if (bool_of(child_of(sys0, "pci0.7"), "vga_console") && !vga) problems += "\xe2\x80\xa2 The console goes to graphics, but there is no graphics card. Add one, or set the chipset's console output to serial.\n";
        if (vga && !child_of(theConfig, "gui")) problems += "\xe2\x80\xa2 There is a graphics card but no window to show it in. Choose a window under Display.\n";
        if (!have_firmware())
            problems += "\xe2\x80\xa2 No firmware: neither " + text_of(sys0, "rom.srm", "cl67srmrom.exe") + " nor a programmed " +
                        text_of(sys0, "rom.flash", "flash.rom") + " exists beside the configuration. Use Import firmware\xe2\x80\xa6 on the System page.\n";
        for (auto card : children_of(sys0))
        {
            const std::string type = value_of(card);
            if (type != "s3" && type != "gloria" && type != "radeon" && type != "powerstorm" && type != "cirrus") continue;
            const std::string rom = text_of(card, "rom");
            if (rom.empty() || !g_file_test(host_path(rom).c_str(), G_FILE_TEST_IS_REGULAR))
                problems += "\xe2\x80\xa2 The graphics card in PCI " + pci_slot_of(name_of(card)) + " has no BIOS file at " + (rom.empty() ? std::string("(none)") : rom) +
                            (type == "s3" ? ". Install the vgabios package and choose Use GNU vgabios on its page, or point it at a real S3 BIOS dump.\n" : ".\n");
        }
        for (auto card : children_of(sys0))
            if (value_of(card) == "dec21143" && text_of(card, "type", "pcap") != "slirp" && (text_of(card, "adapter").empty() || text_of(card, "adapter") == "list"))
                problems += "\xe2\x80\xa2 The DEC 21143 Ethernet card in PCI " + pci_slot_of(name_of(card)) + " names no host interface; the emulator asks for one on its console and stops when started from here.\n";
        if (!invalid.empty()) problems += "\xe2\x80\xa2 A setting on the current page is not valid (marked in red) and keeps its previous value.\n";
        return problems;
    }
    bool save()
    {
        if (config_path.empty()) return save_as();
        const std::string problems = check_machine();
        if (!problems.empty())
        {
            GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(window), GtkDialogFlags(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT),
                GTK_MESSAGE_WARNING, GTK_BUTTONS_NONE, "This machine will not start as it is.");
            gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog), "%s", utf8(problems).c_str());
            gtk_dialog_add_buttons(GTK_DIALOG(dialog), "_Cancel", GTK_RESPONSE_CANCEL, "Save _anyway", GTK_RESPONSE_ACCEPT, nullptr);
            gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_CANCEL);
            const int response = gtk_dialog_run(GTK_DIALOG(dialog));
            gtk_widget_destroy(dialog);
            while (gtk_events_pending()) gtk_main_iteration();
            if (response != GTK_RESPONSE_ACCEPT) return false;
        }
        try { editor_save(config_path.c_str()); }
        catch (const std::exception& e) { message(GTK_MESSAGE_ERROR, "Could not save \xe2\x80\x9c" + machine_name + "\xe2\x80\x9d.", e.what()); return false; }
        dirty = false;
        remember_machine(config_path);
        refresh_recent_menu();
        set_title();
        return true;
    }
    bool save_as()
    {
        const std::string path = choose_file(true, config_path);
        if (path.empty()) return false;
        set_path(path);
        return save();
    }
    // Unsaved work check before closing or switching machines. Returns true to go ahead.
    bool confirm_leave(const char *verb)
    {
        if (!dirty) return true;
        GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(window), GtkDialogFlags(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT),
            GTK_MESSAGE_QUESTION, GTK_BUTTONS_NONE, "Save changes to \xe2\x80\x9c%s\xe2\x80\x9d before %s?", utf8(machine_name).c_str(), verb);
        gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog), "Unsaved settings are lost if you discard them.");
        gtk_dialog_add_buttons(GTK_DIALOG(dialog), "_Discard", GTK_RESPONSE_REJECT, "_Cancel", GTK_RESPONSE_CANCEL, "_Save", GTK_RESPONSE_ACCEPT, nullptr);
        gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT);
        if (GtkWidget *discard = gtk_dialog_get_widget_for_response(GTK_DIALOG(dialog), GTK_RESPONSE_REJECT))
            gtk_style_context_add_class(gtk_widget_get_style_context(discard), "destructive-action");
        const int response = gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
        while (gtk_events_pending()) gtk_main_iteration();
        if (response == GTK_RESPONSE_REJECT) return true;
        if (response == GTK_RESPONSE_ACCEPT) return save();
        return false;
    }
    void request_new() { if (confirm_leave("starting a new machine")) new_machine(); }
    void request_open(const std::string& chosen = {})
    {
        std::string path = chosen;
        if (path.empty()) path = choose_file(false);
        if (path.empty() || !confirm_leave("opening another machine")) return;
        open_machine(path);
    }
    void request_close()
    {
        if (!confirm_leave("closing")) return;
        if (child)
        {
            GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(window), GtkDialogFlags(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT),
                GTK_MESSAGE_QUESTION, GTK_BUTTONS_NONE, "\xe2\x80\x9c%s\xe2\x80\x9d is still running.", utf8(machine_name).c_str());
            gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog), "The emulator keeps running after the editor closes. Its output goes to\n%s", utf8(run_log_path).c_str());
            gtk_dialog_add_buttons(GTK_DIALOG(dialog), "_Stop machine", GTK_RESPONSE_REJECT, "_Cancel", GTK_RESPONSE_CANCEL, "_Leave running", GTK_RESPONSE_ACCEPT, nullptr);
            gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT);
            const int response = gtk_dialog_run(GTK_DIALOG(dialog));
            gtk_widget_destroy(dialog);
            if (response == GTK_RESPONSE_REJECT) stop_machine();
            else if (response != GTK_RESPONSE_ACCEPT) return;
        }
        gtk_main_quit();
    }
    void confirm_remove()
    {
        const Node *n = selected_node();
        if (!n || !n->removable) return;
        GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(window), GtkDialogFlags(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT),
            GTK_MESSAGE_QUESTION, GTK_BUTTONS_NONE, "Remove %s from the machine?", utf8(n->title).c_str());
        gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog), "%s",
            n->kind == Node::Kind::Disk ? "The image file on the host is kept." : n->kind == Node::Kind::Scsi ? "Its disks are removed with it; their image files are kept." : "The change is written when you save.");
        gtk_dialog_add_buttons(GTK_DIALOG(dialog), "_Cancel", GTK_RESPONSE_CANCEL, "_Remove", GTK_RESPONSE_ACCEPT, nullptr);
        if (GtkWidget *remove = gtk_dialog_get_widget_for_response(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT))
            gtk_style_context_add_class(gtk_widget_get_style_context(remove), "destructive-action");
        gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT);
        const int response = gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
        while (gtk_events_pending()) gtk_main_iteration();
        if (response == GTK_RESPONSE_ACCEPT) remove_selected();
    }

    void message(GtkMessageType type, const std::string& primary, const std::string& secondary)
    {
        GtkWidget *dialog = gtk_message_dialog_new(window ? GTK_WINDOW(window) : nullptr,
            GtkDialogFlags(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT), type, GTK_BUTTONS_CLOSE, "%s", utf8(primary).c_str());
        if (!secondary.empty()) gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog), "%s", utf8(secondary).c_str());
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
        while (gtk_events_pending()) gtk_main_iteration();
    }
    void show_text(const std::string& heading, const std::string& content, bool monospace)
    {
        GtkWidget *dialog = gtk_dialog_new_with_buttons(heading.c_str(), GTK_WINDOW(window),
            GtkDialogFlags(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT), "_Close", GTK_RESPONSE_CLOSE, nullptr);
        gtk_window_set_default_size(GTK_WINDOW(dialog), 700, 480);
        GtkTextBuffer *buffer = gtk_text_buffer_new(nullptr);
        gtk_text_buffer_set_text(buffer, content.c_str(), (gint)content.size());
        GtkWidget *view = gtk_text_view_new_with_buffer(buffer);
        g_object_unref(buffer);
        gtk_text_view_set_editable(GTK_TEXT_VIEW(view), FALSE);
        gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(view), FALSE);
        gtk_text_view_set_monospace(GTK_TEXT_VIEW(view), monospace);
        gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(view), GTK_WRAP_WORD_CHAR);
        gtk_text_view_set_left_margin(GTK_TEXT_VIEW(view), 12);
        gtk_text_view_set_right_margin(GTK_TEXT_VIEW(view), 12);
        gtk_text_view_set_top_margin(GTK_TEXT_VIEW(view), 10);
        GtkWidget *scrolled = scroller(view);
        g_object_set(scrolled, "margin", 12, nullptr);
        gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), scrolled, TRUE, TRUE, 0);
        gtk_widget_show_all(scrolled);
        if (monospace)
        {
            // Logs: the interesting part (an error) is at the end.
            GtkTextIter end;
            gtk_text_buffer_get_end_iter(buffer, &end);
            GtkTextMark *mark = gtk_text_buffer_create_mark(buffer, nullptr, &end, FALSE);
            gtk_text_view_scroll_to_mark(GTK_TEXT_VIEW(view), mark, 0.0, TRUE, 0.0, 1.0);
        }
        gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_CLOSE);
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
        while (gtk_events_pending()) gtk_main_iteration();
    }
    void show_help()
    {
        show_text("ES40 machine settings",
            "The tree on the left is the emulated AlphaServer ES40. Select a device to see its settings on the right; "
            "changes apply as you make them and are written when you save (Ctrl+S).\n\n"
            "Add (or the context menu) adds processors, drives on the selected controller, and PCI cards. "
            "Remove, the Delete key or the context menu takes the selected device out again.\n\n"
            "Start saves the machine and runs the emulator in the configuration's directory; its output goes to a .log file "
            "beside the configuration (File > Show emulator log). Stop asks it to shut down and save its state.\n\n"
            "Relative file names are taken from the configuration's directory, so a machine folder can be moved as a whole.\n\n"
            "Keys: Ctrl+N new, Ctrl+O open, Ctrl+S save, Ctrl+Shift+S save as, Ctrl+R start, Ctrl+Shift+R stop, Ctrl+Q quit.", false);
    }
    std::string choose_file(bool save, const std::string& suggested = {})
    {
        GtkWidget *dialog = gtk_file_chooser_dialog_new(save ? "Save machine configuration" : "Open machine configuration",
            GTK_WINDOW(window), save ? GTK_FILE_CHOOSER_ACTION_SAVE : GTK_FILE_CHOOSER_ACTION_OPEN,
            "_Cancel", GTK_RESPONSE_CANCEL, save ? "_Save" : "_Open", GTK_RESPONSE_ACCEPT, nullptr);
        GtkFileChooser *chooser = GTK_FILE_CHOOSER(dialog);
        GtkFileFilter *configurations = gtk_file_filter_new();
        gtk_file_filter_set_name(configurations, "ES40 configurations (*.cfg)");
        gtk_file_filter_add_pattern(configurations, "*.cfg");
        gtk_file_chooser_add_filter(chooser, configurations);
        GtkFileFilter *all = gtk_file_filter_new();
        gtk_file_filter_set_name(all, "All files");
        gtk_file_filter_add_pattern(all, "*");
        gtk_file_chooser_add_filter(chooser, all);
        if (save)
        {
            gtk_file_chooser_set_do_overwrite_confirmation(chooser, TRUE);
            if (suggested.empty()) gtk_file_chooser_set_current_name(chooser, "es40.cfg");
            else gtk_file_chooser_set_filename(chooser, suggested.c_str());
        }
        std::string result;
        if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT)
            if (gchar *filename = gtk_file_chooser_get_filename(chooser)) { result = filename; g_free(filename); }
        gtk_widget_destroy(dialog);
        while (gtk_events_pending()) gtk_main_iteration();
        return result;
    }

    // ---- running the machine --------------------------------------------------
    // The emulator installed beside this editor, else the first one on PATH.
    static std::string emulator_path()
    {
        std::string result;
        if (gchar *self_path = g_file_read_link("/proc/self/exe", nullptr))
        {
            gchar *directory = g_path_get_dirname(self_path);
            result = std::string(directory) + "/es40";
            g_free(directory); g_free(self_path);
        }
        if (result.empty() || !g_file_test(result.c_str(), G_FILE_TEST_IS_EXECUTABLE))
        {
            gchar *found = g_find_program_in_path("es40");
            result = found ? found : "/usr/bin/es40";
            g_free(found);
        }
        return result;
    }
    void update_run_state()
    {
        if (!run_button) return;
        gtk_button_set_label(GTK_BUTTON(run_button), child ? "Sto_p" : "_Start");
        gtk_button_set_image(GTK_BUTTON(run_button),
            gtk_image_new_from_icon_name(child ? "media-playback-stop-symbolic" : "media-playback-start-symbolic", GTK_ICON_SIZE_BUTTON));
        gtk_widget_set_tooltip_text(run_button, child ? "Ctrl+Shift+R: ask the emulator to shut down and save its state"
                                                       : "Ctrl+R: save and run this machine in the emulator");
        set_title();
    }
    // Start runs what is on disk: unsaved edits are written first.
    void request_start()
    {
        if (child) return;
        if (dirty || config_path.empty()) { if (!save()) return; }
        start_machine();
    }
    void start_machine()
    {
        if (child) return;
        gchar *directory = g_path_get_dirname(config_path.c_str());
        gchar *name = g_path_get_basename(config_path.c_str());
        run_log_path = std::string(directory) + "/" + stem_of(name) + ".log";
        const int log = open(run_log_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
        std::string emulator = emulator_path();
        gchar *command[] = { emulator.data(), name, nullptr };
        GError *error = nullptr;
        // Relative paths in the configuration (ROMs, disk images) resolve against its directory.
        const gboolean started = g_spawn_async_with_fds(directory, command, nullptr,
            GSpawnFlags(G_SPAWN_DO_NOT_REAP_CHILD | G_SPAWN_SEARCH_PATH), nullptr, nullptr, &child, -1, log, log, &error);
        if (log >= 0) close(log);
        g_free(directory); g_free(name);
        if (!started)
        {
            child = 0;
            message(GTK_MESSAGE_ERROR, "Could not start the emulator.", std::string(error ? error->message : "") + "\n\nCommand: " + emulator);
            if (error) g_error_free(error);
            return;
        }
        last_status = -1;
        child_watch = g_child_watch_add(child, on_child_exit, this);
        update_run_state();
    }
    void stop_machine()
    {
        if (child) kill(child, SIGTERM); // The emulator saves flash/DPR/CMOS state and exits.
    }
    static void on_child_exit(GPid pid, gint status, gpointer data)
    { self(data)->guarded([&] { self(data)->machine_exited(pid, status); }); }
    void machine_exited(GPid pid, int status)
    {
        if (pid != child) return;
        last_status = WIFEXITED(status) ? WEXITSTATUS(status) : WIFSIGNALED(status) ? 128 + WTERMSIG(status) : 1;
        g_spawn_close_pid(pid);
        child = 0; child_watch = 0;
        if (!window) return;
        update_run_state();
        // A machine that stops on its own with an error usually failed while
        // starting; the reason is at the end of its log.
        if (last_status > 0)
        {
            std::string content = read_tail(run_log_path, 8 * 1024);
            if (content.empty()) content = "(The emulator wrote nothing to its log.)";
            show_text("The emulator exited with status " + std::to_string(last_status) + " \xe2\x80\x94 " + utf8(run_log_path), content, true);
        }
    }
    static std::string read_tail(const std::string& path, long limit = 64 * 1024)
    {
        std::string result;
        FILE *file = fopen(path.c_str(), "rb");
        if (!file) return result;
        if (!fseek(file, 0, SEEK_END))
        {
            const long size = ftell(file);
            const long count = std::min(size, limit);
            if (size > 0 && !fseek(file, size - count, SEEK_SET))
            {
                result.resize((size_t)count);
                result.resize(fread(result.data(), 1, (size_t)count, file));
            }
        }
        fclose(file);
        return result;
    }
    void show_log()
    {
        if (run_log_path.empty())
        {
            message(GTK_MESSAGE_INFO, "The machine has not been started from this editor yet.",
                    "Start it with Ctrl+R; its output is then written beside the configuration file.");
            return;
        }
        std::string content = read_tail(run_log_path);
        if (content.empty()) content = "(The emulator has not written anything yet.)";
        show_text("Emulator log \xe2\x80\x94 " + utf8(run_log_path), content, true);
    }

    // ---- systemd unit ----------------------------------------------------------
    std::string unit_name() const { return "es40-" + stem_of(machine_name) + ".service"; }
    std::string service_text(bool system) const
    {
        gchar *directory = g_path_get_dirname(config_path.c_str());
        std::ostringstream unit;
        unit << "# systemd unit generated by es40_cfg_gtk for " << config_path << "\n"
             << "# Install: " << (system ? "sudo install -m 644 <this file> /etc/systemd/system/" : "copy to ~/.config/systemd/user/") << "\n"
             << "# A serial console that waits for a connection holds the machine until a telnet client connects.\n\n"
             << "[Unit]\n"
             << "Description=ES40 AlphaServer emulator (" << stem_of(machine_name) << ")\n"
             << "After=network-online.target\n"
             << "Wants=network-online.target\n\n"
             << "[Service]\n"
             << "Type=simple\n"
             << "WorkingDirectory=" << directory << "\n"
             << "ExecStart=\"" << emulator_path() << "\" \"" << config_path << "\"\n"
             << "# SIGTERM lets the emulator save its flash, DPR and CMOS state before it exits.\n"
             << "KillSignal=SIGTERM\n"
             << "TimeoutStopSec=60\n"
             << "Restart=on-failure\n"
             << "RestartSec=5\n";
        if (system) unit << "User=" << g_get_user_name() << "\n";
        unit << "\n[Install]\n"
             << "WantedBy=" << (system ? "multi-user.target" : "default.target") << "\n";
        g_free(directory);
        return unit.str();
    }
    struct service_preview { Editor *ui; GtkTextBuffer *buffer; GtkWidget *system; };
    static void on_service_scope(GtkToggleButton *, gpointer data)
    {
        auto *preview = static_cast<service_preview *>(data);
        const std::string text = preview->ui->service_text(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(preview->system)));
        gtk_text_buffer_set_text(preview->buffer, text.c_str(), (gint)text.size());
    }
    void generate_service()
    {
        if (config_path.empty() || dirty) { if (!save()) return; }
        GtkWidget *dialog = gtk_dialog_new_with_buttons("Generate systemd service", GTK_WINDOW(window),
            GtkDialogFlags(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT),
            "_Cancel", GTK_RESPONSE_CANCEL, "_Save\xe2\x80\xa6", GTK_RESPONSE_ACCEPT, nullptr);
        gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT);
        gtk_window_set_default_size(GTK_WINDOW(dialog), 760, 600);
        GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
        gtk_box_set_spacing(GTK_BOX(content), 8);
        g_object_set(content, "margin", 12, nullptr);
        GtkWidget *user_scope = gtk_radio_button_new_with_mnemonic(nullptr,
            "_User service: starts when you log in, needs no root (~/.config/systemd/user)");
        GtkWidget *system_scope = gtk_radio_button_new_with_mnemonic_from_widget(GTK_RADIO_BUTTON(user_scope),
            "S_ystem service: starts at boot as your user, installed with sudo (/etc/systemd/system)");
        gtk_box_pack_start(GTK_BOX(content), user_scope, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(content), system_scope, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(content), dim_label("The unit runs the emulator in the configuration's directory and stops it with SIGTERM so it saves its state. "
                                                       "Edit the text below before saving if needed."), FALSE, FALSE, 0);
        GtkTextBuffer *buffer = gtk_text_buffer_new(nullptr);
        GtkWidget *view = gtk_text_view_new_with_buffer(buffer);
        g_object_unref(buffer);
        gtk_text_view_set_monospace(GTK_TEXT_VIEW(view), TRUE);
        gtk_text_view_set_left_margin(GTK_TEXT_VIEW(view), 8);
        gtk_text_view_set_top_margin(GTK_TEXT_VIEW(view), 6);
        gtk_box_pack_start(GTK_BOX(content), scroller(view), TRUE, TRUE, 0);
        service_preview preview{this, buffer, system_scope};
        on_service_scope(nullptr, &preview);
        g_signal_connect(system_scope, "toggled", G_CALLBACK(on_service_scope), &preview);
        gtk_widget_show_all(content);
        const int response = gtk_dialog_run(GTK_DIALOG(dialog));
        const bool system = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(system_scope));
        GtkTextIter begin, end;
        gtk_text_buffer_get_bounds(buffer, &begin, &end);
        gchar *edited = gtk_text_buffer_get_text(buffer, &begin, &end, FALSE);
        const std::string text = edited ? edited : "";
        g_free(edited);
        gtk_widget_destroy(dialog);
        while (gtk_events_pending()) gtk_main_iteration();
        if (response != GTK_RESPONSE_ACCEPT) return;

        const std::string user_directory = std::string(g_get_user_config_dir()) + "/systemd/user";
        if (!system) g_mkdir_with_parents(user_directory.c_str(), 0700);
        GtkWidget *chooser = gtk_file_chooser_dialog_new("Save systemd unit", GTK_WINDOW(window), GTK_FILE_CHOOSER_ACTION_SAVE,
            "_Cancel", GTK_RESPONSE_CANCEL, "_Save", GTK_RESPONSE_ACCEPT, nullptr);
        gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(chooser), TRUE);
        gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(chooser), system ? g_get_home_dir() : user_directory.c_str());
        gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(chooser), unit_name().c_str());
        std::string path;
        if (gtk_dialog_run(GTK_DIALOG(chooser)) == GTK_RESPONSE_ACCEPT)
            if (gchar *filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(chooser))) { path = filename; g_free(filename); }
        gtk_widget_destroy(chooser);
        while (gtk_events_pending()) gtk_main_iteration();
        if (path.empty()) return;
        GError *error = nullptr;
        if (!g_file_set_contents(path.c_str(), text.c_str(), (gssize)text.size(), &error))
        {
            message(GTK_MESSAGE_ERROR, "Could not write the unit file.", error ? error->message : path);
            if (error) g_error_free(error);
            return;
        }
        gchar *saved_directory = g_path_get_dirname(path.c_str());
        gchar *saved_name = g_path_get_basename(path.c_str());
        const std::string unit = saved_name, where = saved_directory;
        g_free(saved_directory); g_free(saved_name);
        std::string steps;
        if (system)
            steps = "Install and start it with:\n"
                    "  sudo install -m 644 \"" + path + "\" /etc/systemd/system/" + unit + "\n"
                    "  sudo systemctl daemon-reload\n"
                    "  sudo systemctl enable --now " + unit + "\n\n"
                    "Follow its output with:\n"
                    "  journalctl -u " + unit + " -f";
        else
            steps = std::string(where == user_directory ? "" : "Copy it to " + user_directory + "/ first, then e") + (where == user_directory ? "E" : "") + "nable and start it with:\n"
                    "  systemctl --user daemon-reload\n"
                    "  systemctl --user enable --now " + unit + "\n\n"
                    "Follow its output with:\n"
                    "  journalctl --user -u " + unit + " -f\n\n"
                    "To keep it running while you are logged out:\n"
                    "  sudo loginctl enable-linger " + g_get_user_name();
        show_text("Service written to " + utf8(path), steps, true);
    }

    // ---- window ----------------------------------------------------------------
    void set_title()
    {
        if (!window) return;
        gtk_window_set_title(GTK_WINDOW(window), (utf8(machine_name) + (dirty ? "*" : "") + " - ES40 machine settings").c_str());
        gtk_header_bar_set_title(GTK_HEADER_BAR(header), utf8(machine_name).c_str());
        std::string subtitle = dirty ? "unsaved changes" : config_path.empty() ? "not saved yet" : utf8(config_directory());
        if (child) subtitle = "running  \xe2\x80\xa2  " + subtitle;
        else if (last_status == 0) subtitle = "stopped  \xe2\x80\xa2  " + subtitle;
        else if (last_status > 0) subtitle = "exited with status " + std::to_string(last_status) + "  \xe2\x80\xa2  " + subtitle;
        gtk_header_bar_set_subtitle(GTK_HEADER_BAR(header), subtitle.c_str());
    }
    void refresh_recent_menu()
    {
        if (!recent_menu) return;
        gtk_container_foreach(GTK_CONTAINER(recent_menu), [](GtkWidget *child, gpointer) { gtk_widget_destroy(child); }, nullptr);
        const std::vector<std::string> machines = read_recent_machines();
        for (const auto& machine : machines)
        {
            GtkWidget *item = gtk_menu_item_new_with_label(utf8(basename_of(machine)).c_str());
            gtk_widget_set_tooltip_text(item, utf8(machine).c_str());
            g_object_set_data_full(G_OBJECT(item), "es40-path", g_strdup(machine.c_str()), g_free);
            g_signal_connect(item, "activate", G_CALLBACK(on_open_recent), this);
            gtk_menu_shell_append(GTK_MENU_SHELL(recent_menu), item);
        }
        if (machines.empty())
        {
            GtkWidget *item = gtk_menu_item_new_with_label("No recent machines");
            gtk_widget_set_sensitive(item, FALSE);
            gtk_menu_shell_append(GTK_MENU_SHELL(recent_menu), item);
        }
        gtk_widget_show_all(recent_menu);
    }

    // The Add menu: what makes sense for the selected device is enabled.
    GtkWidget *build_add_menu()
    {
        GtkWidget *menu = gtk_menu_new();
        auto item = [&](GtkWidget *parent, const char *label, const char *action, bool enabled = true)
        {
            GtkWidget *entry = gtk_menu_item_new_with_mnemonic(label);
            g_object_set_data_full(G_OBJECT(entry), "es40-add", g_strdup(action), g_free);
            g_signal_connect(entry, "activate", G_CALLBACK(on_add), this);
            gtk_widget_set_sensitive(entry, enabled);
            gtk_menu_shell_append(GTK_MENU_SHELL(parent), entry);
            return entry;
        };
        auto submenu = [&](const char *label)
        {
            GtkWidget *entry = gtk_menu_item_new_with_mnemonic(label);
            GtkWidget *sub = gtk_menu_new();
            gtk_menu_item_set_submenu(GTK_MENU_ITEM(entry), sub);
            gtk_menu_shell_append(GTK_MENU_SHELL(menu), entry);
            return sub;
        };
        const bool disks = can_add_disk();
        const Node *n = selected_node();
        const bool floppy = n && (n->kind == Node::Kind::Floppy || (n->kind == Node::Kind::Disk && nodes[n->parent].kind == Node::Kind::Floppy));
        item(menu, floppy ? "Floppy _drive" : "Hard _disk on this controller", "disk", disks);
        if (!floppy) item(menu, "_CD-ROM drive on this controller", "cdrom", disks);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
        int cpus = 0;
        for (int i = 0; i < MAX_CPUS; ++i) if (child_of(sys0, "cpu" + std::to_string(i))) ++cpus;
        item(menu, "_Processor", "cpu", cpus < MAX_CPUS);
        const bool slots = !free_pci_slots().empty();
        GtkWidget *graphics = submenu("_Graphics card");
        item(graphics, "S3 Trio64 (recommended)", "s3", slots);
        item(graphics, "ELSA GLoria Synergy", "gloria", slots);
        item(graphics, "ATI Radeon 7500", "radeon", slots);
        item(graphics, "Digital PowerStorm 300/350", "powerstorm", slots);
        item(menu, "_Network card (DEC 21143)", "dec21143", slots);
        GtkWidget *scsi = submenu("_SCSI controller");
        item(scsi, "Symbios 53C810 (bootable, recommended)", "sym53c810", slots);
        item(scsi, "LSI 53C1020 Ultra320 (not bootable from SRM)", "lsi53c1020", slots);
        GtkWidget *audio = submenu("Sound c_ard");
        item(audio, "Ensoniq ES1370", "es1370", slots);
        item(audio, "Ensoniq ES1371", "es1371", slots);
        gtk_widget_show_all(menu);
        return menu;
    }
    void add(const std::string& action)
    {
        if (action == "disk") add_disk(false);
        else if (action == "cdrom") add_disk(true);
        else if (action == "cpu") add_cpu();
        else add_card(action);
    }
    void popup_context(GdkEvent *event)
    {
        GtkWidget *menu = build_add_menu();
        gtk_menu_shell_prepend(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
        const Node *n = selected_node();
        GtkWidget *remove = gtk_menu_item_new_with_mnemonic(n ? ("_Remove " + utf8(n->title)).c_str() : "_Remove");
        gtk_widget_set_sensitive(remove, n && n->removable);
        g_signal_connect(remove, "activate", G_CALLBACK(on_remove_page), this);
        gtk_menu_shell_prepend(GTK_MENU_SHELL(menu), remove);
        gtk_widget_show_all(menu);
        g_object_ref_sink(menu);
        g_signal_connect(menu, "deactivate", G_CALLBACK(+[](GtkMenuShell *m, gpointer) { g_idle_add([](gpointer p) -> gboolean { g_object_unref(p); return G_SOURCE_REMOVE; }, m); }), nullptr);
        gtk_menu_popup_at_pointer(GTK_MENU(menu), event);
    }

    // ---- signal trampolines ----------------------------------------------------
    static gboolean on_delete(GtkWidget *, GdkEvent *, gpointer data) { self(data)->guarded([&] { self(data)->request_close(); }); return TRUE; }
    static void on_destroy(GtkWidget *, gpointer data) { self(data)->window = nullptr; gtk_main_quit(); }
    static gboolean on_key(GtkWidget *, GdkEventKey *event, gpointer data)
    {
        gboolean handled = FALSE;
        self(data)->guarded([&] { handled = self(data)->key(event); });
        return handled;
    }
    static gboolean on_tree_key(GtkWidget *, GdkEventKey *event, gpointer data)
    {
        if (event->keyval != GDK_KEY_Delete && event->keyval != GDK_KEY_KP_Delete) return FALSE;
        self(data)->guarded([&] { self(data)->confirm_remove(); });
        return TRUE;
    }
    static gboolean on_tree_button(GtkWidget *tree, GdkEventButton *event, gpointer data)
    {
        if (event->button != 3) return FALSE;
        GtkTreePath *path = nullptr;
        if (gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(tree), (int)event->x, (int)event->y, &path, nullptr, nullptr, nullptr))
        {
            gtk_tree_view_set_cursor(GTK_TREE_VIEW(tree), path, nullptr, FALSE);
            gtk_tree_path_free(path);
        }
        self(data)->guarded([&] { self(data)->popup_context((GdkEvent *)event); });
        return TRUE;
    }
    static void on_selection(GtkTreeSelection *selection, gpointer data)
    {
        Editor *editor = self(data);
        if (editor->building) return;
        GtkTreeModel *model; GtkTreeIter iter;
        if (!gtk_tree_selection_get_selected(selection, &model, &iter)) return;
        int index = 0;
        gtk_tree_model_get(model, &iter, 2, &index, -1);
        editor->guarded([&]
        {
            if (index < 0 || index >= (int)editor->nodes.size() || editor->nodes[index].key == editor->selected_key) return;
            editor->selected_key = editor->nodes[index].key;
            editor->build_page();
        });
    }
    static void on_add(GtkMenuItem *item, gpointer data)
    {
        const char *action = static_cast<const char *>(g_object_get_data(G_OBJECT(item), "es40-add"));
        self(data)->guarded([&] { if (action) self(data)->add(action); });
    }
    static void on_add_button(GtkButton *button, gpointer data)
    {
        Editor *editor = self(data);
        editor->guarded([&]
        {
            GtkWidget *menu = editor->build_add_menu();
            g_object_ref_sink(menu);
            g_signal_connect(menu, "deactivate", G_CALLBACK(+[](GtkMenuShell *m, gpointer) { g_idle_add([](gpointer p) -> gboolean { g_object_unref(p); return G_SOURCE_REMOVE; }, m); }), nullptr);
            gtk_menu_popup_at_widget(GTK_MENU(menu), GTK_WIDGET(button), GDK_GRAVITY_NORTH_WEST, GDK_GRAVITY_SOUTH_WEST, nullptr);
            // Opened from the keyboard or a click: Return should take the first usable entry.
            GList *children = gtk_container_get_children(GTK_CONTAINER(menu));
            for (GList *l = children; l; l = l->next)
                if (GTK_IS_MENU_ITEM(l->data) && !GTK_IS_SEPARATOR_MENU_ITEM(l->data) && gtk_widget_get_sensitive(GTK_WIDGET(l->data)))
                { gtk_menu_shell_select_item(GTK_MENU_SHELL(menu), GTK_WIDGET(l->data)); break; }
            g_list_free(children);
        });
    }
    static void on_save(GtkWidget *, gpointer data) { self(data)->guarded([&] { self(data)->save(); }); }
    static void on_save_as(GtkWidget *, gpointer data) { self(data)->guarded([&] { self(data)->save_as(); }); }
    static void on_new(GtkWidget *, gpointer data) { self(data)->guarded([&] { self(data)->request_new(); }); }
    static void on_open(GtkWidget *, gpointer data) { self(data)->guarded([&] { self(data)->request_open(); }); }
    static void on_open_recent(GtkMenuItem *item, gpointer data)
    {
        const char *path = static_cast<const char *>(g_object_get_data(G_OBJECT(item), "es40-path"));
        self(data)->guarded([&] { if (path) self(data)->request_open(path); });
    }
    static void on_start(GtkWidget *, gpointer data) { self(data)->guarded([&] { self(data)->request_start(); }); }
    static void on_stop(GtkWidget *, gpointer data) { self(data)->guarded([&] { self(data)->stop_machine(); }); }
    static void on_run(GtkButton *, gpointer data) { self(data)->guarded([&] { if (self(data)->child) self(data)->stop_machine(); else self(data)->request_start(); }); }
    static void on_show_log(GtkWidget *, gpointer data) { self(data)->guarded([&] { self(data)->show_log(); }); }
    static void on_service(GtkWidget *, gpointer data) { self(data)->guarded([&] { self(data)->generate_service(); }); }
    static void on_help(GtkWidget *, gpointer data) { self(data)->guarded([&] { self(data)->show_help(); }); }
    static void on_quit(GtkWidget *, gpointer data) { self(data)->guarded([&] { self(data)->request_close(); }); }

    bool key(GdkEventKey *event)
    {
        const bool control = (event->state & GDK_CONTROL_MASK) != 0, shift = (event->state & GDK_SHIFT_MASK) != 0;
        const guint k = gdk_keyval_to_lower(event->keyval);
        if (event->keyval == GDK_KEY_F1) { show_help(); return true; }
        if ((event->keyval == GDK_KEY_Delete || event->keyval == GDK_KEY_KP_Delete) && !control)
        {
            // Delete removes the selected device unless a text field has the focus.
            GtkWidget *focus = gtk_window_get_focus(GTK_WINDOW(window));
            if (focus && !GTK_IS_EDITABLE(focus) && !GTK_IS_TEXT_VIEW(focus)) { confirm_remove(); return true; }
            return false;
        }
        if (!control) return false;
        switch (k)
        {
        case GDK_KEY_s: if (shift) save_as(); else save(); return true;
        case GDK_KEY_n: request_new(); return true;
        case GDK_KEY_o: request_open(); return true;
        case GDK_KEY_q: request_close(); return true;
        case GDK_KEY_r: if (shift) stop_machine(); else request_start(); return true;
        default: return false;
        }
    }
};

void Editor::ensure_window()
{
    if (window) return;
    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_default_size(GTK_WINDOW(window), 1100, 740);
    gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_CENTER);
    gtk_window_set_icon_name(GTK_WINDOW(window), "preferences-system");
    g_signal_connect(window, "delete-event", G_CALLBACK(on_delete), this);
    g_signal_connect(window, "destroy", G_CALLBACK(on_destroy), this);
    g_signal_connect(window, "key-press-event", G_CALLBACK(on_key), this);

    header = gtk_header_bar_new();
    gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(header), TRUE);
    GtkWidget *file = gtk_menu_button_new();
    gtk_container_add(GTK_CONTAINER(file), gtk_label_new_with_mnemonic("_File"));
    GtkWidget *file_menu = gtk_menu_new();
    auto item = [&](GtkWidget *menu, const char *label, GCallback handler, const char *accelerator)
    {
        GtkWidget *entry = gtk_menu_item_new_with_mnemonic(label);
        if (accelerator)
        {
            // Shown as a right-aligned hint; the window's key handler does the work.
            GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 24);
            GtkWidget *text = gtk_bin_get_child(GTK_BIN(entry));
            g_object_ref(text); gtk_container_remove(GTK_CONTAINER(entry), text);
            gtk_box_pack_start(GTK_BOX(box), text, TRUE, TRUE, 0); g_object_unref(text);
            gtk_label_set_xalign(GTK_LABEL(text), 0.0f);
            gtk_box_pack_end(GTK_BOX(box), dim_label(accelerator), FALSE, FALSE, 0);
            gtk_container_add(GTK_CONTAINER(entry), box);
        }
        g_signal_connect(entry, "activate", handler, this);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), entry);
        return entry;
    };
    item(file_menu, "_New machine", G_CALLBACK(on_new), "Ctrl+N");
    item(file_menu, "_Open\xe2\x80\xa6", G_CALLBACK(on_open), "Ctrl+O");
    GtkWidget *recent_item = gtk_menu_item_new_with_mnemonic("Open _recent");
    recent_menu = gtk_menu_new();
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(recent_item), recent_menu);
    gtk_menu_shell_append(GTK_MENU_SHELL(file_menu), recent_item);
    gtk_menu_shell_append(GTK_MENU_SHELL(file_menu), gtk_separator_menu_item_new());
    item(file_menu, "_Save", G_CALLBACK(on_save), "Ctrl+S");
    item(file_menu, "Save _as\xe2\x80\xa6", G_CALLBACK(on_save_as), "Ctrl+Shift+S");
    gtk_menu_shell_append(GTK_MENU_SHELL(file_menu), gtk_separator_menu_item_new());
    item(file_menu, "S_tart machine", G_CALLBACK(on_start), "Ctrl+R");
    item(file_menu, "Sto_p machine", G_CALLBACK(on_stop), "Ctrl+Shift+R");
    item(file_menu, "Show emulator _log", G_CALLBACK(on_show_log), nullptr);
    item(file_menu, "Generate s_ystemd service\xe2\x80\xa6", G_CALLBACK(on_service), nullptr);
    gtk_menu_shell_append(GTK_MENU_SHELL(file_menu), gtk_separator_menu_item_new());
    item(file_menu, "_Help", G_CALLBACK(on_help), "F1");
    item(file_menu, "_Quit", G_CALLBACK(on_quit), "Ctrl+Q");
    gtk_widget_show_all(file_menu);
    gtk_menu_button_set_popup(GTK_MENU_BUTTON(file), file_menu);
    gtk_header_bar_pack_start(GTK_HEADER_BAR(header), file);
    refresh_recent_menu();
    run_button = gtk_button_new_with_mnemonic("_Start");
    gtk_button_set_always_show_image(GTK_BUTTON(run_button), TRUE);
    g_signal_connect(run_button, "clicked", G_CALLBACK(on_run), this);
    gtk_header_bar_pack_start(GTK_HEADER_BAR(header), run_button);
    GtkWidget *save_button = gtk_button_new_with_mnemonic("_Save");
    gtk_widget_set_tooltip_text(save_button, "Ctrl+S: write the configuration file");
    gtk_style_context_add_class(gtk_widget_get_style_context(save_button), "suggested-action");
    g_signal_connect(save_button, "clicked", G_CALLBACK(on_save), this);
    gtk_header_bar_pack_end(GTK_HEADER_BAR(header), save_button);
    gtk_window_set_titlebar(GTK_WINDOW(window), header);

    GtkWidget *paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_container_add(GTK_CONTAINER(window), paned);

    // Left: the device tree with Add/Remove under it.
    GtkWidget *left = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    store = gtk_tree_store_new(3, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INT);
    tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
    g_object_unref(store);
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(tree), FALSE);
    gtk_tree_view_set_level_indentation(GTK_TREE_VIEW(tree), 6);
    GtkTreeViewColumn *column = gtk_tree_view_column_new();
    GtkCellRenderer *icon = gtk_cell_renderer_pixbuf_new();
    g_object_set(icon, "xpad", 6, "ypad", 6, nullptr);
    gtk_tree_view_column_pack_start(column, icon, FALSE);
    gtk_tree_view_column_add_attribute(column, icon, "icon-name", 0);
    GtkCellRenderer *label = gtk_cell_renderer_text_new();
    g_object_set(label, "ypad", 6, "ellipsize", PANGO_ELLIPSIZE_END, nullptr);
    gtk_tree_view_column_pack_start(column, label, TRUE);
    gtk_tree_view_column_add_attribute(column, label, "markup", 1);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree), column);
    g_signal_connect(gtk_tree_view_get_selection(GTK_TREE_VIEW(tree)), "changed", G_CALLBACK(on_selection), this);
    g_signal_connect(tree, "key-press-event", G_CALLBACK(on_tree_key), this);
    g_signal_connect(tree, "button-press-event", G_CALLBACK(on_tree_button), this);
    GtkWidget *tree_scroller = scroller(tree, false);
    gtk_widget_set_size_request(tree_scroller, 330, -1);
    gtk_box_pack_start(GTK_BOX(left), tree_scroller, TRUE, TRUE, 0);
    GtkWidget *toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_style_context_add_class(gtk_widget_get_style_context(toolbar), "inline-toolbar");
    add_button = gtk_button_new_with_mnemonic("_Add\xe2\x80\xa6");
    gtk_button_set_image(GTK_BUTTON(add_button), gtk_image_new_from_icon_name("list-add-symbolic", GTK_ICON_SIZE_BUTTON));
    gtk_button_set_always_show_image(GTK_BUTTON(add_button), TRUE);
    gtk_widget_set_tooltip_text(add_button, "Add a processor, a drive on the selected controller, or a PCI card");
    g_signal_connect(add_button, "clicked", G_CALLBACK(on_add_button), this);
    remove_button = gtk_button_new_with_mnemonic("_Remove");
    gtk_button_set_image(GTK_BUTTON(remove_button), gtk_image_new_from_icon_name("list-remove-symbolic", GTK_ICON_SIZE_BUTTON));
    gtk_button_set_always_show_image(GTK_BUTTON(remove_button), TRUE);
    gtk_widget_set_tooltip_text(remove_button, "Delete: remove the selected device");
    g_signal_connect(remove_button, "clicked", G_CALLBACK(on_remove_page), this);
    gtk_box_pack_start(GTK_BOX(toolbar), add_button, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(toolbar), remove_button, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(left), toolbar, FALSE, FALSE, 0);
    gtk_paned_pack1(GTK_PANED(paned), left, FALSE, FALSE);

    // Right: the selected device's settings.
    GtkWidget *right = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    GtkWidget *title_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    g_object_set(title_row, "margin-top", 16, "margin-bottom", 4, "margin-start", 22, "margin-end", 22, nullptr);
    GtkWidget *titles = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    page_title = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(page_title), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(page_title), PANGO_ELLIPSIZE_END);
    PangoAttrList *attributes = pango_attr_list_new();
    pango_attr_list_insert(attributes, pango_attr_weight_new(PANGO_WEIGHT_BOLD));
    pango_attr_list_insert(attributes, pango_attr_scale_new(1.35));
    gtk_label_set_attributes(GTK_LABEL(page_title), attributes);
    pango_attr_list_unref(attributes);
    page_identity = dim_label("");
    gtk_label_set_ellipsize(GTK_LABEL(page_identity), PANGO_ELLIPSIZE_END);
    gtk_label_set_line_wrap(GTK_LABEL(page_identity), FALSE);
    gtk_box_pack_start(GTK_BOX(titles), page_title, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(titles), page_identity, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(title_row), titles, TRUE, TRUE, 0);
    remove_page_button = gtk_button_new_with_mnemonic("Re_move");
    gtk_widget_set_valign(remove_page_button, GTK_ALIGN_CENTER);
    gtk_style_context_add_class(gtk_widget_get_style_context(remove_page_button), "destructive-action");
    g_signal_connect(remove_page_button, "clicked", G_CALLBACK(on_remove_page), this);
    gtk_box_pack_end(GTK_BOX(title_row), remove_page_button, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(right), title_row, FALSE, FALSE, 0);
    page_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    g_object_set(page_box, "margin-start", 22, "margin-end", 22, "margin-bottom", 18, nullptr);
    page_scroller = scroller(page_box, false);
    gtk_container_set_focus_vadjustment(GTK_CONTAINER(page_box), gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(page_scroller)));
    gtk_box_pack_start(GTK_BOX(right), page_scroller, TRUE, TRUE, 0);
    gtk_paned_pack2(GTK_PANED(paned), right, TRUE, FALSE);

    gtk_widget_show_all(window);
    gtk_widget_set_no_show_all(remove_page_button, TRUE);
    update_run_state();
    gtk_window_present(GTK_WINDOW(window));
}

// The model reports parse and save failures on stdout/stderr, and a crash
// says nothing at all. Started from a desktop launcher nobody sees those
// streams, so they go to a log file that survives a crash; a terminal keeps
// seeing them directly.
class PosixEditorDiagnostics
{
    FILE *file = nullptr;
    int output = -1, error = -1;
    static void on_signal(int number)
    {
        const char *name = number == SIGSEGV ? "SIGSEGV" : number == SIGABRT ? "SIGABRT" : number == SIGBUS ? "SIGBUS" : number == SIGFPE ? "SIGFPE" : "signal";
        char line[96];
        const int length = snprintf(line, sizeof line, "\nes40_cfg_gtk: terminated by %s (%d)\n", name, number);
        if (length > 0) { ssize_t ignored = write(STDERR_FILENO, line, (size_t)length); (void)ignored; }
        signal(number, SIG_DFL);
        raise(number);
    }
public:
    std::string path;
    PosixEditorDiagnostics()
    {
        if (isatty(STDERR_FILENO)) return;
        const char *cache = getenv("XDG_CACHE_HOME");
        std::string directory = cache && *cache ? cache : (getenv("HOME") ? std::string(getenv("HOME")) + "/.cache" : "");
        if (!directory.empty())
        {
            directory += "/es40";
            mkdir(directory.c_str(), 0700);
            path = directory + "/es40_cfg_gtk.log";
            file = fopen(path.c_str(), "w+");
        }
        if (!file) { path.clear(); file = tmpfile(); }
        if (!file) return;
        fflush(stdout); fflush(stderr);
        output = dup(STDOUT_FILENO); error = dup(STDERR_FILENO);
        if (output >= 0) dup2(fileno(file), STDOUT_FILENO);
        if (error >= 0) dup2(fileno(file), STDERR_FILENO);
        setvbuf(stdout, nullptr, _IOLBF, 0);
        for (int number : {SIGSEGV, SIGABRT, SIGBUS, SIGFPE}) signal(number, on_signal);
    }
    ~PosixEditorDiagnostics()
    {
        fflush(stdout); fflush(stderr);
        if (output >= 0) { dup2(output, STDOUT_FILENO); close(output); }
        if (error >= 0) { dup2(error, STDERR_FILENO); close(error); }
        if (file) fclose(file);
    }
};

int main(int argc, char **argv)
{
    std::vector<std::string> arguments(argv, argv + argc);
    if (arguments.size() == 2 && (arguments[1] == "-h" || arguments[1] == "-?" || arguments[1] == "--help"))
    {
        printf("Usage: %s [configuration.cfg]\n"
               "       %s --render configuration.cfg\n"
               "Opens the ES40 machine settings window. Without a file, es40.cfg in the\n"
               "current directory is opened if present, otherwise a new machine is started.\n"
               "--render loads a file and prints it as the editor would save it, for tests.\n", argv[0], argv[0]);
        return 0;
    }
    if (arguments.size() == 3 && arguments[1] == "--render")
    {
        // Load a configuration and print it as the editor would save it: a
        // byte-identical result proves the writer leaves untouched files alone.
        try { editor_load(arguments[2].c_str()); editor_save("-"); editor_release(); }
        catch (const std::exception& e) { fprintf(stderr, "%s: %s\n", arguments[2].c_str(), e.what()); return 1; }
        return 0;
    }
    std::string input = arguments.size() > 1 ? arguments[1] : "";
    if (arguments.size() < 2)
        if (FILE *existing = fopen("es40.cfg", "rb")) { fclose(existing); input = "es40.cfg"; }

    PosixEditorDiagnostics capture;
    if (!gtk_init_check(nullptr, nullptr))
    {
        fprintf(stderr, "Unable to open a display for the GTK configuration editor.\n");
        return 1;
    }
    Editor editor;
    editor.ensure_window();
    editor.open_machine(input);
    editor.run();
    return 0;
}

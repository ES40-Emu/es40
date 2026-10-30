/* ES40 emulator -- Shared configuration editor backend contract.
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

#pragma once

#include <string>
#include <vector>

// Frontends only render these descriptors. All option definitions live in ConfigEditor.cpp.
struct EditorField
{
    enum class Type { Text, Choice, Integer, Number, Pattern };
    Type type = Type::Text;
    std::string value;
    std::vector<std::string> choices;
    std::string pattern;
    int precision = 0;
    double minimum = 0, maximum = 0;
};

using SelectFuncPtr = void (*)(const char *);
struct MenuEntry_t
{
    const char *text;
    const char *description;
    SelectFuncPtr select_callback;
};
using ValidationFuncPtr = void (*)(EditorField *);
struct FormEntry_t
{
    const char *label;
    const char *preset;
    const char *name;
    const char *description;
    ValidationFuncPtr validation_callback;
};
using FormValues_t = char **;
using FormCheckFuncPtr = bool (*)(FormEntry_t[], int, FormValues_t);

constexpr int MENU_SEL_EXIT = -1;
constexpr int MENU_SEL_QUIT = -2;

class EditorClosed {}; // A window close cancels the entire editor, including nested dialogs.
class EditorUI
{
public:
    virtual ~EditorUI() = default;
    virtual void initialize() = 0;
    virtual void shutdown() = 0;
    virtual void banner() = 0;
    virtual void text(const char *title, const char *text) = 0;
    virtual int menu(const char *title, MenuEntry_t[], int count, int y, int x) = 0;
    // true applies the fields; false cancels. Field values survive validation retries.
    virtual bool form(const char *title, FormEntry_t[], std::vector<EditorField>&) = 0;
    // Frontends with a file chooser may replace the output path when the user saves.
    // Return true with `path` set to a file the user has already agreed to overwrite;
    // the editor then writes it without asking again.
    virtual bool select_output(std::string&) { return false; }
    // Frontends with a persistent window may keep the session open after a
    // successful save (e.g. to start the machine) instead of ending the editor.
    virtual bool keep_editing_after_save() { return false; }
};

EditorField prepare_editor_field(const FormEntry_t&, const char *preset);
bool validate_editor_field(EditorField&);
int run_config_editor(int argc, char **argv, EditorUI&);

// Direct model access for frontends that present the device tree themselves
// (es40_cfg_gtk) instead of walking the menus. The model is the CConfigurator
// tree rooted at theConfig, with the machine at sys0; saving patches the loaded
// file so comments and untouched settings survive. Failures throw std::exception.
class CConfigurator;
extern CConfigurator *theConfig, *sys0;
void editor_load(const char *filename);
void editor_new();
void editor_save(const char *filename);
void editor_release();
std::string memory_bits2size(int mem_bits);
int memory_size2bits(char *mem_size);

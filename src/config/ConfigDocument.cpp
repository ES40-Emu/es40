/* ES40 emulator -- Source-preserving configuration documents.
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

#include "StdAfx.h"
#include "ConfigDocument.h"
#include "Configurator.h"
#include <algorithm>
#include <cstring>
#include <fcntl.h>
#include <random>
#include <set>
#include <stdexcept>
#include <system_error>
#ifdef _WIN32
#include <io.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;
struct ConfigDocumentToken
{
    size_t begin = 0, end = 0;
    char kind = 0;
    std::string value;
};
struct ConfigDocumentEntry
{
    ConfigDocumentToken name, value;
    size_t end = 0, close = 0;
    bool block = false;
    CConfigurator *original = nullptr;
    std::vector<ConfigDocumentEntry> entries;
};
struct ConfigDocumentPatch { size_t begin, end; std::string text; };

static bool bare(const std::string& value)
{
    return !value.empty() && std::all_of(value.begin(), value.end(), [](unsigned char c)
        { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                 (c >= '0' && c <= '9') || c == '_' || c == '.'; });
}
// Numbers, sizes (2G) and booleans are written bare like the shipped es40.cfg does; anything else is quoted.
static bool plain_value(const std::string& value)
{
    if (value.empty() || value.size() > 20) return false;
    std::string lowered;
    for (unsigned char c : value) lowered += (char)tolower(c);
    if (lowered == "true" || lowered == "false" || lowered == "yes" || lowered == "no") return true;
    size_t digits = 0;
    while (digits < value.size() && value[digits] >= '0' && value[digits] <= '9') ++digits;
    if (!digits) return false;
    return digits == value.size() || (digits == value.size() - 1 && strchr("KkMmGg", value.back()));
}
static std::string encode_value(const std::string& value)
{
    std::string result = "\"";
    for (char c : value)
    {
        if (c == '\r' || c == '\n' || c == '\0')
            throw std::runtime_error("Configuration values cannot contain line breaks or NUL bytes");
        result += c;
        if (c == '"') result += c;
    }
    return result + "\"";
}
static std::string plain_or_quoted(const std::string& value) { return plain_value(value) ? value : encode_value(value); }
static std::string replacement(const ConfigDocumentToken& old, const std::string& value, const std::string& source)
{
    return source[old.begin] != '"' && bare(value) ? value : encode_value(value);
}
class ConfigDocumentParser
{
    const std::string& source;
    size_t pos = 0;
    bool line_has_content = false;
    ConfigDocumentToken token;
    [[noreturn]] void fail() const
    { throw std::runtime_error("Invalid configuration syntax near byte " + std::to_string(pos)); }
    ConfigDocumentToken next()
    {
        for (;;)
        {
            if (pos == source.size()) return {pos, pos, 0, {}};
            char c = source[pos];
            if (c == '\r' || c == '\n') { line_has_content = false; ++pos; continue; }
            if (c == ' ' || c == '\t' || c == '\f' || c == '\v') { ++pos; continue; }
            if ((c == ';' && !line_has_content) || source.compare(pos, 2, "//") == 0)
            {
                pos = source.find_first_of("\r\n", pos);
                if (pos == std::string::npos) pos = source.size();
                continue;
            }
            if (source.compare(pos, 2, "/*") == 0)
            {
                size_t end = source.find("*/", pos + 2);
                if (end == std::string::npos) fail();
                if (source.find_first_of("\r\n", pos) < end) line_has_content = false;
                pos = end + 2; continue;
            }
            break;
        }
        ConfigDocumentToken t; t.begin = pos;
        const char c = source[pos++];
        line_has_content = true;
        if (c == '=' || c == ';' || c == '{' || c == '}') t.kind = c;
        else if (c == '"')
        {
            t.kind = 'v';
            for (;;)
            {
                if (pos == source.size()) fail();
                char ch = source[pos++];
                if (ch == '\r' || ch == '\n' || ch == '\0') fail();
                if (ch != '"') { t.value += ch; continue; }
                if (pos < source.size() && source[pos] == '"') { t.value += '"'; ++pos; }
                else break;
            }
        }
        else
        {
            t.kind = 'v'; t.value += c;
            while (pos < source.size() && bare(std::string(1, source[pos]))) t.value += source[pos++];
            if (!bare(t.value)) fail();
        }
        t.end = pos;
        return t;
    }
    void advance() { token = next(); }
    void entries(ConfigDocumentEntry& parent, int depth)
    {
        if (depth > 64) fail();
        int values = 0, children = 0;
        while (token.kind && token.kind != '}')
        {
            ConfigDocumentEntry entry; entry.name = token;
            if (token.kind != 'v' || !bare(token.value) || source[token.begin] == '"') fail();
            advance(); if (token.kind != '=') fail();
            advance(); if (token.kind != 'v') fail();
            entry.value = token; advance();
            if (token.kind == '{')
            {
                if (++children > CFG_MAX_CHILDREN) fail();
                entry.block = true; advance(); entries(entry, depth + 1);
                if (token.kind != '}') fail();
                entry.close = token.begin; entry.end = token.end; advance();
                if (token.kind == ';') { entry.end = token.end; advance(); }
            }
            else
            {
                if (++values > CFG_MAX_VALUES || token.kind != ';') fail();
                entry.end = token.end; advance();
            }
            parent.entries.push_back(std::move(entry));
        }
    }
public:
    explicit ConfigDocumentParser(const std::string& text) : source(text)
    {
        if (source.compare(0, 3, "\xef\xbb\xbf") == 0) pos = 3;
    }
    ConfigDocumentEntry parse()
    {
        ConfigDocumentEntry root; root.block = true; root.close = root.end = source.size();
        advance(); entries(root, 0); if (token.kind) fail(); return root;
    }
};

static void populate(const ConfigDocumentEntry& entry, CConfigurator *config)
{
    for (const auto& child : entry.entries)
        if (child.block)
        {
            auto node = new CConfigurator(config, (char *)child.name.value.c_str(), (char *)child.value.value.c_str());
            populate(child, node);
        }
        else config->add_value(strdup(child.name.value.c_str()), strdup(child.value.value.c_str()));
}

static void bind_entries(ConfigDocumentEntry& entry, CConfigurator *config)
{
    entry.original = config;
    std::set<int> used;
    for (auto& child : entry.entries)
        if (child.block)
        {
            CConfigurator *match = nullptr;
            for (int i = 0; i < config->editor_child_count(); ++i)
                if (!used.count(i) && child.name.value == config->editor_child(i)->get_myName())
                { used.insert(i); match = config->editor_child(i); break; }
            if (!match) throw std::runtime_error("Configuration source and device model disagree");
            bind_entries(child, match);
        }
}

static FILE *open_file(const fs::path& path, const char *mode)
{
#ifdef _WIN32
    std::wstring wide_mode; while (*mode) wide_mode += *mode++;
    return _wfopen(path.c_str(), wide_mode.c_str());
#else
    return fopen(path.c_str(), mode);
#endif
}
struct ConfigDocumentTemporaryFile
{
    fs::path path;
    FILE *file = nullptr;
    explicit ConfigDocumentTemporaryFile(const fs::path& target)
    {
        std::random_device random;
        for (int i = 0; i < 100; ++i)
        {
            path = target; path += ".tmp-" + std::to_string(random());
#ifdef _WIN32
            file = open_file(path, "wbx");
#else
            const int descriptor = open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR);
            if (descriptor >= 0)
            {
                file = fdopen(descriptor, "wb");
                if (!file) { close(descriptor); std::error_code ignored; fs::remove(path, ignored); }
            }
#endif
            if (file) return;
            if (errno != EEXIST) break;
        }
        throw std::runtime_error("Could not create a temporary file beside " + target.string());
    }
    ~ConfigDocumentTemporaryFile()
    {
        if (file) fclose(file);
        std::error_code ignored; fs::remove(path, ignored);
    }
    void write(const std::string& text)
    {
        if (fwrite(text.data(), 1, text.size(), file) != text.size() || fflush(file))
            throw std::runtime_error("Failed writing temporary configuration file");
#ifdef _WIN32
        if (_commit(_fileno(file)))
#else
        if (fsync(fileno(file)))
#endif
            throw std::runtime_error("Failed flushing temporary configuration file");
        FILE *closing = file; file = nullptr;
        if (fclose(closing)) throw std::runtime_error("Failed closing temporary configuration file");
    }
};
static void replace_file(const fs::path& from, const fs::path& to)
{
#ifdef _WIN32
    if (!MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        throw std::system_error(GetLastError(), std::system_category(), "Could not replace " + to.string());
#else
    fs::rename(from, to);
#endif
}

struct ConfigDocument::Impl
{
    std::string text, newline;
    ConfigDocumentEntry root;
    explicit Impl(std::string source) : text(std::move(source)), root(ConfigDocumentParser(text).parse())
    {
        size_t pos = text.find_first_of("\r\n");
        newline = pos == std::string::npos ? "\n" :
            (text[pos] == '\r' ? (text.compare(pos, 2, "\r\n") == 0 ? "\r\n" : "\r") : "\n");
    }
    std::string indent(size_t position) const
    {
        size_t start = text.find_last_of("\r\n", position ? position - 1 : 0);
        start = start == std::string::npos ? 0 : start + 1;
        size_t end = start; while (end < position && (text[end] == ' ' || text[end] == '\t')) ++end;
        return text.substr(start, end - start);
    }
    std::string child_indent(const ConfigDocumentEntry& entry) const
    {
        for (const auto& child : entry.entries)
        {
            auto value = indent(child.name.begin);
            if (value.size() > indent(entry.name.begin).size() || &entry == &root) return value;
        }
        return &entry == &root ? "" : indent(entry.name.begin) + "  ";
    }
    std::string serialize(CConfigurator *node, const std::string& padding) const
    {
        std::string value = node->get_myValue();
        std::string result = padding + node->get_myName() + " = " + (bare(value) ? value : encode_value(value)) + newline + padding + "{" + newline;
        for (int i = 0; i < node->editor_value_count(); ++i)
            result += padding + "  " + node->editor_value_name(i) + " = " + plain_or_quoted(node->editor_value(i)) + ";" + newline;
        for (int i = 0; i < node->editor_child_count(); ++i) result += serialize(node->editor_child(i), padding + "  ");
        return result + padding + "}" + newline;
    }
    // A removed entry takes its whole line when nothing else shares it, so no blank indented line stays behind.
    ConfigDocumentPatch removal(size_t begin, size_t end) const
    {
        size_t line = begin;
        while (line > 0 && (text[line - 1] == ' ' || text[line - 1] == '\t')) --line;
        const bool starts_line = line == 0 || text[line - 1] == '\n' || text[line - 1] == '\r';
        size_t after = end;
        while (after < text.size() && (text[after] == ' ' || text[after] == '\t')) ++after;
        const bool ends_line = after >= text.size() || text[after] == '\n' || text[after] == '\r';
        if (!starts_line || !ends_line) return {begin, end, ""};
        if (after < text.size() && text[after] == '\r') ++after;
        if (after < text.size() && text[after] == '\n') ++after;
        return {line, after, ""};
    }
    void insert(const ConfigDocumentEntry& entry, const std::string& content, std::vector<ConfigDocumentPatch>& patches) const
    {
        if (content.empty()) return;
        size_t position = entry.close;
        size_t line = text.find_last_of("\r\n", position ? position - 1 : 0);
        line = line == std::string::npos ? 0 : line + 1;
        bool own_line = std::all_of(text.begin() + line, text.begin() + position, [](char c) { return c == ' ' || c == '\t'; });
        if (&entry != &root && own_line) position = line;
        std::string added = content;
        if (position && text[position - 1] != '\r' && text[position - 1] != '\n') added = newline + added;
        if (&entry != &root && !own_line) added += indent(entry.name.begin);
        patches.push_back({position, position, added});
    }
    void render(const ConfigDocumentEntry& entry, CConfigurator *node, std::vector<ConfigDocumentPatch>& patches) const
    {
        if (&entry != &root)
        {
            if (entry.name.value != node->get_myName()) patches.push_back({entry.name.begin, entry.name.end, node->get_myName()});
            if (entry.value.value != node->get_myValue()) patches.push_back({entry.value.begin, entry.value.end, replacement(entry.value, node->get_myValue(), text)});
        }
        std::set<int> values, children;
        std::set<CConfigurator *> original_children;
        for (const auto& child : entry.entries)
            if (child.block && child.original) original_children.insert(child.original);
        for (const auto& child : entry.entries)
        {
            if (!child.block)
            {
                int match = -1;
                for (int i = 0; i < node->editor_value_count(); ++i)
                    if (!values.count(i) && child.name.value == node->editor_value_name(i)) { match = i; break; }
                if (match < 0) patches.push_back(removal(child.name.begin, child.end));
                else
                {
                    values.insert(match);
                    if (child.value.value != node->editor_value(match))
                        patches.push_back({child.value.begin, child.value.end, replacement(child.value, node->editor_value(match), text)});
                }
            }
            else
            {
                int match = -1;
                for (int i = 0; i < node->editor_child_count(); ++i)
                    if (!children.count(i) && child.original == node->editor_child(i)) { match = i; break; }
                if (match < 0)
                    for (int i = 0; i < node->editor_child_count(); ++i)
                        if (!children.count(i) && !original_children.count(node->editor_child(i)) && child.name.value == node->editor_child(i)->get_myName()) { match = i; break; }
                if (match < 0) patches.push_back(removal(child.name.begin, child.end));
                else { children.insert(match); render(child, node->editor_child(match), patches); }
            }
        }
        std::string added;
        const auto padding = child_indent(entry);
        for (int i = 0; i < node->editor_value_count(); ++i)
            if (!values.count(i)) added += padding + node->editor_value_name(i) + " = " + plain_or_quoted(node->editor_value(i)) + ";" + newline;
        std::vector<int> order;
        for (int i = 0; i < node->editor_child_count(); ++i) order.push_back(i);
        if (&entry == &root)
            std::stable_sort(order.begin(), order.end(), [&](int a, int b)
            { return std::string(node->editor_child(a)->get_myName()) == "gui" && std::string(node->editor_child(b)->get_myName()) != "gui"; });
        for (int i : order)
            if (!children.count(i))
            {
                auto child = node->editor_child(i);
                if (&entry == &root && std::string(child->get_myName()) == "gui")
                {
                    // The emulator initializes top-level devices in file order.
                    auto system = std::find_if(entry.entries.begin(), entry.entries.end(), [](const ConfigDocumentEntry& e) { return e.block && e.name.value == "sys0"; });
                    if (system != entry.entries.end())
                    {
                        const size_t position = system->name.begin - indent(system->name.begin).size();
                        patches.push_back({position, position, serialize(child, padding)}); continue;
                    }
                }
                added += serialize(child, padding);
            }
        insert(entry, added, patches);
    }
};

ConfigDocument::ConfigDocument(std::string source) : impl(new Impl(std::move(source))) {}
ConfigDocument::~ConfigDocument() = default;
const std::string& ConfigDocument::source() const { return impl->text; }
CConfigurator *ConfigDocument::configuration()
{
    char empty[] = "";
    auto config = new CConfigurator(nullptr, nullptr, nullptr, empty, 0);
    populate(impl->root, config);
    bind(config);
    return config;
}
void ConfigDocument::bind(CConfigurator *configuration) { bind_entries(impl->root, configuration); }
std::string ConfigDocument::render(CConfigurator *configuration) const
{
    std::vector<ConfigDocumentPatch> patches; impl->render(impl->root, configuration, patches);
    std::stable_sort(patches.begin(), patches.end(), [](const ConfigDocumentPatch& a, const ConfigDocumentPatch& b) { return a.begin < b.begin || (a.begin == b.begin && a.end < b.end); });
    std::string result; size_t cursor = 0;
    for (const auto& patch : patches)
    {
        if (patch.begin < cursor) throw std::runtime_error("Overlapping configuration edits");
        result.append(impl->text, cursor, patch.begin - cursor); result += patch.text; cursor = patch.end;
    }
    return result + impl->text.substr(cursor);
}

ConfigFileSnapshot read_config_file(const fs::path& filename, bool allow_missing)
{
    ConfigFileSnapshot result;
    result.path = fs::absolute(filename).lexically_normal();
    std::error_code error;
    const auto status = fs::status(result.path, error);
    if (error && error != std::errc::no_such_file_or_directory) throw fs::filesystem_error("Cannot inspect configuration", result.path, error);
    if (!fs::exists(status))
    {
        if (fs::is_symlink(fs::symlink_status(result.path))) throw std::runtime_error("Configuration symlink has no target");
        if (allow_missing) return result;
        throw std::runtime_error("Configuration file does not exist: " + filename.string());
    }
    if (!fs::is_regular_file(status)) throw std::runtime_error("Configuration is not a regular file: " + filename.string());
    result.path = fs::canonical(result.path);
    FILE *file = open_file(result.path, "rb");
    if (!file) throw std::runtime_error("Cannot read configuration: " + filename.string());
    char buffer[8192]; size_t count;
    while ((count = fread(buffer, 1, sizeof(buffer), file))) result.text.append(buffer, count);
    const bool failed = ferror(file) != 0; fclose(file);
    if (failed) throw std::runtime_error("Failed reading configuration: " + filename.string());
    result.exists = true;
    return result;
}

void save_config_file(const ConfigFileSnapshot& expected, const std::string& text)
{
    auto current = read_config_file(expected.path, true);
    auto unchanged = [&]()
    {
        auto state = read_config_file(expected.path, true);
        if (state.exists != expected.exists || state.text != expected.text)
            throw std::runtime_error("Configuration changed on disk; reload it before saving: " + expected.path.string());
    };
    unchanged();
    if (current.exists && current.text == text) return;
    if (current.exists && (fs::status(current.path).permissions() & (fs::perms::owner_write | fs::perms::group_write | fs::perms::others_write)) == fs::perms::none)
        throw std::runtime_error("Configuration is read-only: " + current.path.string());
    ConfigDocumentTemporaryFile output(expected.path); output.write(text);
    fs::path backup = expected.path; backup += ".bak";
    if (current.exists)
    {
        fs::permissions(output.path, fs::status(current.path).permissions());
        ConfigDocumentTemporaryFile copy(backup); copy.write(current.text);
        fs::permissions(copy.path, fs::status(current.path).permissions());
        unchanged();
        replace_file(copy.path, backup);
    }
    unchanged();
    replace_file(output.path, expected.path);
#ifndef _WIN32
    // Flush the directory entry after the atomic rename where supported.
    int directory = open(expected.path.parent_path().c_str(), O_RDONLY);
    if (directory >= 0) { fsync(directory); close(directory); }
#endif
}

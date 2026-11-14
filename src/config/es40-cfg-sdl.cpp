/* ES40 emulator -- SDL configuration editor frontend.
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

// Font data in gui/sdl_fonts.h retains its LGPL-2.0-or-later license.

#include "ConfigEditor.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include "gui/sdl_fonts.h"
#include <algorithm>
#include <cstring>
#include <stdexcept>

static constexpr int WIDTH = 1100, HEIGHT = 760;
static constexpr int TOP = 88, BOTTOM = 560, ROW = 36, FORM_ROW = 64;
static constexpr SDL_Color BACKGROUND{18, 24, 35, 255}, PANEL{28, 37, 52, 255};
static constexpr SDL_Color INK{228, 234, 244, 255}, MUTED{157, 174, 195, 255};
static constexpr SDL_Color ACCENT{67, 123, 186, 255}, INPUT{12, 19, 30, 255};

static size_t previous_char(const std::string& value, size_t pos)
{
    if (pos) --pos;
    while (pos && (static_cast<unsigned char>(value[pos]) & 0xc0) == 0x80) --pos;
    return pos;
}
static size_t next_char(const std::string& value, size_t pos)
{
    if (pos < value.size()) ++pos;
    while (pos < value.size() && (static_cast<unsigned char>(value[pos]) & 0xc0) == 0x80) ++pos;
    return pos;
}
static std::vector<std::string> wrap_text(const std::string& body, size_t width)
{
    std::vector<std::string> lines;
    size_t start = 0;
    do
    {
        size_t end = body.find('\n', start);
        if (end == std::string::npos) end = body.size();
        std::string line = body.substr(start, end - start);
        while (line.size() > width)
        {
            size_t split = line.rfind(' ', width);
            if (split == std::string::npos || split == 0) split = width;
            lines.push_back(line.substr(0, split));
            line.erase(0, split + (line[split] == ' ' ? 1 : 0));
        }
        lines.push_back(line);
        start = end + 1;
    } while (start <= body.size());
    return lines;
}

class ES40SdlConfigEditorUI final : public EditorUI
{
    SDL_Window *window = nullptr;
    SDL_Renderer *renderer = nullptr;
    SDL_Texture *font = nullptr;

    static void require(bool ok)
    { if (!ok) throw std::runtime_error(SDL_GetError()); }
    void color(SDL_Color c) { SDL_SetRenderDrawColor(renderer, c.r, c.g, c.b, c.a); }
    void rectangle(float x, float y, float w, float h, SDL_Color c)
    {
        SDL_FRect rect{x, y, w, h}; color(c); SDL_RenderFillRect(renderer, &rect);
    }
    // The emulator's existing bitmap font keeps this frontend self-contained.
    // UTF-8 values are retained; glyphs outside the font are shown as '?'.
    void label(float x, float y, const std::string& value, SDL_Color c = INK, int limit = 102)
    {
        SDL_SetTextureColorMod(font, c.r, c.g, c.b);
        for (size_t i = 0; i < value.size() && limit-- > 0; i = next_char(value, i), x += 10)
        {
            unsigned char glyph = static_cast<unsigned char>(value[i]);
            if (glyph >= 128 || glyph < 32) glyph = '?';
            SDL_FRect src{float((glyph % 16) * 8), float((glyph / 16) * 16), 8, 16};
            SDL_FRect dest{x, y, 10, 20};
            SDL_RenderTexture(renderer, font, &src, &dest);
        }
    }
    void frame(const char *title, const char *subtitle)
    {
        color(BACKGROUND); SDL_RenderClear(renderer);
        rectangle(0, 0, WIDTH, 70, PANEL);
        rectangle(0, 0, 6, 70, ACCENT);
        label(26, 12, "ES40 / CONFIGURATION", MUTED);
        label(26, 38, title);
        label(26, 574, subtitle, MUTED);
    }
    void description(const char *body)
    {
        auto lines = wrap_text(body ? body : "", 102);
        for (size_t i = 0; i < lines.size() && i < 4; ++i)
            label(26, 602 + float(i) * 21, lines[i], MUTED);
    }
    void button(int x, const char *body, bool primary = false)
    {
        rectangle(float(x), 706, 242, 36, primary ? ACCENT : PANEL);
        label(float(x + 12), 714, body, INK, 22);
    }
    int clicked_button(const SDL_Event& event)
    {
        if (event.type != SDL_EVENT_MOUSE_BUTTON_DOWN || event.button.button != SDL_BUTTON_LEFT ||
            event.button.y < 706 || event.button.y >= 742) return -1;
        for (int i = 0; i < 3; ++i)
            if (event.button.x >= 26 + i * 260 && event.button.x < 268 + i * 260) return i;
        return -1;
    }
    SDL_Event wait_event()
    {
        SDL_Event event;
        require(SDL_WaitEvent(&event));
        if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED)
            throw EditorClosed{};
        SDL_ConvertEventToRenderCoordinates(renderer, &event);
        return event;
    }
    static void visible(int current, int& first, int count, int rows)
    {
        if (current < first) first = current;
        if (current >= first + rows) first = current - rows + 1;
        first = std::max(0, std::min(first, std::max(0, count - rows)));
    }
public:
    void initialize() override
    {
        require(SDL_Init(SDL_INIT_VIDEO));
        window = SDL_CreateWindow("ES40 Configuration Editor", WIDTH, HEIGHT,
                                  SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
        require(window != nullptr);
        SDL_SetWindowMinimumSize(window, 700, 500);
        renderer = SDL_CreateRenderer(window, nullptr);
        require(renderer != nullptr);
        require(SDL_SetRenderLogicalPresentation(renderer, WIDTH, HEIGHT, SDL_LOGICAL_PRESENTATION_LETTERBOX));
        SDL_Surface *surface = SDL_CreateSurface(128, 256, SDL_PIXELFORMAT_RGBA32);
        require(surface != nullptr);
        for (int glyph = 0; glyph < 256; ++glyph)
            for (int y = 0; y < 16; ++y)
                for (int x = 0; x < 8; ++x)
                {
                    auto pixel = static_cast<Uint8 *>(surface->pixels) +
                                 ((glyph / 16) * 16 + y) * surface->pitch + ((glyph % 16) * 8 + x) * 4;
                    pixel[0] = pixel[1] = pixel[2] = 255;
                    pixel[3] = (sdl_font8x16[glyph][y] & (0x80 >> x)) ? 255 : 0;
                }
        font = SDL_CreateTextureFromSurface(renderer, surface);
        SDL_DestroySurface(surface);
        require(font != nullptr);
        SDL_SetTextureBlendMode(font, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(font, SDL_SCALEMODE_NEAREST);
    }
    void shutdown() override
    {
        SDL_DestroyTexture(font); font = nullptr;
        SDL_DestroyRenderer(renderer); renderer = nullptr;
        SDL_DestroyWindow(window); window = nullptr;
        SDL_Quit();
    }
    void banner() override {} // Main menu is immediately available in the window.
    void text(const char *title, const char *body) override
    {
        auto lines = wrap_text(body ? body : "", 102);
        int first = 0;
        const int rows = 22;
        for (;;)
        {
            frame(title, "Up / Down / mouse wheel: scroll");
            for (int i = first; i < int(lines.size()) && i < first + rows; ++i)
                label(26, TOP + float(i - first) * 21, lines[i]);
            button(26, "Enter / Esc: Close", true);
            SDL_RenderPresent(renderer);
            auto event = wait_event();
            if (clicked_button(event) == 0) return;
            if (event.type == SDL_EVENT_MOUSE_WHEEL) first -= int(event.wheel.y * 3);
            if (event.type == SDL_EVENT_KEY_DOWN)
            {
                switch (event.key.key)
                {
                case SDLK_UP: --first; break;
                case SDLK_DOWN: ++first; break;
                case SDLK_PAGEUP: first -= rows; break;
                case SDLK_PAGEDOWN: first += rows; break;
                case SDLK_RETURN: case SDLK_KP_ENTER: case SDLK_ESCAPE:
                case SDLK_F1: case SDLK_F2: case SDLK_F3: return;
                default: break;
                }
            }
            first = std::max(0, std::min(first, std::max(0, int(lines.size()) - rows)));
        }
    }
    int menu(const char *title, MenuEntry_t entry[], int count, int, int) override
    {
        int current = 0, first = 0;
        const int rows = (BOTTOM - TOP) / ROW;
        auto move = [&](int direction)
        {
            do { current = (current + direction + count) % count; } while (!entry[current].text);
        };
        if (!entry[current].text) move(1);
        for (;;)
        {
            visible(current, first, count, rows);
            frame(title, "Arrows: select    Enter / click: open    F1: help");
            label(940, 38, std::to_string(current + 1) + " / " + std::to_string(count), MUTED, 13);
            for (int i = first; i < count && i < first + rows; ++i)
            {
                float y = TOP + float(i - first) * ROW;
                if (!entry[i].text) { rectangle(26, y + ROW / 2, WIDTH - 52, 1, MUTED); continue; }
                if (i == current) rectangle(26, y, WIDTH - 52, ROW - 2, ACCENT);
                label(40, y + 7, entry[i].text);
                if (entry[i].select_callback) label(WIDTH - 58, y + 7, ">");
            }
            description(entry[current].description);
            button(26, "Enter: Open", true); button(286, std::strcmp(title, "Main menu") == 0 ? "F2: Save and exit" : "F2: Back"); button(546, "F3: Quit");
            SDL_RenderPresent(renderer);
            auto event = wait_event();
            int button = clicked_button(event);
            bool activate = button == 0;
            if (button == 1) return MENU_SEL_EXIT;
            if (button == 2) return MENU_SEL_QUIT;
            if (event.type == SDL_EVENT_MOUSE_WHEEL) move(event.wheel.y > 0 ? -1 : 1);
            if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT &&
                event.button.x >= 26 && event.button.x < WIDTH - 26 && event.button.y >= TOP &&
                event.button.y < TOP + rows * ROW)
            {
                int idx = first + int(event.button.y - TOP) / ROW;
                if (idx < count && entry[idx].text) { current = idx; activate = true; }
            }
            if (event.type == SDL_EVENT_KEY_DOWN)
            {
                switch (event.key.key)
                {
                case SDLK_UP: move(-1); break;
                case SDLK_DOWN: move(1); break;
                case SDLK_HOME: current = count - 1; move(1); break;
                case SDLK_END: current = 0; move(-1); break;
                case SDLK_F1: text(entry[current].text, entry[current].description); break;
                case SDLK_F2: return MENU_SEL_EXIT;
                case SDLK_ESCAPE: case SDLK_F3: return MENU_SEL_QUIT;
                case SDLK_RETURN: case SDLK_KP_ENTER: activate = true; break;
                default: break;
                }
            }
            if (activate)
            {
                if (!entry[current].select_callback) return current;
                entry[current].select_callback(entry[current].text);
            }
        }
    }
    bool form(const char *title, FormEntry_t entry[], std::vector<EditorField>& fields) override
    {
        int current = 0, first = 0;
        const int count = int(fields.size()), rows = (BOTTOM - TOP) / FORM_ROW;
        size_t cursor = fields[0].value.size();
        bool selected = true;
        struct TextInputScope
        {
            SDL_Window *window;
            ~TextInputScope() { SDL_StopTextInput(window); }
        } input_scope{window};
        require(SDL_StartTextInput(window));
        auto focus = [&](int idx) { current = idx; cursor = fields[current].value.size(); selected = true; };
        auto cycle = [&](int direction)
        {
            auto& field = fields[current];
            if (field.choices.empty()) return;
            auto found = std::find(field.choices.begin(), field.choices.end(), field.value);
            int idx = found == field.choices.end() ? (direction > 0 ? -1 : 0) : int(found - field.choices.begin());
            field.value = field.choices[(idx + direction + int(field.choices.size())) % field.choices.size()];
            cursor = field.value.size();
            if (field.is_editable()) selected = true;
        };
        auto insert = [&](const std::string& value)
        {
            auto& field = fields[current];
            if (!field.is_editable()) return;
            if (selected) { field.value.clear(); cursor = 0; selected = false; }
            // Match the terminal frontend's 256-byte field limit without splitting UTF-8.
            if (field.value.size() + value.size() <= 256 && value.find_first_of("\r\n\t") == std::string::npos)
            { field.value.insert(cursor, value); cursor += value.size(); }
        };
        for (;;)
        {
            visible(current, first, count, rows);
            frame(title, "Up / Down: field    Tab / Left / Right: choice    F1: help");
            label(940, 38, std::to_string(current + 1) + " / " + std::to_string(count), MUTED, 13);
            for (int i = first; i < count && i < first + rows; ++i)
            {
                float y = TOP + float(i - first) * FORM_ROW;
                label(26, y, entry[i].label, i == current ? INK : MUTED);
                rectangle(26, y + 24, WIDTH - 52, 30, i == current ? ACCENT : INPUT);
                const auto& field = fields[i];
                size_t offset = 0;
                if (i == current)
                    while (cursor - offset > 96) offset = next_char(field.value, offset);
                label(38, y + 29, field.value.substr(offset), INK, 98);
                if (field.is_choice()) label(WIDTH - 58, y + 29, "<>");
                if (field.is_editable() && i == current)
                {
                    int column = 0;
                    for (size_t pos = offset; pos < cursor; pos = next_char(field.value, pos)) ++column;
                    rectangle(38 + float(column) * 10, y + 49, 10, 2, INK);
                    if (selected) label(WIDTH - 188, y + 29, "[replace]", MUTED, 9);
                }
            }
            description(entry[current].description);
            button(26, "F2: Apply", true); button(286, "F3 / Esc: Cancel"); button(546, "F1: Field help");
            SDL_RenderPresent(renderer);
            auto event = wait_event();
            int button = clicked_button(event);
            if (button == 0) return true;
            if (button == 1) return false;
            if (button == 2) text(entry[current].label, entry[current].description);
            if (event.type == SDL_EVENT_MOUSE_WHEEL)
                focus(std::max(0, std::min(count - 1, current + (event.wheel.y > 0 ? -1 : 1))));
            if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT &&
                event.button.x >= 26 && event.button.x < WIDTH - 26 && event.button.y >= TOP &&
                event.button.y < TOP + rows * FORM_ROW)
            {
                int idx = first + int(event.button.y - TOP) / FORM_ROW;
                if (idx < count)
                {
                    focus(idx);
                    if (fields[idx].is_choice() && (!fields[idx].is_editable() || event.button.x >= WIDTH - 76))
                        cycle(1);
                }
            }
            if (event.type == SDL_EVENT_TEXT_INPUT) insert(event.text.text);
            if (event.type != SDL_EVENT_KEY_DOWN) continue;
            auto& field = fields[current];
            const bool choice = field.type == EditorField::Type::Choice;
            if (event.key.mod & SDL_KMOD_CTRL)
            {
                switch (event.key.key)
                {
                case SDLK_A: selected = true; break;
                case SDLK_C: if (selected) SDL_SetClipboardText(field.value.c_str()); break;
                case SDLK_X:
                    if (selected && !choice) { SDL_SetClipboardText(field.value.c_str()); field.value.clear(); cursor = 0; }
                    break;
                case SDLK_V:
                {
                    char *clipboard = SDL_GetClipboardText();
                    if (clipboard) { insert(clipboard); SDL_free(clipboard); }
                    break;
                }
                default: break;
                }
                continue;
            }
            switch (event.key.key)
            {
            case SDLK_F1: text(entry[current].label, entry[current].description); break;
            case SDLK_F2: return true;
            case SDLK_F3: case SDLK_ESCAPE: return false;
            case SDLK_UP: focus((current + count - 1) % count); break;
            case SDLK_DOWN: case SDLK_RETURN: case SDLK_KP_ENTER: focus((current + 1) % count); break;
            case SDLK_PAGEUP: focus(std::max(0, current - rows)); break;
            case SDLK_PAGEDOWN: focus(std::min(count - 1, current + rows)); break;
            case SDLK_TAB:
                if (field.is_choice()) cycle((event.key.mod & SDL_KMOD_SHIFT) ? -1 : 1);
                else focus((current + ((event.key.mod & SDL_KMOD_SHIFT) ? count - 1 : 1)) % count);
                break;
            case SDLK_LEFT:
                if (choice) cycle(-1); else { cursor = previous_char(field.value, cursor); selected = false; }
                break;
            case SDLK_RIGHT:
                if (choice) cycle(1); else { cursor = next_char(field.value, cursor); selected = false; }
                break;
            case SDLK_HOME: cursor = 0; selected = false; break;
            case SDLK_END: cursor = field.value.size(); selected = false; break;
            case SDLK_BACKSPACE: case SDLK_DELETE:
                if (!choice)
                {
                    if (selected) { field.value.clear(); cursor = 0; selected = false; }
                    else if (event.key.key == SDLK_BACKSPACE && cursor)
                    { size_t prev = previous_char(field.value, cursor); field.value.erase(prev, cursor - prev); cursor = prev; }
                    else if (event.key.key == SDLK_DELETE && cursor < field.value.size())
                        field.value.erase(cursor, next_char(field.value, cursor) - cursor);
                }
                break;
            default: break;
            }
        }
    }
};

int main(int argc, char **argv)
{
    ES40SdlConfigEditorUI ui;
    return run_config_editor(argc, argv, ui);
}

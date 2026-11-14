/* ES40 emulator -- native Windows configuration editor frontend.
 * Copyright (C) 2026 by the ES40 Emulator Project
 * SPDX-License-Identifier: BSD-1-Clause
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
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

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <shellapi.h>
#include <io.h>
#include <fcntl.h>
#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include "ConfigEditor.h"

#if defined(_MSC_VER)
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
#endif

constexpr wchar_t editor_window_class[] = L"ES40.ConfigEditor.Win32";
constexpr wchar_t form_window_class[] = L"ES40.ConfigEditor.Form";
// Stable control IDs also permit testing the real frontend with Win32 messages.
constexpr int id_quit = 1003, id_help = 1004, id_menu = 1100, id_field = 2000;
constexpr int pending_action = INT_MIN, help_action = 10;

static std::wstring wide(const std::string& text)
{
    if (text.empty()) return {};
    const int count = MultiByteToWideChar(CP_ACP, 0, text.data(), (int)text.size(), nullptr, 0);
    if (!count) throw std::runtime_error("Unable to decode configuration text.");
    std::wstring result(count, L'\0');
    MultiByteToWideChar(CP_ACP, 0, text.data(), (int)text.size(), result.data(), count);
    return result;
}

static std::string narrow(const std::wstring& text)
{
    if (text.empty()) return {};
    const bool utf8 = GetACP() == CP_UTF8;
    BOOL substituted = FALSE;
    const DWORD flags = utf8 ? 0 : WC_NO_BEST_FIT_CHARS;
    const int count = WideCharToMultiByte(CP_ACP, flags, text.data(), (int)text.size(), nullptr, 0,
        nullptr, utf8 ? nullptr : &substituted);
    if (!count || substituted) throw std::runtime_error("This value cannot be represented in the active Windows code page.");
    std::string result(count, '\0');
    if (!WideCharToMultiByte(CP_ACP, flags, text.data(), (int)text.size(), result.data(), count,
        nullptr, utf8 ? nullptr : &substituted) || substituted)
        throw std::runtime_error("This value cannot be represented in the active Windows code page.");
    return result;
}

static std::wstring control_text(const char* source)
{
    const std::wstring text = wide(source ? source : "");
    std::wstring result;
    for (size_t i = 0; i < text.size(); ++i)
    {
        if (text[i] == L'\r')
        { if (i + 1 < text.size() && text[i + 1] == L'\n') ++i; result += L"\r\n"; }
        else if (text[i] == L'\n') result += L"\r\n";
        else result += text[i];
    }
    return result;
}

static std::wstring window_text(HWND control)
{
    std::wstring text(GetWindowTextLengthW(control) + 1, L'\0');
    GetWindowTextW(control, text.data(), (int)text.size());
    text.resize(wcslen(text.c_str()));
    return text;
}

static HWND combo_edit(HWND control)
{
    COMBOBOXINFO info = {sizeof(info)};
    if (!GetComboBoxInfo(control, &info) || !info.hwndItem)
        throw std::runtime_error("Unable to access the editable settings control.");
    return info.hwndItem;
}

class Win32EditorUI final : public EditorUI
{
    enum class kind { menu, form, text };
    struct page
    {
        kind type = kind::menu;
        std::string title, body;
        MenuEntry_t* menu_entries = nullptr;
        FormEntry_t* form_entries = nullptr;
        std::vector<EditorField>* fields = nullptr;
        std::vector<int> menu_indices;
        std::vector<std::wstring> drafts;
        std::vector<HWND> labels, inputs;
        int current = 0, scroll = 0, action = pending_action;
        bool main_menu = false;
    };
    HWND owner = nullptr, window = nullptr, viewport = nullptr, list = nullptr;
    HWND heading = nullptr, subtitle = nullptr, description = nullptr;
    HFONT font = nullptr, title_font = nullptr;
    HBRUSH background = nullptr;
    std::vector<HWND> controls;
    page* active = nullptr;
    std::exception_ptr failure;
    bool closed = false, building = false;
    int dpi = 96;
    std::wstring machine_name;

    int px(int value) const { return MulDiv(value, dpi, 96); }
    static LRESULT CALLBACK procedure(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
    {
        auto* ui = reinterpret_cast<Win32EditorUI*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (message == WM_NCCREATE)
        {
            ui = static_cast<Win32EditorUI*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)ui);
        }
        if (!ui) return DefWindowProcW(hwnd, message, wparam, lparam);
        try { return ui->event(hwnd, message, wparam, lparam); }
        catch (...) { ui->failure = std::current_exception(); return 0; }
    }

    HWND add_control(HWND parent, const wchar_t* type, const std::wstring& text, DWORD style, int id, DWORD extended = 0)
    {
        HWND control = CreateWindowExW(extended, type, text.c_str(), WS_CHILD | WS_VISIBLE | style,
            0, 0, 1, 1, parent, (HMENU)(INT_PTR)id, GetModuleHandleW(nullptr), nullptr);
        if (!control) throw std::runtime_error("Unable to create a native settings control.");
        SendMessageW(control, WM_SETFONT, (WPARAM)font, TRUE);
        if (parent == window) controls.push_back(control);
        return control;
    }

    void destroy_controls()
    {
        building = true;
        for (HWND control : controls) DestroyWindow(control);
        controls.clear();
        viewport = list = heading = subtitle = description = nullptr;
        building = false;
    }

    void update_description()
    {
        if (!active || !description || active->type == kind::text) return;
        const char* body = "";
        if (active->type == kind::menu && !active->menu_indices.empty())
            body = active->menu_entries[active->menu_indices[active->current]].description;
        else if (active->type == kind::form && active->fields && !active->fields->empty())
            body = active->form_entries[active->current].description;
        SetWindowTextW(description, control_text(body).c_str());
    }

    void form_layout()
    {
        if (!active || !viewport || active->type != kind::form) return;
        RECT bounds; GetClientRect(viewport, &bounds);
        const int row = px(76), total = (int)active->inputs.size() * row + px(12);
        const int maximum = std::max(0, total - (int)bounds.bottom);
        active->scroll = std::max(0, std::min(active->scroll, maximum));
        SCROLLINFO scroll = { sizeof(scroll), SIF_RANGE | SIF_PAGE | SIF_POS };
        scroll.nMin = 0; scroll.nMax = std::max(0, total - 1);
        scroll.nPage = (UINT)bounds.bottom; scroll.nPos = active->scroll;
        SetScrollInfo(viewport, SB_VERT, &scroll, TRUE);
        for (size_t i = 0; i < active->inputs.size(); ++i)
        {
            const int top = px(10) + (int)i * row - active->scroll;
            MoveWindow(active->labels[i], px(12), top, std::max(1, (int)bounds.right - px(28)), px(22), TRUE);
            const auto& field = (*active->fields)[i];
            const bool choice = field.is_choice();
            HWND edit = field.type == EditorField::Type::EditableChoice ? combo_edit(active->inputs[i]) : nullptr;
            const std::wstring draft = edit ? window_text(edit) : L"";
            DWORD start = 0, end = 0;
            if (edit) SendMessageW(edit, EM_GETSEL, (WPARAM)&start, (LPARAM)&end);
            MoveWindow(active->inputs[i], px(12), top + px(26), std::max(1, (int)bounds.right - px(28)), choice ? px(280) : px(28), TRUE);
            // Resizing a combo can replace typed text with a case-insensitive match.
            if (edit && window_text(edit) != draft)
            {
                SetWindowTextW(edit, draft.c_str());
                SendMessageW(edit, EM_SETSEL, start, end);
            }
        }
    }

    void reveal_field(int index)
    {
        if (!active || !viewport || index < 0 || index >= (int)active->inputs.size()) return;
        active->current = index;
        RECT bounds; GetClientRect(viewport, &bounds);
        const int top = px(10) + index * px(76), bottom = top + px(58);
        if (top < active->scroll) active->scroll = top;
        else if (bottom > active->scroll + bounds.bottom) active->scroll = bottom - bounds.bottom;
        form_layout(); update_description();
    }

    void scroll_form(int delta)
    {
        if (!active || active->type != kind::form) return;
        active->scroll += delta; form_layout();
    }

    void layout()
    {
        if (!active || !heading) return;
        RECT bounds; GetClientRect(window, &bounds);
        const int width = bounds.right, height = bounds.bottom, margin = px(20);
        MoveWindow(heading, margin, px(16), width - margin * 2, px(32), TRUE);
        MoveWindow(subtitle, margin, px(52), width - margin * 2, px(26), TRUE);
        const int bottom = height - px(60);
        if (active->type == kind::text)
        {
            MoveWindow(description, margin, px(94), width - margin * 2, std::max(1, bottom - px(110)), TRUE);
        }
        else
        {
            const int content_height = std::max(px(90), bottom - px(250));
            MoveWindow(active->type == kind::form ? viewport : list, margin, px(94), width - margin * 2, content_height, TRUE);
            MoveWindow(description, margin, px(104) + content_height, width - margin * 2, std::max(px(60), bottom - px(120) - content_height), TRUE);
        }
        if (HWND button = GetDlgItem(window, IDOK)) MoveWindow(button, margin, bottom + px(8), px(132), px(32), TRUE);
        if (HWND button = GetDlgItem(window, IDCANCEL)) MoveWindow(button, margin + px(144), bottom + px(8), px(152), px(32), TRUE);
        if (HWND button = GetDlgItem(window, id_help)) MoveWindow(button, margin + px(308), bottom + px(8), px(116), px(32), TRUE);
        if (HWND button = GetDlgItem(window, id_quit)) MoveWindow(button, width - margin - px(132), bottom + px(8), px(132), px(32), TRUE);
        form_layout();
    }

    void build_page(page& current)
    {
        building = true;
        current.inputs.clear(); current.labels.clear();
        heading = add_control(window, L"STATIC", L"Machine settings", SS_LEFT, 101);
        SendMessageW(heading, WM_SETFONT, (WPARAM)title_font, TRUE);
        subtitle = add_control(window, L"STATIC", wide(current.title), SS_LEFT, 102);
        if (current.type == kind::menu)
        {
            list = add_control(window, L"LISTBOX", L"", LBS_NOTIFY | LBS_NOINTEGRALHEIGHT | WS_VSCROLL | WS_TABSTOP,
                id_menu, WS_EX_CLIENTEDGE);
            for (int index : current.menu_indices)
            {
                const auto& item = current.menu_entries[index];
                std::wstring label = wide(item.text);
                if (item.select_callback) label += L"  >";
                SendMessageW(list, LB_ADDSTRING, 0, (LPARAM)label.c_str());
            }
            SendMessageW(list, LB_SETCURSEL, current.current, 0);
        }
        else if (current.type == kind::form)
        {
            viewport = CreateWindowExW(WS_EX_CONTROLPARENT | WS_EX_CLIENTEDGE, form_window_class, L"",
                WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_CLIPCHILDREN, 0, 0, 1, 1, window, nullptr, GetModuleHandleW(nullptr), this);
            if (!viewport) throw std::runtime_error("Unable to create the settings form.");
            controls.push_back(viewport);
            for (size_t i = 0; i < current.fields->size(); ++i)
            {
                const auto& field = (*current.fields)[i];
                std::wstring label = wide(current.form_entries[i].label ? current.form_entries[i].label : "");
                if (field.type == EditorField::Type::Integer || field.type == EditorField::Type::Number)
                {
                    std::wostringstream range;
                    range << L"  (" << field.minimum << L" to " << field.maximum << L")";
                    label += range.str();
                }
                current.labels.push_back(add_control(viewport, L"STATIC", label, SS_LEFT, 3000 + (int)i));
                const bool choice = field.is_choice();
                const DWORD combo_style = field.is_editable() ? CBS_DROPDOWN | CBS_AUTOHSCROLL : CBS_DROPDOWNLIST;
                HWND input = add_control(viewport, choice ? L"COMBOBOX" : L"EDIT", choice ? L"" : current.drafts[i],
                    WS_TABSTOP | (choice ? combo_style | CBS_HASSTRINGS | WS_VSCROLL : ES_AUTOHSCROLL), id_field + (int)i,
                    choice ? 0 : WS_EX_CLIENTEDGE);
                current.inputs.push_back(input);
                if (choice)
                {
                    for (const auto& option : field.choices)
                    { const std::wstring text = wide(option); SendMessageW(input, CB_ADDSTRING, 0, (LPARAM)text.c_str()); }
                    if (field.is_editable())
                    {
                        SendMessageW(input, CB_LIMITTEXT, 32767, 0);
                        // Use the edit child to preserve the exact spelling.
                        SetWindowTextW(combo_edit(input), current.drafts[i].c_str());
                    }
                    else
                    {
                        LRESULT selected = SendMessageW(input, CB_FINDSTRINGEXACT, (WPARAM)-1, (LPARAM)current.drafts[i].c_str());
                        if (selected == CB_ERR)
                            selected = SendMessageW(input, CB_ADDSTRING, 0, (LPARAM)current.drafts[i].c_str());
                        SendMessageW(input, CB_SETCURSEL, selected, 0);
                    }
                    SendMessageW(input, CB_SETMINVISIBLE, 12, 0);
                }
                else SendMessageW(input, EM_SETLIMITTEXT, 32767, 0);
            }
        }
        description = add_control(window, L"EDIT", current.type == kind::text ? control_text(current.body.c_str()) : L"",
            ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL | WS_TABSTOP, 103, WS_EX_CLIENTEDGE);
        SendMessageW(description, EM_SETLIMITTEXT, 1024 * 1024, 0);
        add_control(window, L"BUTTON", current.type == kind::menu ? L"&Open" : current.type == kind::form ? L"&Apply" : L"&Close",
            WS_TABSTOP | BS_DEFPUSHBUTTON, IDOK);
        if (current.type != kind::text)
        {
            add_control(window, L"BUTTON", current.type == kind::form ? L"&Cancel" : current.main_menu ? L"&Save and close" : L"&Back",
                WS_TABSTOP | BS_PUSHBUTTON, IDCANCEL);
            add_control(window, L"BUTTON", L"&Help (F1)", WS_TABSTOP | BS_PUSHBUTTON, id_help);
            add_control(window, L"BUTTON", L"Cancel &all", WS_TABSTOP | BS_PUSHBUTTON, id_quit);
        }
        building = false;
        layout(); update_description();
        InvalidateRect(window, nullptr, TRUE);
        if (current.type == kind::menu) SetFocus(list);
        else if (current.type == kind::form && !current.inputs.empty())
        { reveal_field(current.current); SetFocus(current.inputs[current.current]); }
        else SetFocus(GetDlgItem(window, IDOK));
        UpdateWindow(window);
    }

    void save_editable_drafts()
    {
        if (!active || active->type != kind::form) return;
        for (size_t i = 0; i < active->inputs.size(); ++i)
            if ((*active->fields)[i].type == EditorField::Type::EditableChoice)
                active->drafts[i] = window_text(combo_edit(active->inputs[i]));
    }

    void apply_form()
    {
        if (!active || !active->fields) return;
        save_editable_drafts();
        auto proposal = *active->fields;
        for (size_t i = 0; i < proposal.size(); ++i)
        {
            try
            {
                proposal[i].value = narrow(active->drafts[i]);
                if (!validate_editor_field(proposal[i]))
                    throw std::runtime_error(std::string("Invalid value for ") + active->form_entries[i].label + ". Check the field help and permitted values.");
            }
            catch (const std::exception& error)
            {
                reveal_field((int)i); SetFocus(active->inputs[i]);
                MessageBoxW(window, wide(error.what()).c_str(), L"Invalid setting", MB_OK | MB_ICONWARNING);
                SetFocus(active->inputs[i]); return;
            }
        }
        *active->fields = std::move(proposal); active->action = IDOK;
    }

    LRESULT event(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
    {
        switch (message)
        {
        case WM_CLOSE: closed = true; return 0;
        case WM_SIZE: if (hwnd == window) layout(); else if (hwnd == viewport) form_layout(); return 0;
        case WM_GETMINMAXINFO:
            if (hwnd == window) reinterpret_cast<MINMAXINFO*>(lparam)->ptMinTrackSize = { px(780), px(600) };
            return 0;
        case WM_COMMAND:
        {
            if (!active || building) return 0;
            const int id = LOWORD(wparam), notification = HIWORD(wparam);
            if (id >= id_field && active->type == kind::form && id < id_field + (int)active->drafts.size())
            {
                const int index = id - id_field;
                const auto& field = (*active->fields)[index];
                const bool choice = field.is_choice();
                if (notification == (choice ? CBN_SELCHANGE : EN_CHANGE))
                {
                    if (choice)
                    {
                        const LRESULT selected = SendMessageW((HWND)lparam, CB_GETCURSEL, 0, 0);
                        const LRESULT length = SendMessageW((HWND)lparam, CB_GETLBTEXTLEN, selected, 0);
                        if (selected != CB_ERR && length != CB_ERR)
                        {
                            std::wstring value((size_t)length + 1, L'\0');
                            SendMessageW((HWND)lparam, CB_GETLBTEXT, selected, (LPARAM)value.data());
                            value.resize((size_t)length); active->drafts[index] = std::move(value);
                        }
                    }
                    else active->drafts[index] = window_text((HWND)lparam);
                }
                else if (choice && field.is_editable() && notification == CBN_EDITCHANGE)
                    active->drafts[index] = window_text((HWND)lparam);
                if (notification == (choice ? CBN_SETFOCUS : EN_SETFOCUS)) reveal_field(index);
                return 0;
            }
            if (id == id_menu)
            {
                const int selected = (int)SendMessageW(list, LB_GETCURSEL, 0, 0);
                if (selected != LB_ERR) active->current = selected;
                update_description();
                if (notification == LBN_DBLCLK) active->action = IDOK;
                return 0;
            }
            if (id == id_quit) { closed = true; return 0; }
            if (id == id_help) { active->action = help_action; return 0; }
            if (id == IDCANCEL) { active->action = MENU_SEL_EXIT; return 0; }
            if (id == IDOK)
            {
                if (active->type == kind::form) apply_form(); else active->action = IDOK;
                return 0;
            }
            break;
        }
        case WM_VSCROLL:
            if (hwnd == viewport && active)
            {
                RECT bounds; GetClientRect(viewport, &bounds);
                switch (LOWORD(wparam))
                {
                case SB_LINEUP: scroll_form(-px(38)); break;
                case SB_LINEDOWN: scroll_form(px(38)); break;
                case SB_PAGEUP: scroll_form(-bounds.bottom); break;
                case SB_PAGEDOWN: scroll_form(bounds.bottom); break;
                case SB_TOP: active->scroll = 0; form_layout(); break;
                case SB_BOTTOM: active->scroll = INT_MAX / 2; form_layout(); break;
                case SB_THUMBTRACK: case SB_THUMBPOSITION:
                { SCROLLINFO scroll = {sizeof(scroll), SIF_TRACKPOS}; GetScrollInfo(viewport, SB_VERT, &scroll); active->scroll = scroll.nTrackPos; form_layout(); break; }
                }
                return 0;
            }
            break;
        case WM_MOUSEWHEEL:
            if (active && active->type == kind::form)
            { scroll_form(-GET_WHEEL_DELTA_WPARAM(wparam) * px(76) / WHEEL_DELTA); return 0; }
            break;
        case WM_CTLCOLORSTATIC:
            if ((HWND)lparam == heading || (HWND)lparam == subtitle)
            { SetTextColor((HDC)wparam, RGB(35,48,66)); SetBkColor((HDC)wparam, RGB(245,247,250)); return (LRESULT)background; }
            break;
        case WM_ERASEBKGND:
        { RECT bounds; GetClientRect(hwnd, &bounds); FillRect((HDC)wparam, &bounds, hwnd == viewport ? (HBRUSH)(COLOR_WINDOW + 1) : background); return 1; }
        }
        return DefWindowProcW(hwnd, message, wparam, lparam);
    }

    int run_page(page& current)
    {
        current.action = pending_action; active = &current;
        try
        {
            build_page(current);
            while (!closed && !failure && current.action == pending_action)
            {
                MSG message;
                const int result = GetMessageW(&message, nullptr, 0, 0);
                if (result <= 0) { closed = true; break; }
                if (message.message == WM_KEYDOWN && (message.hwnd == window || IsChild(window, message.hwnd)))
                {
                    const bool dropdown = current.type == kind::form && current.current < (int)current.inputs.size() &&
                        (*current.fields)[current.current].is_choice() &&
                        SendMessageW(current.inputs[current.current], CB_GETDROPPEDSTATE, 0, 0);
                    if (message.wParam == VK_F1 && current.type != kind::text) { current.action = help_action; continue; }
                    if (message.wParam == VK_F2) { SendMessageW(window, WM_COMMAND, current.type == kind::menu ? IDCANCEL : IDOK, 0); continue; }
                    if (message.wParam == VK_F3) { closed = true; continue; }
                    if (message.wParam == VK_ESCAPE && !dropdown)
                    { current.action = current.type == kind::menu && current.main_menu ? MENU_SEL_QUIT : MENU_SEL_EXIT; continue; }
                    if (message.wParam == VK_RETURN && !dropdown && GetFocus() == list)
                    { current.action = IDOK; continue; }
                    if (message.wParam == VK_RETURN && !dropdown && current.type == kind::form &&
                        std::any_of(current.inputs.begin(), current.inputs.end(), [](HWND input)
                        { return input == GetFocus() || IsChild(input, GetFocus()); }))
                    { SendMessageW(window, WM_COMMAND, IDOK, 0); continue; }
                }
                if (!IsDialogMessageW(window, &message)) { TranslateMessage(&message); DispatchMessageW(&message); }
            }
            save_editable_drafts();
        }
        catch (...) { active = nullptr; destroy_controls(); throw; }
        active = nullptr; destroy_controls();
        if (failure) std::rethrow_exception(failure);
        if (closed) throw EditorClosed{};
        return current.action;
    }

public:
    explicit Win32EditorUI(HWND parent = nullptr, const std::wstring& configuration = L"es40.cfg")
        : owner(parent), machine_name(configuration.substr(configuration.find_last_of(L"\\/") + 1)) {}
    ~Win32EditorUI() override { shutdown(); }
    void initialize() override
    {
        INITCOMMONCONTROLSEX controls = {sizeof(controls), ICC_STANDARD_CLASSES}; InitCommonControlsEx(&controls);
        HDC dc = GetDC(nullptr); dpi = GetDeviceCaps(dc, LOGPIXELSX); ReleaseDC(nullptr, dc);
        font = CreateFontW(-px(14), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        title_font = CreateFontW(-px(22), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        background = CreateSolidBrush(RGB(245,247,250));
        for (const wchar_t* name : { editor_window_class, form_window_class })
        {
            WNDCLASSEXW type = {sizeof(type)}; type.lpfnWndProc = procedure; type.hInstance = GetModuleHandleW(nullptr);
            type.lpszClassName = name; type.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
            type.hIcon = LoadIconW(nullptr, MAKEINTRESOURCEW(32512));
            if (!RegisterClassExW(&type) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
                throw std::runtime_error("Unable to register the native configuration editor.");
        }
        window = CreateWindowExW(WS_EX_CONTROLPARENT, editor_window_class, (machine_name + L" - ES40 machine settings").c_str(), WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
            CW_USEDEFAULT, CW_USEDEFAULT, px(1000), px(790), owner, nullptr, GetModuleHandleW(nullptr), this);
        if (!window) throw std::runtime_error("Unable to open the native configuration editor.");
        ShowWindow(window, SW_SHOWNORMAL); SetForegroundWindow(window);
    }
    void shutdown() override
    {
        active = nullptr; destroy_controls();
        if (window) { DestroyWindow(window); window = nullptr; }
        if (font) { DeleteObject(font); font = nullptr; }
        if (title_font) { DeleteObject(title_font); title_font = nullptr; }
        if (background) { DeleteObject(background); background = nullptr; }
    }
    void banner() override {}
    void text(const char* title, const char* body) override
    {
        page current; current.type = kind::text; current.title = title ? title : "Information"; current.body = body ? body : "";
        run_page(current);
    }
    int menu(const char* title, MenuEntry_t entries[], int count, int, int) override
    {
        page current; current.type = kind::menu; current.title = title ? title : "Settings"; current.menu_entries = entries;
        current.main_menu = current.title == "Main menu";
        for (int i = 0; i < count; ++i) if (entries[i].text) current.menu_indices.push_back(i);
        if (current.menu_indices.empty()) return MENU_SEL_EXIT;
        for (;;)
        {
            const int action = run_page(current);
            if (action == MENU_SEL_EXIT || action == MENU_SEL_QUIT) return action;
            const int selected = current.menu_indices[current.current];
            if (action == help_action) { text(entries[selected].text, entries[selected].description); continue; }
            if (!entries[selected].select_callback) return selected;
            entries[selected].select_callback(entries[selected].text);
        }
    }
    bool form(const char* title, FormEntry_t entries[], std::vector<EditorField>& fields) override
    {
        page current; current.type = kind::form; current.title = title ? title : "Settings";
        current.form_entries = entries; current.fields = &fields;
        for (const auto& field : fields) current.drafts.push_back(wide(field.value));
        for (;;)
        {
            const int action = run_page(current);
            if (action == help_action)
            { if (!fields.empty()) text(entries[current.current].label, entries[current.current].description); continue; }
            return action == IDOK;
        }
    }
};

// The backend can fail before EditorUI::initialize (e.g. parsing an invalid
// input). Capture both streams so a CREATE_NO_WINDOW helper still explains it.
class Win32EditorDiagnostics
{
    FILE* file = nullptr;
    int output = -1, error = -1;
public:
    Win32EditorDiagnostics()
    {
        // MSVC tmpfile can use the drive root, which ordinary users cannot
        // write. Use the user's temporary directory and delete-on-close.
        wchar_t directory[MAX_PATH] = {}, path[MAX_PATH] = {};
        const DWORD length = GetTempPathW(MAX_PATH, directory);
        if (!length || length >= MAX_PATH || !GetTempFileNameW(directory, L"es4", 0, path)) return;
        HANDLE handle = CreateFileW(path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
        if (handle == INVALID_HANDLE_VALUE) { DeleteFileW(path); return; }
        const int descriptor = _open_osfhandle((intptr_t)handle, _O_RDWR | _O_BINARY);
        if (descriptor < 0) { CloseHandle(handle); return; }
        file = _fdopen(descriptor, "w+b");
        if (!file) { _close(descriptor); return; }
        // A CREATE_NO_WINDOW process without inherited streams starts with
        // CRT descriptor -2. Never pass that sentinel to _dup/_dup2.
        FILE* reopened = nullptr;
        if (_fileno(stdout) < 0) freopen_s(&reopened, "NUL", "w", stdout);
        if (_fileno(stderr) < 0) freopen_s(&reopened, "NUL", "w", stderr);
        fflush(stdout); fflush(stderr);
        if (_fileno(stdout) >= 0) output = _dup(_fileno(stdout));
        if (_fileno(stderr) >= 0) error = _dup(_fileno(stderr));
        if (output >= 0) _dup2(_fileno(file), _fileno(stdout));
        if (error >= 0) _dup2(_fileno(file), _fileno(stderr));
    }
    ~Win32EditorDiagnostics()
    {
        fflush(stdout); fflush(stderr);
        if (output >= 0) { _dup2(output, _fileno(stdout)); _close(output); }
        if (error >= 0) { _dup2(error, _fileno(stderr)); _close(error); }
        if (file) fclose(file);
    }
    std::string contents()
    {
        if (!file) return {};
        fflush(stdout); fflush(stderr); fflush(file);
        _fseeki64(file, 0, SEEK_END); const auto size = _ftelli64(file);
        if (size <= 0) return {};
        const size_t count = (size_t)std::min<__int64>(size, 64 * 1024);
        _fseeki64(file, size - count, SEEK_SET);
        std::string result(count, '\0'); result.resize(fread(result.data(), 1, count, file)); return result;
    }
};

int main(int, char**)
{
    HWND owner = nullptr;
    try
    {
        int argc = 0; LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        if (!argv) throw std::runtime_error("Unable to read the editor command line.");
        struct argument_guard { LPWSTR* values; ~argument_guard() { LocalFree(values); } } arguments{argv};
        std::vector<std::string> values{narrow(argv[0])};
        for (int i = 1; i < argc; ++i)
        {
            if (wcscmp(argv[i], L"--owner") == 0)
            {
                if (++i == argc) throw std::runtime_error("--owner requires a decimal window handle.");
                wchar_t* end = nullptr; errno = 0; const unsigned long long value = wcstoull(argv[i], &end, 10);
                if (errno || end == argv[i] || *end || value > (std::numeric_limits<UINT_PTR>::max)())
                    throw std::runtime_error("Invalid --owner window handle.");
                owner = reinterpret_cast<HWND>((UINT_PTR)value);
                if (!IsWindow(owner)) throw std::runtime_error("The management window is no longer available.");
            }
            else if (wcscmp(argv[i], L"--help") == 0 || wcscmp(argv[i], L"-h") == 0)
            {
                MessageBoxW(owner, L"es40-cfg-win32.exe [INPUT] [OUTPUT] [--owner DECIMAL_HWND]\n\nUse the same INPUT and OUTPUT path to edit a machine in place. With no paths, the shared editor opens es40.cfg if available or creates a new configuration. Settings categories and validation are shared with the SDL and terminal editors.", L"ES40 native settings", MB_OK | MB_ICONINFORMATION);
                return 0;
            }
            else values.push_back(narrow(argv[i]));
        }
        if (values.size() > 3) throw std::runtime_error("Usage: es40-cfg-win32.exe [INPUT] [OUTPUT] [--owner DECIMAL_HWND]");
        std::vector<char*> backend_arguments;
        for (auto& value : values) backend_arguments.push_back(value.data());
        backend_arguments.push_back(nullptr);
        int result; std::string diagnostics;
        {
            Win32EditorDiagnostics capture;
            Win32EditorUI ui(owner, values.size() > 1 ? wide(values[1]) : L"es40.cfg");
            result = run_config_editor((int)values.size(), backend_arguments.data(), ui);
            if (result) diagnostics = capture.contents();
        }
        if (result)
        {
            if (diagnostics.empty()) diagnostics = "The configuration editor could not open or save this machine. Check the input/output paths and configuration syntax.";
            MessageBoxW(owner, control_text(diagnostics.c_str()).c_str(), L"ES40 settings error", MB_OK | MB_ICONERROR);
        }
        return result;
    }
    catch (const EditorClosed&) { return 0; }
    catch (const std::exception& error)
    { MessageBoxW(owner, wide(error.what()).c_str(), L"ES40 settings error", MB_OK | MB_ICONERROR); return 1; }
}
#endif

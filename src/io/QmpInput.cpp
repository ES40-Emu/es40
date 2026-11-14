/* ES40 emulator.
 * Copyright (C) 2026 by gdwnldsKSC
 * SPDX-License-Identifier: BSD-1-Clause
 *
 * Protocol: https://www.qemu.org/docs/master/interop/qmp-spec.html
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following condition is met:
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this condition and the following disclaimer.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
#include "StdAfx.h"

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#elif (defined(__unix__) || defined(__APPLE__)) && !defined(__VMS)
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include "QmpInput.h"
#include "Keyboard.h"
#include "QmpKeymap.h"

#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif
#define PICOJSON_USE_INT64
#include "../../third_party/picojson/picojson.h"
#include <algorithm>
#include <initializer_list>

namespace {
using Json = picojson::value;
constexpr size_t message_limit = 65536;
constexpr size_t output_limit = 524288;
const char* version = R"({"qemu":{"major":0,"minor":0,"micro":0},"package":"ES40 QMP input subset 1"})";

struct QmpError {
    const char* type;
    const char* text;
};

void invalid(const char* text) { throw QmpError{"GenericError", text}; }

void fields(const Json& value, std::initializer_list<const char*> allowed)
{
    if (!value.is<picojson::object>()) invalid("Expected an object");
    for (const auto& member : value.get<picojson::object>())
        if (std::none_of(allowed.begin(), allowed.end(),
            [&](const char* name) { return member.first == name; }))
            invalid("Unexpected member");
}

const std::string& string_value(const Json& value)
{
    if (!value.is<std::string>()) invalid("Expected a string");
    return value.get<std::string>();
}

bool json_boolean(const Json& value)
{
    if (!value.is<bool>()) invalid("Expected a boolean");
    return value.get<bool>();
}

int integer(const Json& value, int minimum, int maximum)
{
    if (!value.is<int64_t>() || value.get<int64_t>() < minimum
        || value.get<int64_t>() > maximum)
        invalid("Integer outside the supported range");
    return (int)value.get<int64_t>();
}

bool whitespace(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }
bool digit(char c) { return c >= '0' && c <= '9'; }

u64 input_batch_id(const Json& value)
{
    const std::string& text = string_value(value);
    if (text.empty() || text.size() > 20 || text[0] == '0') invalid("Invalid input batch ID");
    u64 id = 0;
    for (char c : text) {
        if (!digit(c) || id > (~u64(0) - (c - '0')) / 10) invalid("Invalid input batch ID");
        id = id * 10 + (c - '0');
    }
    return id;
}

void unicode_escape(std::string& out, unsigned cp)
{
    if (cp > 0xffff) {
        unicode_escape(out, 0xd800 + ((cp - 0x10000) >> 10));
        unicode_escape(out, 0xdc00 + ((cp - 0x10000) & 0x3ff));
        return;
    }
    char escaped[7];
    std::snprintf(escaped, sizeof escaped, "\\u%04x", cp);
    out += escaped;
}

// Accept QMP quoting and normalize UTF-8 to ASCII without changing numeric IDs.
std::string normalize(const std::string& input)
{
    std::string out;
    char quote = 0;
    for (size_t i = 0; i < input.size(); ++i) {
        unsigned char c = (unsigned char)input[i];
        if (quote) {
            if (c == '\\') {
                if (++i == input.size()) invalid("Invalid JSON escape");
                if (input[i] == '\'') out += '\'';
                else { out += '\\'; out += input[i]; }
            } else if (c == (unsigned char)quote) {
                quote = 0;
                out += '"';
            } else if (c == '"') {
                out += "\\\"";
            } else if (c < 0x20) {
                invalid("Invalid JSON string");
            } else if (c < 0x80) {
                out += (char)c;
            } else {
                unsigned cp, minimum, remaining;
                if (c >= 0xc2 && c <= 0xdf) { cp = c & 0x1f; minimum = 0x80; remaining = 1; }
                else if (c >= 0xe0 && c <= 0xef) { cp = c & 0x0f; minimum = 0x800; remaining = 2; }
                else if (c >= 0xf0 && c <= 0xf4) { cp = c & 0x07; minimum = 0x10000; remaining = 3; }
                else { invalid("Invalid UTF-8"); return {}; }
                while (remaining--) {
                    if (++i == input.size() || ((unsigned char)input[i] & 0xc0) != 0x80)
                        invalid("Invalid UTF-8");
                    cp = (cp << 6) | ((unsigned char)input[i] & 0x3f);
                }
                if (cp < minimum || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff))
                    invalid("Invalid UTF-8");
                unicode_escape(out, cp);
            }
        } else if (c == '"' || c == '\'') {
            quote = (char)c;
            out += '"';
        } else if (c == '-' || digit((char)c)) {
            const size_t begin = i;
            if (c == '-') ++i;
            if (i == input.size() || !digit(input[i])) invalid("Invalid JSON number");
            if (input[i] == '0') ++i;
            else while (i < input.size() && digit(input[i])) ++i;
            if (i < input.size() && input[i] == '.') {
                ++i;
                if (i == input.size() || !digit(input[i])) invalid("Invalid JSON number");
                while (i < input.size() && digit(input[i])) ++i;
            }
            if (i < input.size() && (input[i] == 'e' || input[i] == 'E')) {
                ++i;
                if (i < input.size() && (input[i] == '+' || input[i] == '-')) ++i;
                if (i == input.size() || !digit(input[i])) invalid("Invalid JSON number");
                while (i < input.size() && digit(input[i])) ++i;
            }
            if (i < input.size() && !whitespace(input[i]) && input[i] != ','
                && input[i] != '}' && input[i] != ']') invalid("Invalid JSON number");
            out.append(input, begin, i - begin);
            --i;
        } else {
            if (c >= 0x80) invalid("Invalid JSON token");
            out += (char)c;
        }
    }
    if (quote) invalid("Incomplete JSON string");
    return out;
}

size_t value_end(const std::string& text, size_t pos)
{
    unsigned depth = 0;
    bool quoted = false, escape = false;
    for (; pos < text.size(); ++pos) {
        const char c = text[pos];
        if (quoted) {
            if (escape) escape = false;
            else if (c == '\\') escape = true;
            else if (c == '"') { quoted = false; if (!depth) return pos + 1; }
        } else if (c == '"') quoted = true;
        else if (c == '{' || c == '[') ++depth;
        else if (c == '}' || c == ']') {
            if (!depth) return pos;
            if (!--depth) return pos + 1;
        } else if (!depth && (c == ',' || whitespace(c))) return pos;
    }
    return pos;
}

std::string request_id(const std::string& text)
{
    size_t pos = 1;
    while (pos < text.size()) {
        while (whitespace(text[pos])) ++pos;
        if (text[pos] == '}') break;
        const size_t end = value_end(text, pos);
        Json key;
        picojson::parse(key, text.substr(pos, end - pos));
        pos = end;
        while (whitespace(text[pos])) ++pos;
        ++pos;
        while (whitespace(text[pos])) ++pos;
        const size_t begin = pos;
        pos = value_end(text, pos);
        if (key.get<std::string>() == "id") return text.substr(begin, pos - begin);
        while (whitespace(text[pos])) ++pos;
        if (text[pos] != ',') break;
        ++pos;
    }
    return {};
}

#if defined(_WIN32)
using Socket = SOCKET;
constexpr Socket no_socket = INVALID_SOCKET;
int socket_error() { return WSAGetLastError(); }
bool would_block() { const int e = socket_error(); return e == WSAEWOULDBLOCK || e == WSAEINTR; }
void close_socket(Socket s) { if (s != no_socket) closesocket(s); }
bool nonblocking(Socket s) { u_long one = 1; return ioctlsocket(s, FIONBIO, &one) == 0; }
#define ES40_QMP_SOCKETS
#elif (defined(__unix__) || defined(__APPLE__)) && !defined(__VMS)
using Socket = int;
constexpr Socket no_socket = -1;
int socket_error() { return errno; }
bool would_block() { return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR; }
void close_socket(Socket s) { if (s != no_socket) close(s); }
bool nonblocking(Socket s)
{
    const int flags = fcntl(s, F_GETFL);
    if (flags < 0 || fcntl(s, F_SETFL, flags | O_NONBLOCK) < 0
        || fcntl(s, F_SETFD, FD_CLOEXEC) < 0) return false;
#ifdef SO_NOSIGPIPE
    int one = 1;
    if (setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one) < 0) return false;
#endif
    return true;
}
#define ES40_QMP_SOCKETS
#endif
}

struct QmpInput::Impl {
    CKeyboard& keyboard;
    unsigned port = 0;
    bool negotiated = false;
    std::string pending, frame, nesting, output;
    char quote = 0;
    bool escape = false;
#ifdef ES40_QMP_SOCKETS
    Socket listener = no_socket, client = no_socket;
#endif
#ifdef _WIN32
    bool winsock = false;
#endif

    Impl(CKeyboard& keyboard, const char* setting) : keyboard(keyboard)
    {
        if (!setting || !*setting) FAILURE(Configuration, "Invalid qmp.port");
        for (const char* p = setting; *p; ++p) {
            if (!digit(*p) || port > 6553) FAILURE(Configuration, "Invalid qmp.port");
            port = port * 10 + (*p - '0');
        }
        if (port > 65535) FAILURE(Configuration, "qmp.port must be 0..65535");
    }

    void disconnect()
    {
#ifdef ES40_QMP_SOCKETS
        if (client != no_socket) {
            close_socket(client);
            client = no_socket;
            keyboard.release_input();
        }
#endif
        pending.clear(); frame.clear(); nesting.clear(); output.clear();
        quote = 0; escape = false; negotiated = false;
    }

    void reply(const std::string& body, const std::string& id = {})
    {
        const std::string message = "{" + body + (id.empty() ? "" : ",\"id\":" + id) + "}\r\n";
        if (message.size() > output_limit - output.size()) { disconnect(); return; }
        output += message;
    }

    void error(const char* type, const char* text, const std::string& id = {})
    {
        reply(std::string("\"error\":{\"class\":\"") + type + "\",\"desc\":\"" + text + "\"}", id);
    }

    std::string input(const Json& args, bool tracked = false)
    {
        fields(args, {"events"});
        const Json& events = args.get("events");
        if (!events.is<picojson::array>()) invalid("Expected an events array");
        if (events.get<picojson::array>().size() > CKeyboard::INPUT_CAPACITY)
            invalid("Too many input events");
        std::vector<CKeyboard::InputEvent> batch;
        unsigned buttons = keyboard.input_buttons();
        for (const Json& item : events.get<picojson::array>()) {
            fields(item, {"type", "data"});
            const std::string& type = string_value(item.get("type"));
            const Json& data = item.get("data");
            CKeyboard::InputEvent event;
            if (type == "key") {
                fields(data, {"down", "key"});
                const bool down = json_boolean(data.get("down"));
                const Json& key = data.get("key");
                fields(key, {"type", "data"});
                const std::string& kind = string_value(key.get("type"));
                u32 code = 0;
                bool mapped = false;
                if (kind == "qcode") mapped = qmp_key_code(string_value(key.get("data")), code);
                else if (kind == "number") mapped = qmp_key_number((unsigned)integer(key.get("data"), 0, 255), code);
                if (!mapped) invalid("Unsupported key");
                event.key = code | (down ? 0 : BX_KEY_RELEASED);
            } else {
                event.type = CKeyboard::InputEvent::Type::Mouse;
                if (type == "rel") {
                    fields(data, {"axis", "value"});
                    const std::string& axis = string_value(data.get("axis"));
                    const int value = integer(data.get("value"), -32767, 32767);
                    if (axis == "x") event.dx = value;
                    else if (axis == "y") event.dy = -value;
                    else invalid("Unsupported relative axis");
                } else if (type == "btn") {
                    fields(data, {"down", "button"});
                    const bool down = json_boolean(data.get("down"));
                    const std::string& name = string_value(data.get("button"));
                    unsigned mask = 0;
                    if (name == "left") mask = 1;
                    else if (name == "right") mask = 2;
                    else if (name == "middle") mask = 4;
                    else if (name == "wheel-up" || name == "wheel-down") {
                        if (!down) continue;
                        event.dz = name == "wheel-up" ? 1 : -1;
                    } else invalid("Unsupported mouse button");
                    if (down) buttons |= mask;
                    else buttons &= ~mask;
                } else invalid("Unsupported input event type");
                event.buttons = buttons;
            }
            if (event.type == CKeyboard::InputEvent::Type::Mouse && !batch.empty()) {
                auto& previous = batch.back();
                if (previous.type == event.type && previous.buttons == event.buttons
                    && std::abs(previous.dx + event.dx) <= 32767
                    && std::abs(previous.dy + event.dy) <= 32767
                    && std::abs(previous.dz + event.dz) <= 32767) {
                    previous.dx += event.dx; previous.dy += event.dy; previous.dz += event.dz;
                    continue;
                }
            }
            batch.push_back(event);
        }
        u64 batch_id = 0;
        switch (keyboard.queue_input(batch.data(), batch.size(), tracked ? &batch_id : nullptr)) {
        case CKeyboard::InputResult::Accepted:
            return tracked ? "{\"__es40-batch\":\"" + std::to_string(batch_id) + "\"}" : "{}";
        case CKeyboard::InputResult::Full:
            invalid(tracked ? "Input queue or completion history is full" : "Input queue is full"); break;
        case CKeyboard::InputResult::Unavailable: invalid("Input device or connection cleanup is not ready"); break;
        default: invalid("Unsupported input"); break;
        }
        return "{}";
    }

    std::string input_status(const Json& args)
    {
        fields(args, {"__es40-batch"});
        CKeyboard::InputStatus status;
        if (!keyboard.input_status(input_batch_id(args.get("__es40-batch")), status))
            invalid("Unknown or expired input batch ID");
        const char* state = status.pending ? "pending" : status.cancelled ? "cancelled" : "delivered";
        return "{\"__es40-batch\":\"" + std::to_string(status.id)
            + "\",\"__es40-state\":\"" + state
            + "\",\"__es40-total\":" + std::to_string(status.pending + status.delivered + status.cancelled)
            + ",\"__es40-pending\":" + std::to_string(status.pending)
            + ",\"__es40-delivered\":" + std::to_string(status.delivered)
            + ",\"__es40-cancelled\":" + std::to_string(status.cancelled) + "}";
    }

    void request(const std::string& raw)
    {
        std::string id;
        try {
            const std::string text = normalize(raw);
            Json root;
            std::string parse_error;
            const auto end = picojson::parse(root, text.begin(), text.end(), &parse_error);
            if (!parse_error.empty() || end != text.end() || !root.is<picojson::object>())
                invalid("Invalid JSON request");
            id = request_id(text);
            fields(root, {"execute", "arguments", "id"});
            const std::string& command = string_value(root.get("execute"));
            const Json empty{picojson::object{}};
            const Json& args = root.contains("arguments") ? root.get("arguments") : empty;
            if (!args.is<picojson::object>()) invalid("Expected arguments object");
            if (command == "qmp_capabilities" && !negotiated) {
                fields(args, {"enable"});
                if (args.contains("enable") && (!args.get("enable").is<picojson::array>()
                    || !args.get("enable").get<picojson::array>().empty()))
                    invalid("Unsupported capability");
                negotiated = true;
                reply("\"return\":{}", id);
            } else if (!negotiated || command == "qmp_capabilities") {
                error("CommandNotFound", "Command is not available in this negotiation state", id);
            } else if (command == "input-send-event") {
                reply("\"return\":" + input(args), id);
            } else if (command == "__es40-input-send-event") {
                reply("\"return\":" + input(args, true), id);
            } else if (command == "__es40-query-input") {
                reply("\"return\":" + input_status(args), id);
            } else if (command == "query-version") {
                fields(args, {});
                reply(std::string("\"return\":") + version, id);
            } else if (command == "query-mice") {
                fields(args, {});
                reply(R"("return":[{"name":"ES40 PS/2 mouse","index":0,"current":true,"absolute":false}])", id);
            } else if (command == "query-commands") {
                fields(args, {});
                reply(R"("return":[{"name":"qmp_capabilities"},{"name":"query-version"},{"name":"query-commands"},{"name":"query-mice"},{"name":"input-send-event"},{"name":"__es40-input-send-event"},{"name":"__es40-query-input"}])", id);
            } else error("CommandNotFound", "Command is not supported", id);
        } catch (const QmpError& e) {
            error(e.type, e.text, id);
        } catch (const std::exception&) {
            error("GenericError", "Invalid JSON request", id);
        }
    }

    bool consume(char c)
    {
        if (frame.empty() && whitespace(c)) return false;
        if ((unsigned char)c < 0x20 && (!whitespace(c) || quote)) {
            frame.clear(); nesting.clear(); quote = 0; escape = false;
            error("GenericError", "Invalid JSON character");
            return true;
        }
        if (frame.empty() && c != '{') {
            error("GenericError", "Expected a JSON object");
            return true;
        }
        frame += c;
        if (frame.size() > message_limit) { disconnect(); return true; }
        if (quote) {
            if (escape) escape = false;
            else if (c == '\\') escape = true;
            else if (c == quote) quote = 0;
        } else if (c == '"' || c == '\'') quote = c;
        else if (c == '{' || c == '[') {
            nesting += c;
            if (nesting.size() > 32) { disconnect(); return true; }
        } else if (c == '}' || c == ']') {
            if (nesting.empty() || (c == '}' ? nesting.back() != '{' : nesting.back() != '[')) {
                frame.clear(); nesting.clear();
                error("GenericError", "Mismatched JSON delimiters");
                return true;
            }
            nesting.pop_back();
            if (nesting.empty()) {
                std::string complete;
                complete.swap(frame);
                request(complete);
                return true;
            }
        }
        return false;
    }

#ifdef ES40_QMP_SOCKETS
    void flush()
    {
        if (client == no_socket || output.empty()) return;
        int flags = 0;
#ifdef MSG_NOSIGNAL
        flags = MSG_NOSIGNAL;
#endif
        const int n = (int)send(client, output.data(), (int)std::min<size_t>(output.size(), 16384), flags);
        if (n > 0) output.erase(0, (size_t)n);
        else if (n == 0 || !would_block()) disconnect();
    }
#endif
};

QmpInput::QmpInput(CKeyboard& keyboard, const char* port) : impl(new Impl(keyboard, port)) {}
QmpInput::~QmpInput() { stop(); }

void QmpInput::start()
{
    if (!impl->port) return;
#ifdef ES40_QMP_SOCKETS
    if (impl->listener != no_socket) return;
#ifdef _WIN32
    WSADATA wsa;
    const int result = WSAStartup(MAKEWORD(2, 2), &wsa);
    if (result) FAILURE(Configuration, "QMP Winsock startup failed");
    impl->winsock = true;
#endif
    impl->listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    bool ready = impl->listener != no_socket && nonblocking(impl->listener);
    int one = 1;
#ifdef _WIN32
    if (ready) ready = setsockopt(impl->listener, SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
        (const char*)&one, sizeof one) == 0;
#else
    if (ready) ready = setsockopt(impl->listener, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one) == 0;
#endif
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons((unsigned short)impl->port);
    if (ready) ready = bind(impl->listener, (sockaddr*)&address, sizeof address) == 0;
    if (ready) ready = listen(impl->listener, 4) == 0;
    if (!ready) {
        const int code = socket_error();
        stop();
        FAILURE(Configuration, "QMP listener failed (socket error " + std::to_string(code) + ")");
    }
    printf("qmp: listening on 127.0.0.1:%u\n", impl->port);
#else
    FAILURE(Configuration, "QMP sockets are not supported on this host");
#endif
}

void QmpInput::stop()
{
    impl->disconnect();
#ifdef ES40_QMP_SOCKETS
    close_socket(impl->listener);
    impl->listener = no_socket;
#endif
#ifdef _WIN32
    if (impl->winsock) { WSACleanup(); impl->winsock = false; }
#endif
}

void QmpInput::poll()
{
#ifdef ES40_QMP_SOCKETS
    if (impl->listener == no_socket) return;
    for (unsigned i = 0; i < 8; ++i) {
        Socket socket = accept(impl->listener, nullptr, nullptr);
        if (socket == no_socket) break;
        if (impl->client != no_socket || !nonblocking(socket)) { close_socket(socket); continue; }
        int one = 1;
        setsockopt(socket, IPPROTO_TCP, TCP_NODELAY, (const char*)&one, sizeof one);
        impl->client = socket;
        impl->reply(std::string("\"QMP\":{\"version\":") + version + ",\"capabilities\":[]}");
    }
    impl->flush();
    if (impl->client == no_socket) return;
    if (impl->pending.size() < 4096 && impl->output.size() < output_limit / 2) {
        char buffer[4096];
        const int n = (int)recv(impl->client, buffer, sizeof buffer, 0);
        if (n > 0) impl->pending.append(buffer, (size_t)n);
        else if (n == 0 || !would_block()) { impl->disconnect(); return; }
    }
    size_t consumed = 0;
    unsigned messages = 0;
    while (consumed < impl->pending.size() && messages < 32
        && impl->client != no_socket && impl->output.size() < output_limit / 2) {
        const char c = impl->pending[consumed++];
        if (impl->consume(c)) ++messages;
    }
    if (impl->client != no_socket) impl->pending.erase(0, consumed);
    impl->flush();
#endif
}


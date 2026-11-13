/* ES40 emulator.
 * Copyright (C) 2026 by Antoni Sawicki
 * All rights reserved.
 *
 * WWW    : https://github.com/ES40-Emu/es40
 *
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

/**
 * \file
 * MCP server (see McpServer.h): the Unix socket, the registry entry, the
 * JSON-RPC messages and the frames. The tools are in McpTools.cpp.
 *
 * The socket takes one client at a time; the kernel holds the next one
 * until that client leaves. Each tools/call runs on a thread of its own, so
 * a call that waits for the screen does not hold up the others. Cancelling
 * a call, or leaving, ends it and lets go of the keys and buttons it held.
 **/

#include "StdAfx.h"
#include "McpServer.h"
#include "McpInternal.h"
#include "config/Configurator.h"

#include <cerrno>
#include <climits>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <fcntl.h>
#include <map>
#include <mutex>
#include <poll.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <system_error>
#include <thread>
#include <unistd.h>
#include <vector>

#if !defined(PACKAGE_VERSION)
#define PACKAGE_VERSION "unknown"
#endif

using es40mcp::Json;

std::atomic<bool> es40_mcp_tap{ false };

namespace
{

struct Settings
{
  bool enabled = false;
  std::string name;
  std::string registry;
  std::string socket;
};

Settings g_settings;
es40mcp::VmInfo g_vm;
std::string g_json_path;

int g_listen = -1;
int g_wake[2] = { -1, -1 };
std::thread g_server;
std::atomic<bool> g_stopping{ false };
bool g_running = false;

std::mutex g_calls_mutex;
std::condition_variable g_calls_cv;
int g_calls = 0;

// The frames, one console per display output, in the order they first showed one
struct Console
{
  const void* output = nullptr;
  es40mcp::Image img;
  uint64_t seq = 0;
};

std::mutex g_frame_mutex;
std::condition_variable g_frame_cv;
std::vector<Console> g_consoles;

bool truthy(const char* s)
{
  return s && (!strcmp(s, "1") || !strcasecmp(s, "true") || !strcasecmp(s, "yes") ||
    !strcasecmp(s, "on"));
}

Json response(const Json& id, Json result)
{
  Json m = Json::object();
  m.set("jsonrpc", "2.0");
  m.set("id", id);
  m.set("result", std::move(result));
  return m;
}

Json error_response(const Json& id, int code, const std::string& message)
{
  Json e = Json::object();
  e.set("code", code);
  e.set("message", message);

  Json m = Json::object();
  m.set("jsonrpc", "2.0");
  m.set("id", id);
  m.set("error", std::move(e));
  return m;
}

} // namespace

namespace es40mcp
{

class Connection
{
public:
  explicit Connection(int fd) : m_fd(fd) {}

  /// Sends one message as one line; false once the client is gone
  bool send(const Json& msg)
  {
    std::string line = msg.dump();
    line += '\n';

    // m_write keeps messages whole; close() waits for it, so the fd stays open
    std::lock_guard<std::mutex> lock(m_write);
    int fd;
    {
      std::lock_guard<std::mutex> state(m_state);
      if (m_closed)
        return false;
      fd = m_fd;
    }
    const char* p = line.data();
    size_t left = line.size();
    while (left)
    {
#if defined(MSG_NOSIGNAL)
      ssize_t n = ::send(fd, p, left, MSG_NOSIGNAL);
#else
      ssize_t n = ::send(fd, p, left, 0);
#endif
      if (n < 0)
      {
        if (errno == EINTR)
          continue;
        shutdown();
        return false;
      }
      p += n;
      left -= (size_t)n;
    }
    return true;
  }

  /// Ends the connection without waiting for a send: a send blocked on a
  /// client that does not read fails at once. Any thread may call it.
  void shutdown()
  {
    std::lock_guard<std::mutex> state(m_state);
    if (!m_closed && m_fd >= 0)
      ::shutdown(m_fd, SHUT_RDWR);
    m_closed = true;
  }

  /// Only the server thread, which reads the socket, closes it
  void close()
  {
    shutdown();
    std::lock_guard<std::mutex> lock(m_write);  // a send in progress has failed by now
    std::lock_guard<std::mutex> state(m_state);
    if (m_fd >= 0)
    {
      ::close(m_fd);
      m_fd = -1;
    }
  }

  int fd() const { return m_fd; }

  /// false if a call with this id is still running: ids must be unique
  bool add_call(const std::string& key, std::shared_ptr<std::atomic<bool>> flag)
  {
    std::lock_guard<std::mutex> lock(m_calls_mutex);
    return m_calls.emplace(key, std::move(flag)).second;
  }

  void remove_call(const std::string& key)
  {
    std::lock_guard<std::mutex> lock(m_calls_mutex);
    m_calls.erase(key);
  }

  void cancel_call(const std::string& key)
  {
    std::lock_guard<std::mutex> lock(m_calls_mutex);
    auto it = m_calls.find(key);
    if (it != m_calls.end())
      it->second->store(true);
  }

  void cancel_all()
  {
    std::lock_guard<std::mutex> lock(m_calls_mutex);
    for (auto& c : m_calls)
      c.second->store(true);
  }

private:
  std::mutex m_write;  // held for a whole message
  std::mutex m_state;  // guards m_fd and m_closed; never held while blocked
  int m_fd;
  bool m_closed = false;
  std::mutex m_calls_mutex;
  std::map<std::string, std::shared_ptr<std::atomic<bool>>> m_calls;
};

Call::Call(std::shared_ptr<Connection> conn, Json progress_token,
  std::shared_ptr<std::atomic<bool>> cancelled)
  : m_conn(std::move(conn)), m_progress_token(std::move(progress_token)),
  m_cancelled(std::move(cancelled)), m_start(std::chrono::steady_clock::now()),
  m_last_progress(m_start)
{
}

bool Call::sleep(int ms)
{
  auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);

  while (!cancelled())
  {
    auto left = until - std::chrono::steady_clock::now();
    if (left <= std::chrono::steady_clock::duration::zero())
      return true;
    std::this_thread::sleep_for(std::min<std::chrono::steady_clock::duration>(
      left, std::chrono::milliseconds(20)));
  }
  return false;
}

double Call::elapsed() const
{
  return std::chrono::duration<double>(std::chrono::steady_clock::now() - m_start).count();
}

void Call::progress(const std::string& message)
{
  auto now = std::chrono::steady_clock::now();

  if (m_progress_token.is_null() || now - m_last_progress < std::chrono::seconds(5))
    return;
  m_last_progress = now;

  Json p = Json::object();
  p.set("progressToken", m_progress_token);
  p.set("progress", (double)(long)elapsed());
  p.set("message", message);

  Json m = Json::object();
  m.set("jsonrpc", "2.0");
  m.set("method", "notifications/progress");
  m.set("params", std::move(p));
  m_conn->send(m);
}

Json ToolResult::to_json() const
{
  Json content = Json::array();
  Json t = Json::object();

  t.set("type", "text");
  t.set("text", text.empty() ? std::string(error ? "failed" : "done") : text);
  content.push(std::move(t));
  if (!png.empty())
  {
    Json img = Json::object();
    img.set("type", "image");
    img.set("data", base64(png));
    img.set("mimeType", "image/png");
    content.push(std::move(img));
  }

  Json r = Json::object();
  r.set("content", std::move(content));
  if (error)
    r.set("isError", true);
  return r;
}

bool latest_frame(unsigned console, Frame& out)
{
  std::lock_guard<std::mutex> lock(g_frame_mutex);

  if (console >= g_consoles.size() || g_consoles[console].img.empty())
    return false;
  out.img = g_consoles[console].img;
  out.seq = g_consoles[console].seq;
  return true;
}

bool wait_frame(unsigned console, uint64_t seq, int ms)
{
  std::unique_lock<std::mutex> lock(g_frame_mutex);

  return g_frame_cv.wait_for(lock, std::chrono::milliseconds(ms), [&] {
    return console < g_consoles.size() && g_consoles[console].seq > seq;
  });
}

unsigned console_count()
{
  std::lock_guard<std::mutex> lock(g_frame_mutex);
  return (unsigned)g_consoles.size();
}

const VmInfo& vm_info()
{
  return g_vm;
}

} // namespace es40mcp

void es40_mcp_frame(const void* output, const uint32_t* pixels, unsigned width, unsigned height)
{
  const size_t n = (size_t)width * height;
  Console* c = nullptr;

  if (!pixels || !n)
    return;

  std::lock_guard<std::mutex> lock(g_frame_mutex);
  for (auto& con : g_consoles)
  {
    if (con.output == output)
    {
      c = &con;
      break;
    }
  }
  if (!c)
  {
    g_consoles.emplace_back();
    c = &g_consoles.back();
    c->output = output;
  }
  if (c->img.w == width && c->img.h == height && c->img.px.size() == n &&
    !memcmp(c->img.px.data(), pixels, n * sizeof(uint32_t)))
    return;  // the same picture again
  c->img.w = width;
  c->img.h = height;
  c->img.px.assign(pixels, pixels + n);
  c->seq++;
  g_frame_cv.notify_all();
}

namespace
{

using es40mcp::Connection;

// The client being served, so es40_mcp_stop() can end it
std::mutex g_conn_mutex;
std::shared_ptr<Connection> g_conn;

const char* const g_versions[] = { "2025-11-25", "2025-06-18", "2025-03-26", "2024-11-05", nullptr };

Json initialize_result(const Json* params)
{
  const Json* asked = params ? params->find("protocolVersion") : nullptr;
  const char* version = g_versions[0];

  for (int i = 0; asked && g_versions[i]; i++)
  {
    if (asked->as_string() == g_versions[i])
      version = g_versions[i];
  }

  Json tools = Json::object();
  tools.set("listChanged", false);
  Json caps = Json::object();
  caps.set("tools", std::move(tools));
  Json info = Json::object();
  info.set("name", "es40");
  info.set("version", PACKAGE_VERSION);

  Json r = Json::object();
  r.set("protocolVersion", version);
  r.set("capabilities", std::move(caps));
  r.set("serverInfo", std::move(info));
  r.set("instructions", es40mcp::server_instructions);
  return r;
}

void count_call(int n)
{
  {
    std::lock_guard<std::mutex> lock(g_calls_mutex);
    g_calls += n;
  }
  g_calls_cv.notify_all();
}

/// One tools/call, on a thread of its own
void run_call(std::shared_ptr<Connection> conn, Json id, std::string key, std::string tool,
  Json arguments, Json progress_token, std::shared_ptr<std::atomic<bool>> flag)
{
  es40mcp::ToolResult r;

  try
  {
    es40mcp::Call call(conn, progress_token, flag);
    r = es40mcp::call_tool(call, tool, arguments);
  }
  catch (const std::exception& e)
  {
    r = es40mcp::ToolResult();
    r.error = true;
    r.text = tool + " failed: " + e.what();
  }
  catch (...)
  {
    r = es40mcp::ToolResult();
    r.error = true;
    r.text = tool + " failed inside es40";
  }
  if (!flag->load())  // a cancelled call gets no answer
  {
    // the buttons still down, for es40-mcp: it keeps the connection while
    // one is, because es40 lets go of them when the client leaves
    Json result = r.to_json();
    Json meta = Json::object();
    meta.set("es40/buttons", es40mcp::held_buttons());
    result.set("_meta", std::move(meta));
    conn->send(response(id, std::move(result)));
  }
  conn->remove_call(key);
  count_call(-1);
}

void start_call(const std::shared_ptr<Connection>& conn, const Json& id, const Json* params)
{
  const Json* name = params ? params->find("name") : nullptr;
  const Json* args = params ? params->find("arguments") : nullptr;
  const Json* meta = params ? params->find("_meta") : nullptr;
  const Json* token = meta ? meta->find("progressToken") : nullptr;

  if (!name || !name->is_string())
  {
    conn->send(error_response(id, -32602, "tools/call needs a tool name"));
    return;
  }

  auto flag = std::make_shared<std::atomic<bool>>(false);
  const std::string key = id.dump();

  if (!conn->add_call(key, flag))
  {
    conn->send(error_response(id, -32600, "request id " + key + " is still in use by a running call"));
    return;
  }
  count_call(1);
  try
  {
    std::thread(run_call, conn, id, key, name->as_string(),
      (args && args->is_object()) ? *args : Json::object(), token ? *token : Json(), flag).detach();
  }
  catch (const std::system_error& e)
  {
    // no thread for it (too many threads): the call never started
    conn->remove_call(key);
    count_call(-1);
    conn->send(error_response(id, -32603, std::string("es40 cannot start the call: ") + e.what()));
  }
}

void handle_line(const std::shared_ptr<Connection>& conn, const std::string& line)
{
  Json msg;
  std::string err;

  if (line.find_first_not_of(" \t\r") == std::string::npos)
    return;
  if (!Json::parse(line, msg, err) || !msg.is_object())
  {
    conn->send(error_response(Json(), -32700, "not a JSON object: " + err));
    return;
  }

  const Json* method = msg.find("method");
  const Json* id = msg.find("id");
  const Json* params = msg.find("params");

  if (!method || !method->is_string())
    return;  // an answer; es40 never asks the client anything
  const std::string& m = method->as_string();

  if (!id)
  {
    // notifications
    if (m == "notifications/cancelled" && params)
    {
      const Json* req = params->find("requestId");
      if (req)
        conn->cancel_call(req->dump());
    }
    return;
  }

  if (m == "initialize")
  {
    conn->send(response(*id, initialize_result(params)));
  }
  else if (m == "ping")
  {
    conn->send(response(*id, Json::object()));
  }
  else if (m == "tools/list")
  {
    Json r = Json::object();
    r.set("tools", es40mcp::tool_list());
    conn->send(response(*id, std::move(r)));
  }
  else if (m == "tools/call")
  {
    start_call(conn, *id, params);
  }
  else
  {
    conn->send(error_response(*id, -32601, "es40 has no method " + m));
  }
}

void serve(const std::shared_ptr<Connection>& conn)
{
  std::string in;
  char buf[65536];

  while (!g_stopping.load())
  {
    struct pollfd fds[2] = { { conn->fd(), POLLIN, 0 }, { g_wake[0], POLLIN, 0 } };
    int n = poll(fds, 2, -1);

    if (n < 0)
    {
      if (errno == EINTR)
        continue;
      break;
    }
    if (fds[1].revents)
      break;  // es40 is stopping
    if (!(fds[0].revents & (POLLIN | POLLHUP | POLLERR)))
      continue;

    ssize_t r = read(conn->fd(), buf, sizeof(buf));
    if (r < 0 && (errno == EINTR || errno == EAGAIN))
      continue;
    if (r <= 0)
      break;
    in.append(buf, (size_t)r);

    size_t nl;
    while ((nl = in.find('\n')) != std::string::npos)
    {
      std::string line = in.substr(0, nl);
      in.erase(0, nl + 1);
      handle_line(conn, line);
    }
    if (in.size() > 64 * 1024 * 1024)
      break;
  }
  conn->cancel_all();
  conn->close();
  es40mcp::release_input();
}

void server_main()
{
  while (!g_stopping.load())
  {
    struct pollfd fds[2] = { { g_listen, POLLIN, 0 }, { g_wake[0], POLLIN, 0 } };
    int n = poll(fds, 2, -1);

    if (n < 0)
    {
      if (errno == EINTR)
        continue;
      break;
    }
    if (fds[1].revents)
      break;
    if (!(fds[0].revents & POLLIN))
      continue;

    int fd = accept(g_listen, nullptr, nullptr);
    if (fd < 0)
      continue;
    fcntl(fd, F_SETFD, FD_CLOEXEC);
#if defined(SO_NOSIGPIPE)
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#endif
    auto conn = std::make_shared<Connection>(fd);
    {
      std::lock_guard<std::mutex> lock(g_conn_mutex);
      g_conn = conn;
    }
    serve(conn);
    {
      std::lock_guard<std::mutex> lock(g_conn_mutex);
      g_conn.reset();
    }
  }
}

// The registry: <dir>/<pid>.json says what this VM is and where its socket is

bool make_private_dir(const std::string& dir, std::string& err)
{
  struct stat st;

  if (mkdir(dir.c_str(), 0700) < 0 && errno != EEXIST)
  {
    err = "cannot create " + dir + ": " + strerror(errno);
    return false;
  }
  if (lstat(dir.c_str(), &st) < 0)
  {
    err = "cannot check " + dir + ": " + strerror(errno);
    return false;
  }
  if (!S_ISDIR(st.st_mode))
  {
    err = dir + " is not a directory";
    return false;
  }
  if (st.st_uid != getuid())
  {
    err = dir + " belongs to another user";
    return false;
  }
  if (st.st_mode & 077)
  {
    err = dir + " is open to other users; it must be mode 0700";
    return false;
  }
  return true;
}

/// Removes the files of VMs that ended without cleaning up
void drop_stale(const std::string& dir)
{
  DIR* d = opendir(dir.c_str());
  struct dirent* e;

  if (!d)
    return;
  while ((e = readdir(d)))
  {
    char* end;
    long pid = strtol(e->d_name, &end, 10);

    if (end == e->d_name || pid <= 0 || pid == (long)getpid() ||
      (strcmp(end, ".json") && strcmp(end, ".sock")))
      continue;
    if (kill((pid_t)pid, 0) == 0 || errno != ESRCH)
      continue;
    unlink((dir + "/" + e->d_name).c_str());
  }
  closedir(d);
}

std::string binary_path(const char* argv0)
{
  std::string a = argv0 ? argv0 : "es40";
  char buf[PATH_MAX];

  if (a.find('/') == std::string::npos)
  {
    const char* path = getenv("PATH");
    std::string p = path ? path : "";
    size_t start = 0;

    while (start <= p.size())
    {
      size_t colon = p.find(':', start);
      std::string dir = p.substr(start, colon == std::string::npos ? std::string::npos : colon - start);
      std::string candidate = (dir.empty() ? std::string(".") : dir) + "/" + a;

      if (access(candidate.c_str(), X_OK) == 0)
      {
        a = candidate;
        break;
      }
      if (colon == std::string::npos)
        break;
      start = colon + 1;
    }
  }
  if (realpath(a.c_str(), buf))
    return buf;
  return a;
}

std::string utc_now()
{
  time_t t = time(nullptr);
  struct tm tm;
  char buf[32];

  gmtime_r(&t, &tm);
  strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
  return buf;
}

void write_registry()
{
  const char* session = getenv("CLAUDE_CODE_SESSION_ID");
  Json d = Json::object();

  d.set("pid", g_vm.pid);
  d.set("name", g_vm.name);
  d.set("socket", g_vm.socket);
  d.set("cwd", g_vm.cwd);
  d.set("binary", g_vm.binary);
  d.set("config", g_vm.config);
  d.set("started", g_vm.started);
  // an agent session that started this VM (from its shell tool), so that
  // es40-mcp can tell agents which VMs are theirs
  d.set("by-agent", getenv("CLAUDECODE") != nullptr && session != nullptr);
  d.set("session", session ? Json(session) : Json());

  // write and rename, so a reader never sees half a file
  const std::string text = d.dump() + "\n";
  const std::string tmp = g_json_path + ".tmp";
  int fd = open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
  bool ok = fd >= 0;

  if (ok)
  {
    ok = write(fd, text.data(), text.size()) == (ssize_t)text.size();
    ok = (close(fd) == 0) && ok;
  }
  if (!ok || rename(tmp.c_str(), g_json_path.c_str()) < 0)
  {
    printf("%%MCP-W-REGISTRY: cannot write %s: %s\n", g_json_path.c_str(), strerror(errno));
    unlink(tmp.c_str());
  }
}

// The socket this VM made, so it never removes another VM's at the same path
dev_t g_sock_dev = 0;
ino_t g_sock_ino = 0;

void remove_files()
{
  struct stat st;

  if (!g_json_path.empty())
    unlink(g_json_path.c_str());
  if (!g_vm.socket.empty() && lstat(g_vm.socket.c_str(), &st) == 0 && st.st_dev == g_sock_dev &&
    st.st_ino == g_sock_ino)
    unlink(g_vm.socket.c_str());
}

/// Makes room for the socket at addr: it removes a socket that nothing
/// listens on any more, but never a live one or a file that is no socket
bool socket_path_free(const struct sockaddr_un& addr, std::string& err)
{
  struct stat st;

  if (lstat(addr.sun_path, &st) < 0)
  {
    if (errno == ENOENT)
      return true;
    err = std::string("cannot check ") + addr.sun_path + ": " + strerror(errno);
    return false;
  }
  if (!S_ISSOCK(st.st_mode))
  {
    err = std::string(addr.sun_path) + " exists and is not a socket";
    return false;
  }
  int fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (fd < 0)
  {
    err = std::string("cannot check ") + addr.sun_path + ": " + strerror(errno);
    return false;
  }
  const bool live = connect(fd, (const struct sockaddr*)&addr, sizeof(addr)) == 0 ||
    (errno != ECONNREFUSED && errno != ENOENT);
  close(fd);
  if (live)
  {
    err = std::string("another es40 listens on ") + addr.sun_path;
    return false;
  }
  unlink(addr.sun_path);  // left by a VM that is gone
  return true;
}

void remove_files_at_exit()
{
  if (g_running)
    remove_files();
}

} // namespace

void es40_mcp_configure(CConfigurator* cfg)
{
  const char* v;

  g_settings.enabled = cfg->get_bool_value("mcp", false);
  if ((v = cfg->get_text_value("mcp.name", nullptr)) && *v)
    g_settings.name = v;
  if ((v = cfg->get_text_value("mcp.registry", nullptr)) && *v)
    g_settings.registry = v;
  if ((v = cfg->get_text_value("mcp.socket", nullptr)) && *v)
    g_settings.socket = v;
}

void es40_mcp_start(const char* argv0, const char* config_file)
{
  const char* env = getenv("ES40_MCP");
  char buf[PATH_MAX];
  std::string err;

  if (g_running)
    return;
  if (env && *env)
    g_settings.enabled = truthy(env);
  if (!g_settings.enabled)
    return;
  env = getenv("ES40_MCP_REGISTRY");
  if (env && *env)
    g_settings.registry = env;

  g_vm.pid = (int)getpid();
  g_vm.cwd = getcwd(buf, sizeof(buf)) ? buf : ".";
  if (!g_settings.name.empty())
  {
    g_vm.name = g_settings.name;
  }
  else
  {
    size_t slash = g_vm.cwd.find_last_of('/');
    g_vm.name = slash == std::string::npos ? g_vm.cwd : g_vm.cwd.substr(slash + 1);
    if (g_vm.name.empty())
      g_vm.name = "es40";
  }
  g_vm.config = (config_file && realpath(config_file, buf)) ? buf : (config_file ? config_file : "");
  g_vm.binary = binary_path(argv0);
  g_vm.started = utc_now();
  g_vm.registry = !g_settings.registry.empty() ? g_settings.registry
    : "/tmp/es40-" + std::to_string((unsigned)getuid());

  if (!make_private_dir(g_vm.registry, err))
  {
    printf("%%MCP-W-OFF: MCP server is off: %s\n", err.c_str());
    return;
  }
  drop_stale(g_vm.registry);
  g_vm.socket = !g_settings.socket.empty() ? g_settings.socket
    : g_vm.registry + "/" + std::to_string(g_vm.pid) + ".sock";
  g_json_path = g_vm.registry + "/" + std::to_string(g_vm.pid) + ".json";

  struct sockaddr_un addr;
  memset(&addr, 0, sizeof(addr));
  addr.sun_family = AF_UNIX;
  if (g_vm.socket.size() >= sizeof(addr.sun_path))
  {
    printf("%%MCP-W-OFF: MCP server is off: socket path %s is too long\n", g_vm.socket.c_str());
    return;
  }
  memcpy(addr.sun_path, g_vm.socket.c_str(), g_vm.socket.size() + 1);

  if (!socket_path_free(addr, err))
  {
    printf("%%MCP-W-OFF: MCP server is off: %s\n", err.c_str());
    return;
  }
  struct stat st;
  bool bound = false;
  g_listen = socket(AF_UNIX, SOCK_STREAM, 0);
  if (g_listen < 0 || !(bound = bind(g_listen, (struct sockaddr*)&addr, sizeof(addr)) == 0) ||
    lstat(g_vm.socket.c_str(), &st) < 0 || chmod(g_vm.socket.c_str(), 0600) < 0 ||
    listen(g_listen, 8) < 0 || pipe(g_wake) < 0)
  {
    printf("%%MCP-W-OFF: MCP server is off: %s: %s\n", g_vm.socket.c_str(), strerror(errno));
    if (g_listen >= 0)
      close(g_listen);
    g_listen = -1;
    if (bound)  // only the socket this VM made
      unlink(g_vm.socket.c_str());
    return;
  }
  g_sock_dev = st.st_dev;
  g_sock_ino = st.st_ino;
  fcntl(g_listen, F_SETFD, FD_CLOEXEC);
  fcntl(g_wake[0], F_SETFD, FD_CLOEXEC);
  fcntl(g_wake[1], F_SETFD, FD_CLOEXEC);

  es40_mcp_tap = true;
  g_running = true;
  write_registry();
  atexit(remove_files_at_exit);
  g_server = std::thread(server_main);
  printf("%%MCP-I-LISTEN: MCP server for VM %s on %s\n", g_vm.name.c_str(), g_vm.socket.c_str());
}

void es40_mcp_stop()
{
  if (!g_running)
    return;
  g_stopping = true;
  es40_mcp_tap = false;
  if (g_wake[1] >= 0)
  {
    char c = 'x';
    if (write(g_wake[1], &c, 1) < 0)
    {
      // the server thread also checks g_stopping
    }
  }
  {
    // a send blocked on a client that does not read would keep the server
    // thread from seeing the wake-up
    std::lock_guard<std::mutex> lock(g_conn_mutex);
    if (g_conn)
      g_conn->shutdown();
  }
  if (g_server.joinable())
    g_server.join();
  {
    // The calls use the keyboard and the displays, which go away after this.
    // Their client is gone, so they are cancelled and end within moments.
    std::unique_lock<std::mutex> lock(g_calls_mutex);
    if (!g_calls_cv.wait_for(lock, std::chrono::seconds(5), [] { return g_calls == 0; }))
    {
      printf("%%MCP-I-WAIT: waiting for %d agent calls to end\n", g_calls);
      g_calls_cv.wait(lock, [] { return g_calls == 0; });
    }
  }
  es40mcp::release_input();
  close(g_listen);
  g_listen = -1;
  remove_files();
  g_running = false;
}

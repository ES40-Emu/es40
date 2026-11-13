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
 * es40-mcp: the MCP server that an agent such as Claude Code starts, for
 * example with "claude mcp add es40 -- /path/to/es40-mcp". It speaks MCP's
 * stdio transport, one JSON-RPC message per line on stdin and stdout, and
 * reaches every es40 that runs with its MCP server on (mcp = true). It finds
 * them in the registry folder (see McpServer.h), lists them (vm-list), waits
 * for one to start or to end (vm-wait), and passes every other tool call on
 * to the VM that the call's vm argument names.
 *
 * es40 serves one MCP client at a time, so es40-mcp stays connected to a VM
 * only while it has calls for it, and a few seconds more; other agent
 * sessions can use the VM in between. It keeps the connection while a mouse
 * button pressed with mouse-button down is still down, because es40 lets go
 * of what a client held when that client leaves.
 **/

#include "McpJson.h"
#include "McpSchemas.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <climits>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <map>
#include <memory>
#include <poll.h>
#include <string>
#include <strings.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#include <vector>

#if !defined(PACKAGE_VERSION)
#define PACKAGE_VERSION "unknown"
#endif

using es40mcp::Json;

namespace
{

using Clock = std::chrono::steady_clock;

const int kHelloMs = 5000;  // a VM that does not answer initialize this soon serves another client
const int kIdleMs = 3000;   // how long a connection with nothing to do stays open
const int kTickMs = 250;    // how often timeouts and waits are checked
const int kProgressSecs = 5;

const char* const g_versions[] = { "2025-11-25", "2025-06-18", "2025-03-26", "2024-11-05", nullptr };

std::string g_registry;
std::string g_session;  // the agent session that started es40-mcp, if the agent says
std::string g_version = g_versions[0];

// ---------------------------------------------------------------- messages

Json message(const char* method)
{
  Json m = Json::object();
  m.set("jsonrpc", "2.0");
  m.set("method", method);
  return m;
}

Json response(const Json& id, Json result)
{
  Json m = Json::object();
  m.set("jsonrpc", "2.0");
  m.set("id", id);
  m.set("result", std::move(result));
  return m;
}

Json error_response(const Json& id, int code, const std::string& text)
{
  Json e = Json::object();
  e.set("code", code);
  e.set("message", text);

  Json m = Json::object();
  m.set("jsonrpc", "2.0");
  m.set("id", id);
  m.set("error", std::move(e));
  return m;
}

/// A tool result that is text only
Json text_result(const std::string& text, bool error = false)
{
  Json t = Json::object();
  t.set("type", "text");
  t.set("text", text);
  Json content = Json::array();
  content.push(std::move(t));

  Json r = Json::object();
  r.set("content", std::move(content));
  if (error)
    r.set("isError", true);
  return r;
}

/// Writes one message as one line; false once the other end is gone
bool send_line(int fd, const Json& msg)
{
  std::string line = msg.dump();
  const auto give_up = Clock::now() + std::chrono::seconds(30);
  size_t done = 0;

  line += '\n';
  while (done < line.size())
  {
    ssize_t n = write(fd, line.data() + done, line.size() - done);

    if (n > 0)
    {
      done += (size_t)n;
      continue;
    }
    if (n < 0 && errno == EINTR)
      continue;
    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK) && Clock::now() < give_up)
    {
      struct pollfd p = { fd, POLLOUT, 0 };
      poll(&p, 1, 1000);
      continue;
    }
    return false;
  }
  return true;
}

void to_agent(const Json& msg)
{
  if (!send_line(STDOUT_FILENO, msg))
    exit(0);  // the agent is gone
}

std::string secs(Clock::duration d)
{
  char buf[32];
  snprintf(buf, sizeof(buf), "%.1f s", std::chrono::duration<double>(d).count());
  return buf;
}

// ---------------------------------------------------------------- the registry

/// A running VM, as its <pid>.json in the registry describes it
struct Vm
{
  int pid = 0;
  std::string name, socket, cwd, config, started, session;
  bool by_agent = false;
};

std::map<int, Vm> g_seen;  // every VM found so far, by pid, while it lives

bool alive(int pid)
{
  return pid > 0 && (kill((pid_t)pid, 0) == 0 || errno == EPERM);
}

std::string label(const Vm& v)
{
  return v.name + " (pid " + std::to_string(v.pid) + ")";
}

std::string labels(const std::vector<Vm>& vms)
{
  std::string s;
  for (size_t i = 0; i < vms.size(); i++)
    s += (i ? ", " : "") + label(vms[i]);
  return s;
}

/// The registry must be a folder of this user's alone, as es40 makes it
bool registry_ok(std::string& err)
{
  struct stat st;

  if (mkdir(g_registry.c_str(), 0700) < 0 && errno != EEXIST)
  {
    err = "cannot create " + g_registry + ": " + strerror(errno);
    return false;
  }
  if (lstat(g_registry.c_str(), &st) < 0)
  {
    err = "cannot check " + g_registry + ": " + strerror(errno);
    return false;
  }
  if (!S_ISDIR(st.st_mode))
  {
    err = g_registry + " is not a folder";
    return false;
  }
  if (st.st_uid != getuid())
  {
    err = g_registry + " belongs to another user, so es40-mcp does not trust it";
    return false;
  }
  if (st.st_mode & 077)
  {
    err = g_registry + " is open to other users (it must be mode 0700), so es40-mcp does not trust it";
    return false;
  }
  return true;
}

bool read_small_file(const std::string& path, std::string& out)
{
  int fd = open(path.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
  struct stat st;
  char buf[4096];
  ssize_t n;

  if (fd < 0)
    return false;
  if (fstat(fd, &st) < 0 || !S_ISREG(st.st_mode) || st.st_uid != getuid() || st.st_size > 65536)
  {
    close(fd);
    return false;
  }
  out.clear();
  while ((n = read(fd, buf, sizeof(buf))) > 0)
    out.append(buf, (size_t)n);
  close(fd);
  return n == 0;
}

std::string field(const Json& j, const char* key)
{
  const Json* v = j.find(key);
  return v && v->is_string() ? v->as_string() : std::string();
}

/// The VMs that run now, sorted by name
bool scan(std::vector<Vm>& vms, std::string& err)
{
  DIR* d;

  vms.clear();
  if (!registry_ok(err))
    return false;
  if (!(d = opendir(g_registry.c_str())))
  {
    err = "cannot read " + g_registry + ": " + strerror(errno);
    return false;
  }
  while (struct dirent* e = readdir(d))
  {
    char* end;
    long pid = strtol(e->d_name, &end, 10);
    std::string text, perr;
    Json j;
    Vm v;

    if (end == e->d_name || pid <= 0 || pid > INT_MAX || strcmp(end, ".json") != 0)
      continue;
    if (!read_small_file(g_registry + "/" + e->d_name, text) || !Json::parse(text, j, perr) ||
      !j.is_object())
      continue;
    const Json* p = j.find("pid");
    const Json* a = j.find("by-agent");
    v.pid = (int)pid;
    if (!p || p->as_int(-1) != pid || !alive(v.pid))
      continue;
    v.name = field(j, "name");
    v.socket = field(j, "socket");
    v.cwd = field(j, "cwd");
    v.config = field(j, "config");
    v.started = field(j, "started");
    v.session = field(j, "session");
    v.by_agent = a && a->as_bool(false);
    if (v.name.empty() || v.socket.empty())
      continue;
    vms.push_back(v);
    g_seen[v.pid] = v;
  }
  closedir(d);
  std::sort(vms.begin(), vms.end(), [](const Vm& a, const Vm& b) {
    const int c = strcasecmp(a.name.c_str(), b.name.c_str());
    return c ? c < 0 : a.pid < b.pid;
  });
  for (auto it = g_seen.begin(); it != g_seen.end();)
    it = alive(it->first) ? std::next(it) : g_seen.erase(it);
  return true;
}

std::string none_text()
{
  return "No es40 VM is running with its MCP server on (" + g_registry + " lists none). Start es40"
    " in the background with mcp = true in its configuration, or with ES40_MCP=1, then call vm-wait"
    " until running.";
}

/// What a vm argument names: a VM name, a pid, or (empty) any VM
struct Want
{
  std::string name;
  int pid = 0;

  bool any() const { return name.empty() && !pid; }
  std::string text() const { return name.empty() ? "pid " + std::to_string(pid) : name; }
};

bool parse_want(const Json* vm, Want& w, std::string& err)
{
  w = Want();
  if (!vm || vm->is_null())
    return true;
  if (vm->is_int() && vm->as_int() > 0 && vm->as_int() <= INT_MAX)
  {
    w.pid = (int)vm->as_int();
    return true;
  }
  if (!vm->is_string())
  {
    err = "vm must be a VM name or a pid";
    return false;
  }
  w.name = vm->as_string();

  char* end;
  long pid = strtol(w.name.c_str(), &end, 10);
  if (!w.name.empty() && !*end && pid > 0 && pid <= INT_MAX)
    w.pid = (int)pid;
  return true;
}

bool matches(const Vm& v, const Want& w)
{
  return w.any() || (w.pid && v.pid == w.pid) ||
    (!w.name.empty() && !strcasecmp(v.name.c_str(), w.name.c_str()));
}

/// The one running VM that w names; with no name, the only one that runs
bool pick(const std::vector<Vm>& vms, const Want& w, Vm& out, std::string& err)
{
  std::vector<Vm> hits;

  for (const auto& v : vms)
  {
    if (matches(v, w))
      hits.push_back(v);
  }
  if (hits.size() == 1)
  {
    out = hits[0];
    return true;
  }
  if (vms.empty())
    err = none_text();
  else if (hits.empty())
    err = "No running es40 VM is named " + w.text() + ". Running: " + labels(vms) + ".";
  else if (w.any())
    err = "Several es40 VMs run: " + labels(vms) + ". Say which one with vm.";
  else
    err = "Several running VMs are named " + w.text() + ": " + labels(hits) + ". Give the pid as vm.";
  return false;
}

// ---------------------------------------------------------------- connections to VMs

struct Pending
{
  Json agent_id;  // the id the agent gave the call
  std::string tool;
};

struct Conn
{
  Vm vm;
  int fd = -1;
  bool ready = false;  // es40 answered initialize
  bool dead = false;
  bool busy = false;   // es40 never answered: it serves another client
  std::string why;     // why it is dead
  Clock::time_point opened, last;
  std::string in;
  std::vector<Json> queue;  // calls held back until es40 answers initialize
  std::map<int64_t, Pending> pending;  // by the id es40-mcp gave the call
  int64_t next_id = 1;
  int held = 0;  // buttons pressed with mouse-button down and not released yet
};

std::map<int, std::unique_ptr<Conn>> g_conns;  // by pid

void kill_conn(Conn* c, const std::string& why)
{
  if (!c->dead)
  {
    c->dead = true;
    c->why = why;
  }
}

std::string ended_text(const Conn* c, const Pending& p)
{
  const std::string vm = "VM " + label(c->vm);

  if (c->busy)
    return vm + " does not answer: es40 serves one MCP client at a time, and another one is using"
      " it, probably another agent session. Try again when that is done, or ask the user.";
  if (!alive(c->vm.pid))
    return vm + " ended before it answered " + p.tool + (p.tool == "vm-power" ? "; es40 has quit." : ".");
  return vm + " closed the connection before it answered " + p.tool + " (" + c->why +
    "); es40 is probably quitting.";
}

/// Closes a dead connection and answers the calls it still had
void reap_one(std::map<int, std::unique_ptr<Conn>>::iterator it)
{
  Conn* c = it->second.get();

  for (const auto& p : c->pending)
    to_agent(response(p.second.agent_id, text_result(ended_text(c, p.second), true)));
  close(c->fd);
  g_conns.erase(it);
}

void reap()
{
  for (auto it = g_conns.begin(); it != g_conns.end();)
  {
    auto next = std::next(it);
    if (it->second->dead)
      reap_one(it);
    it = next;
  }
}

Conn* open_conn(const Vm& v, std::string& err)
{
  auto it = g_conns.find(v.pid);
  struct sockaddr_un addr;
  struct stat st;

  if (it != g_conns.end())
  {
    if (!it->second->dead)
      return it->second.get();
    reap_one(it);
  }
  memset(&addr, 0, sizeof(addr));
  addr.sun_family = AF_UNIX;
  if (v.socket.size() >= sizeof(addr.sun_path))
  {
    err = "the socket path of VM " + label(v) + " is too long";
    return nullptr;
  }
  if (lstat(v.socket.c_str(), &st) < 0 || !S_ISSOCK(st.st_mode) || st.st_uid != getuid())
  {
    err = "VM " + label(v) + " has no socket of this user's at " + v.socket;
    return nullptr;
  }
  memcpy(addr.sun_path, v.socket.c_str(), v.socket.size() + 1);

  int fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (fd < 0)
  {
    err = std::string("cannot make a socket: ") + strerror(errno);
    return nullptr;
  }
  fcntl(fd, F_SETFD, FD_CLOEXEC);
#if defined(SO_NOSIGPIPE)
  int one = 1;
  setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#endif
  if (connect(fd, (const struct sockaddr*)&addr, sizeof(addr)) < 0)
  {
    err = "cannot reach VM " + label(v) + " at " + v.socket + ": " + strerror(errno);
    close(fd);
    return nullptr;
  }
  fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);

  Json params = Json::object();
  Json info = Json::object();
  Json hello = message("initialize");
  info.set("name", "es40-mcp");
  info.set("version", PACKAGE_VERSION);
  params.set("protocolVersion", g_version);
  params.set("capabilities", Json::object());
  params.set("clientInfo", std::move(info));
  hello.set("id", 0);
  hello.set("params", std::move(params));
  if (!send_line(fd, hello))
  {
    err = "VM " + label(v) + " closed the connection at once";
    close(fd);
    return nullptr;
  }

  auto c = std::make_unique<Conn>();
  Conn* raw = c.get();
  c->vm = v;
  c->fd = fd;
  c->opened = c->last = Clock::now();
  g_conns[v.pid] = std::move(c);
  return raw;
}

/// How many mouse buttons a call presses (down) less those it releases (up)
int button_change(const std::string& tool, const Json& args)
{
  auto one = [](const Json& a) {
    const std::string& action = a.find("action") ? a.find("action")->as_string() : std::string();
    return action == "down" ? 1 : action == "up" ? -1 : 0;
  };
  int n = 0;

  if (tool == "mouse-button")
    return one(args);
  if (tool == "steps" && args.find("steps"))
  {
    for (const auto& s : args.find("steps")->items())
    {
      const Json* t = s.find("tool");
      const Json* a = s.find("args");
      if (t && t->as_string() == "mouse-button" && a)
        n += one(*a);
    }
  }
  return n;
}

void forward(Conn* c, const Json& agent_id, const std::string& tool, Json params)
{
  const int64_t id = c->next_id++;
  Json m = message("tools/call");

  m.set("id", id);
  m.set("params", std::move(params));
  c->pending[id] = Pending{ agent_id, tool };
  c->last = Clock::now();
  if (!c->ready)
    c->queue.push_back(std::move(m));
  else if (!send_line(c->fd, m))
    kill_conn(c, "the connection broke");
}

void from_vm(Conn* c, const std::string& line)
{
  Json msg;
  std::string err;

  if (line.find_first_not_of(" \t\r") == std::string::npos)
    return;
  if (!Json::parse(line, msg, err) || !msg.is_object())
    return;

  const Json* method = msg.find("method");
  const Json* id = msg.find("id");

  if (method)
  {
    // es40 asks the client nothing; its notifications are progress reports for the agent
    if (!id && method->as_string() == "notifications/progress")
      to_agent(msg);
    return;
  }
  if (!id || !id->is_int())
    return;
  if (id->as_int() == 0 && !c->ready)
  {
    c->ready = true;
    if (!send_line(c->fd, message("notifications/initialized")))
    {
      kill_conn(c, "the connection broke");
      return;
    }
    for (const auto& q : c->queue)
    {
      if (!send_line(c->fd, q))
      {
        kill_conn(c, "the connection broke");
        return;
      }
    }
    c->queue.clear();
    return;
  }

  auto it = c->pending.find(id->as_int());
  if (it == c->pending.end())
    return;  // cancelled meanwhile
  msg.set("id", it->second.agent_id);
  c->pending.erase(it);
  c->last = Clock::now();
  to_agent(msg);
}

void read_conn(int pid)
{
  auto it = g_conns.find(pid);
  char buf[65536];
  bool eof = false;

  if (it == g_conns.end() || it->second->dead)
    return;
  Conn* c = it->second.get();
  for (;;)
  {
    ssize_t r = read(c->fd, buf, sizeof(buf));

    if (r > 0)
    {
      c->in.append(buf, (size_t)r);
      continue;
    }
    if (r < 0 && errno == EINTR)
      continue;
    eof = !(r < 0 && (errno == EAGAIN || errno == EWOULDBLOCK));
    break;
  }

  size_t start = 0, nl;
  while (!c->dead && (nl = c->in.find('\n', start)) != std::string::npos)
  {
    from_vm(c, c->in.substr(start, nl - start));
    start = nl + 1;
  }
  c->in.erase(0, start);
  if (c->in.size() > 256 * 1024 * 1024)
    kill_conn(c, "es40 sent a message that is too long");
  if (eof)
    kill_conn(c, "es40 closed it");
}

/// Ends connections that never got an answer, and those with nothing to do
void check_conns()
{
  const auto now = Clock::now();

  for (auto& kv : g_conns)
  {
    Conn* c = kv.second.get();

    if (c->dead)
      continue;
    if (!c->ready && now - c->opened > std::chrono::milliseconds(kHelloMs))
    {
      c->busy = true;
      kill_conn(c, "no answer to initialize");
    }
    else if (c->ready && c->pending.empty() && c->held <= 0 &&
      now - c->last > std::chrono::milliseconds(kIdleMs))
    {
      kill_conn(c, "idle");
    }
  }
}

// ---------------------------------------------------------------- vm-wait

struct Wait
{
  Json agent_id;
  Json token;  // progress token, or null
  bool gone = false;
  Want want;
  Vm vm;  // for gone: the VM it waits for
  Clock::time_point start, deadline, last_progress;
};

std::vector<Wait> g_waits;

void progress(Wait& w, Clock::time_point now, const std::string& what)
{
  if (w.token.is_null() || now - w.last_progress < std::chrono::seconds(kProgressSecs))
    return;
  w.last_progress = now;

  Json p = Json::object();
  Json m = message("notifications/progress");
  p.set("progressToken", w.token);
  p.set("progress", (double)(long)std::chrono::duration<double>(now - w.start).count());
  p.set("message", what);
  m.set("params", std::move(p));
  to_agent(m);
}

/// True once the wait has answered the agent
bool check_wait(Wait& w, const std::vector<Vm>& vms, Clock::time_point now)
{
  if (w.gone)
  {
    if (!alive(w.vm.pid))
    {
      to_agent(response(w.agent_id, text_result("VM " + label(w.vm) + " is gone (after " +
        secs(now - w.start) + "): es40 has ended and closed its files.")));
      return true;
    }
  }
  else
  {
    for (const auto& v : vms)
    {
      if (matches(v, w.want))
      {
        to_agent(response(w.agent_id, text_result("VM " + label(v) + " runs (found after " +
          secs(now - w.start) + "). Folder: " + v.cwd + ".")));
        return true;
      }
    }
  }
  if (now >= w.deadline)
  {
    const std::string text = w.gone
      ? "VM " + label(w.vm) + " still runs after " + secs(now - w.start) + "."
      : (w.want.any() ? std::string("No es40 VM") : "No es40 VM named " + w.want.text()) +
        " started within " + secs(now - w.start) + ".";
    to_agent(response(w.agent_id, text_result(text, true)));
    return true;
  }
  progress(w, now, w.gone ? "waiting for VM " + label(w.vm) + " to end"
    : "waiting for " + (w.want.any() ? std::string("an es40 VM") : "VM " + w.want.text()) + " to start");
  return false;
}

void check_waits()
{
  std::vector<Vm> vms;
  std::string err;
  bool scanned = true;

  if (g_waits.empty())
    return;
  for (const auto& w : g_waits)
  {
    if (!w.gone)
    {
      scanned = scan(vms, err);
      break;
    }
  }

  const auto now = Clock::now();
  for (size_t i = 0; i < g_waits.size();)
  {
    Wait& w = g_waits[i];
    bool over;

    if (!w.gone && !scanned)
    {
      to_agent(response(w.agent_id, text_result("es40-mcp cannot use the registry: " + err, true)));
      over = true;
    }
    else
    {
      over = check_wait(w, vms, now);
    }
    if (over)
      g_waits.erase(g_waits.begin() + (long)i);
    else
      i++;
  }
}

void start_wait(const Json& id, const Json& args, const Json& token)
{
  const Json* until = args.find("until");
  const Json* timeout = args.find("timeout-ms");
  const std::string u = until ? until->as_string() : std::string();
  int64_t ms = 60000;
  std::string err;
  Wait w;

  if (u != "running" && u != "gone")
  {
    to_agent(response(id, text_result("until must be running or gone", true)));
    return;
  }
  if (timeout && !timeout->is_null())
  {
    if (!timeout->is_int() || timeout->as_int() < 0 || timeout->as_int() > es40mcp::kMaxWaitMs)
    {
      to_agent(response(id, text_result("timeout-ms must be a whole number from 0 to 1500000", true)));
      return;
    }
    ms = timeout->as_int();
  }
  if (!parse_want(args.find("vm"), w.want, err))
  {
    to_agent(response(id, text_result(err, true)));
    return;
  }
  w.agent_id = id;
  w.token = token;
  w.gone = u == "gone";
  w.start = w.last_progress = Clock::now();
  w.deadline = w.start + std::chrono::milliseconds(ms);

  if (w.gone)
  {
    std::vector<Vm> vms, hits;
    if (!scan(vms, err))
    {
      to_agent(response(id, text_result("es40-mcp cannot use the registry: " + err, true)));
      return;
    }
    for (const auto& v : vms)
    {
      if (matches(v, w.want))
        hits.push_back(v);
    }
    // a VM that is quitting has removed its registry entry, but may still be closing its files
    if (hits.empty())
    {
      for (const auto& kv : g_seen)
      {
        if (matches(kv.second, w.want) && alive(kv.first))
          hits.push_back(kv.second);
      }
    }
    if (hits.empty())
    {
      to_agent(response(id, text_result(w.want.any() ? std::string("No es40 VM is running.")
        : "No es40 VM named " + w.want.text() + " is running.")));
      return;
    }
    if (hits.size() > 1)
    {
      to_agent(response(id, text_result((w.want.any() ? std::string("Several es40 VMs run: ")
        : "Several running VMs are named " + w.want.text() + ": ") + labels(hits) + ". Say which one"
        " with vm (a pid if the names are the same).", true)));
      return;
    }
    w.vm = hits[0];
  }
  g_waits.push_back(w);
  check_waits();  // it may be over already
}

// ---------------------------------------------------------------- the agent's messages

const char kVmList[] =
  R"J({"name":"vm-list","description":"List the es40 VMs that run with their MCP server on: name, pid,)J"
  R"J( folder, configuration, when each started, and whether this agent session, another one or a)J"
  R"J( person started it. The other tools take one of these names or pids as vm.",)J"
  R"J("inputSchema":{"type":"object","properties":{}},)J"
  R"J("annotations":{"readOnlyHint":true,"openWorldHint":false}})J";

const char kVmWait[] =
  R"J({"name":"vm-wait","description":"Wait until an es40 VM runs (after you started it) or is gone)J"
  R"J( (after vm-power quit, before you copy its files). One call can wait up to 25 minutes.",)J"
  R"J("inputSchema":{"type":"object","properties":{)J"
  R"J("until":{"type":"string","enum":["running","gone"],"description":"what to wait for"},)J"
  R"J("vm":{"type":"string","description":"the VM's name or pid; for running, leave it out to wait)J"
  R"J( for any VM; for gone, while only one runs"},)J"
  R"J("timeout-ms":{"type":"integer","description":"give up after this long, at most 1500000, 25)J"
  R"J( minutes (default: 60000)"}},"required":["until"]},)J"
  R"J("annotations":{"readOnlyHint":true,"openWorldHint":false}})J";

/// A tool of es40's own with the vm argument added
Json with_vm(const Json& tool)
{
  Json vm = Json::object();
  Json out = Json::object();

  vm.set("type", "string");
  vm.set("description", "which es40 VM: its name or pid, as vm-list shows (default: the only one that runs)");
  for (const auto& kv : tool.members())
  {
    if (kv.first != "inputSchema")
    {
      out.set(kv.first, kv.second);
      continue;
    }
    Json schema = kv.second;
    const Json* old = kv.second.find("properties");
    Json props = Json::object();
    if (old)
    {
      for (const auto& p : old->members())
        props.set(p.first, p.second);
    }
    props.set("vm", vm);
    schema.set("properties", std::move(props));
    out.set("inputSchema", std::move(schema));
  }
  return out;
}

const Json& tools()
{
  static Json list;

  if (list.is_array())
    return list;
  list = Json::array();
  for (const char* s : { kVmList, kVmWait })
  {
    Json t;
    std::string err;
    if (Json::parse(s, t, err))
      list.push(std::move(t));
    else
      fprintf(stderr, "es40-mcp: a tool schema does not parse: %s\n", err.c_str());
  }
  const Json own = es40mcp::tool_list();  // a copy that lives through the loop
  for (const auto& t : own.items())
    list.push(with_vm(t));
  return list;
}

/// A tool that es40 itself answers
bool vm_tool(const std::string& name)
{
  const Json own = es40mcp::tool_list();

  for (const auto& t : own.items())
  {
    const Json* n = t.find("name");
    if (n && n->as_string() == name)
      return true;
  }
  return false;
}

std::string instructions()
{
  return std::string(es40mcp::server_instructions) +
    "- es40-mcp reaches every es40 that runs with mcp = true in its configuration (or"
    " ES40_MCP=1); vm-list shows them. Each tool takes vm: the VM's name (by default the folder"
    " es40 runs in) or its pid. Leave vm out while only one VM runs.\n"
    "- After you start a VM, call vm-wait until running. After vm-power quit, call vm-wait until"
    " gone before you copy its files.\n"
    "- es40 serves one MCP client at a time. While another agent session uses a VM, calls to it"
    " fail as busy; es40-mcp lets go of a VM a few seconds after its last call.\n";
}

Json initialize_result(const Json* params)
{
  const Json* asked = params ? params->find("protocolVersion") : nullptr;
  Json tools_cap = Json::object();
  Json caps = Json::object();
  Json info = Json::object();
  Json r = Json::object();

  g_version = g_versions[0];
  for (int i = 0; asked && g_versions[i]; i++)
  {
    if (asked->as_string() == g_versions[i])
      g_version = g_versions[i];
  }
  tools_cap.set("listChanged", false);
  caps.set("tools", std::move(tools_cap));
  info.set("name", "es40-mcp");
  info.set("version", PACKAGE_VERSION);
  r.set("protocolVersion", g_version);
  r.set("capabilities", std::move(caps));
  r.set("serverInfo", std::move(info));
  r.set("instructions", instructions());
  return r;
}

std::string list_text(bool& error)
{
  std::vector<Vm> vms;
  std::string err, t;

  error = false;
  if (!scan(vms, err))
  {
    error = true;
    return "es40-mcp cannot use the registry: " + err;
  }
  if (vms.empty())
    return none_text();
  t = std::to_string(vms.size()) + (vms.size() == 1 ? " es40 VM runs" : " es40 VMs run") +
    " (registry " + g_registry + "):\n";
  for (const auto& v : vms)
  {
    auto c = g_conns.find(v.pid);

    t += "- " + v.name + ": pid " + std::to_string(v.pid) + ", started " + v.started + " (UTC), folder " +
      v.cwd + ", configuration " + v.config + "; ";
    if (!v.by_agent)
      t += "started by a person, not by an agent";
    else if (!g_session.empty() && v.session == g_session)
      t += "started by this agent session";
    else
      t += "started by another agent session";
    if (c != g_conns.end() && !c->second->dead && c->second->ready)
      t += "; es40-mcp is connected to it";
    t += ".\n";
  }
  return t;
}

/// True if a call with this id still runs
bool in_use(const Json& id)
{
  for (const auto& w : g_waits)
  {
    if (w.agent_id == id)
      return true;
  }
  for (const auto& kv : g_conns)
  {
    for (const auto& p : kv.second->pending)
    {
      if (p.second.agent_id == id)
        return true;
    }
  }
  return false;
}

void cancel(const Json& request_id)
{
  for (size_t i = 0; i < g_waits.size(); i++)
  {
    if (g_waits[i].agent_id == request_id)
    {
      g_waits.erase(g_waits.begin() + (long)i);
      return;
    }
  }
  for (auto& kv : g_conns)
  {
    Conn* c = kv.second.get();

    for (auto it = c->pending.begin(); it != c->pending.end(); ++it)
    {
      if (it->second.agent_id != request_id)
        continue;
      const int64_t id = it->first;
      c->pending.erase(it);
      c->last = Clock::now();
      // a call still held back never reaches es40
      for (auto q = c->queue.begin(); q != c->queue.end(); ++q)
      {
        const Json* qid = q->find("id");
        if (qid && qid->as_int(-1) == id)
        {
          c->queue.erase(q);
          return;
        }
      }
      Json p = Json::object();
      Json m = message("notifications/cancelled");
      p.set("requestId", id);
      m.set("params", std::move(p));
      if (!c->dead && c->ready && !send_line(c->fd, m))
        kill_conn(c, "the connection broke");
      return;
    }
  }
}

void call(const Json& id, const Json* params)
{
  const Json* name = params ? params->find("name") : nullptr;
  const Json* a = params ? params->find("arguments") : nullptr;
  const Json* meta = params ? params->find("_meta") : nullptr;
  const Json* token = meta ? meta->find("progressToken") : nullptr;
  const Json args = a && a->is_object() ? *a : Json::object();
  std::vector<Vm> vms;
  std::string err;
  Want want;
  Vm vm;

  if (!name || !name->is_string())
  {
    to_agent(error_response(id, -32602, "tools/call needs a tool name"));
    return;
  }
  if (in_use(id))
  {
    to_agent(error_response(id, -32600, "request id " + id.dump() + " is still in use by a running call"));
    return;
  }

  const std::string tool = name->as_string();
  if (tool == "vm-list")
  {
    bool error;
    const std::string text = list_text(error);
    to_agent(response(id, text_result(text, error)));
    return;
  }
  if (tool == "vm-wait")
  {
    start_wait(id, args, token ? *token : Json());
    return;
  }
  if (!vm_tool(tool))
  {
    to_agent(response(id, text_result("es40-mcp has no tool " + tool + ".", true)));
    return;
  }
  if (!parse_want(args.find("vm"), want, err) || !scan(vms, err) || !pick(vms, want, vm, err))
  {
    to_agent(response(id, text_result(err, true)));
    return;
  }

  Conn* c = open_conn(vm, err);
  if (!c)
  {
    to_agent(response(id, text_result(err, true)));
    return;
  }
  Json fwd = *params;
  Json fargs = args;
  fargs.erase("vm");
  fwd.set("arguments", std::move(fargs));
  c->held = std::max(0, c->held + button_change(tool, args));
  forward(c, id, tool, std::move(fwd));
}

void handle_agent(const std::string& line)
{
  Json msg;
  std::string err;

  if (line.find_first_not_of(" \t\r") == std::string::npos)
    return;
  if (!Json::parse(line, msg, err) || !msg.is_object())
  {
    to_agent(error_response(Json(), -32700, "not a JSON object: " + err));
    return;
  }

  const Json* method = msg.find("method");
  const Json* id = msg.find("id");
  const Json* params = msg.find("params");

  if (!method || !method->is_string())
    return;  // an answer; es40-mcp asks the agent nothing
  const std::string& m = method->as_string();

  if (!id)
  {
    const Json* req = params ? params->find("requestId") : nullptr;
    if (m == "notifications/cancelled" && req)
      cancel(*req);
    return;
  }
  if (m == "initialize")
  {
    to_agent(response(*id, initialize_result(params)));
  }
  else if (m == "ping")
  {
    to_agent(response(*id, Json::object()));
  }
  else if (m == "tools/list")
  {
    Json r = Json::object();
    r.set("tools", tools());
    to_agent(response(*id, std::move(r)));
  }
  else if (m == "tools/call")
  {
    call(*id, params);
  }
  else
  {
    to_agent(error_response(*id, -32601, "es40-mcp has no method " + m));
  }
}

void usage(FILE* f)
{
  fprintf(f,
    "usage: es40-mcp [--version]\n"
    "\n"
    "The MCP server for the es40 emulator that an agent such as Claude Code\n"
    "starts, for example with:\n"
    "  claude mcp add es40 -- /path/to/es40-mcp\n"
    "It speaks MCP on stdin and stdout and reaches every es40 that runs with\n"
    "mcp = true in its configuration (or ES40_MCP=1), through the registry\n"
    "folder /tmp/es40-<uid>; ES40_MCP_REGISTRY names another one.\n");
}

} // namespace

int main(int argc, char** argv)
{
  for (int i = 1; i < argc; i++)
  {
    if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help"))
    {
      usage(stdout);
      return 0;
    }
    if (!strcmp(argv[i], "--version"))
    {
      printf("es40-mcp %s\n", PACKAGE_VERSION);
      return 0;
    }
    usage(stderr);
    return 2;
  }
  signal(SIGPIPE, SIG_IGN);

  const char* env = getenv("ES40_MCP_REGISTRY");
  g_registry = env && *env ? std::string(env) : "/tmp/es40-" + std::to_string((unsigned)getuid());
  env = getenv("CLAUDE_CODE_SESSION_ID");
  g_session = env ? env : "";

  std::string in;
  char buf[65536];

  for (;;)
  {
    std::vector<struct pollfd> fds;
    std::vector<int> pids;

    fds.push_back({ STDIN_FILENO, POLLIN, 0 });
    for (const auto& kv : g_conns)
    {
      fds.push_back({ kv.second->fd, POLLIN, 0 });
      pids.push_back(kv.first);
    }

    const int n = poll(fds.data(), (nfds_t)fds.size(), g_waits.empty() && g_conns.empty() ? -1 : kTickMs);
    if (n < 0 && errno != EINTR)
    {
      perror("es40-mcp: poll");
      return 1;
    }
    if (n > 0 && (fds[0].revents & (POLLIN | POLLHUP | POLLERR)))
    {
      const ssize_t r = read(STDIN_FILENO, buf, sizeof(buf));

      if (r == 0 || (r < 0 && errno != EINTR && errno != EAGAIN))
        return 0;  // the agent left
      if (r > 0)
      {
        size_t start = 0, nl;

        in.append(buf, (size_t)r);
        while ((nl = in.find('\n', start)) != std::string::npos)
        {
          handle_agent(in.substr(start, nl - start));
          start = nl + 1;
        }
        in.erase(0, start);
        if (in.size() > 64 * 1024 * 1024)
        {
          fprintf(stderr, "es40-mcp: a message from the agent is too long\n");
          return 1;
        }
      }
    }
    for (size_t i = 0; n > 0 && i < pids.size(); i++)
    {
      if (fds[i + 1].revents)
        read_conn(pids[i]);
    }
    check_conns();
    reap();
    check_waits();
  }
}

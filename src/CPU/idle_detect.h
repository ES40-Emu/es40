/* ES40 emulator -- host-side idle detection methods
 *
 * SPDX-License-Identifier: BSD-1-Clause
 *
 * Optional JIT heuristics for guests that spin instead of issuing WTINT,
 * such as HP OpenVMS Alpha 8.4. Disabled by default; repeated PCs alone do
 * not establish that the guest is idle.
 *
 * The configured OS name selects a heuristic, not an OS identity check.
 * Selecting the wrong name can still trigger sleeps during useful work.
 * With idle_detect="off" and idle_nap=false (both defaults), the shared
 * idle wait returns without sleeping. Other timer pacing is independent.
 *
 * Prior art: SIMH "SET CPU IDLE=VMS" (MIT, Copyright (c) Robert M Supnik).
 * No code copied -- only the approach of testing processor state instead of
 * looking for a particular address.
 */
#if !defined(INCLUDED_IDLE_DETECT_H)
#define INCLUDED_IDLE_DETECT_H

#include <bitset>
#include <cstdint>
#include <cstring>

/* Internal rules. Several guest OSes can share one, and the user never names
 * a rule -- only an OS label, so a rule can be improved without anyone's
 * config changing. Same split as SIMH's os_tab[].
 */
enum EIdleRule
{
  RULE_NONE      = 0,
  RULE_SPIN      = 1,   // spinning within a small set of PCs
  RULE_SPIN_IPL3 = 2,   // ...and VMS PAL PS reports IPL 3 at each qualifying sample
  RULE_SPIN_KERNEL = 3  // ...and the CPU is in kernel mode (IER_CM), outside PALmode
};

struct SIdleMethod
{
  const char*  name;
  int          rule;
  const char*  about;
};

/* One row per guest OS, the way SIMH's os_tab[] works. Historical idle host
 * CPU measurements before interruptible waits, single CPU with the JIT on:
 *
 *   HP OpenVMS Alpha 8.4            101.2% -> 7.5%   vms
 *   Tru64 UNIX 5.1B-4               100.8% -> 9.4%   tru64
 *   NetBSD 10.1/alpha stock GENERIC 100.8% -> 20.7%  netbsd
 *
 * RULE_SPIN cannot distinguish idle loops from repetitive computation and
 * can repeatedly sleep during useful work. netbsd selects it: reduced host
 * CPU use trades off against guest throughput and latency.
 * RULE_SPIN_IPL3 additionally samples the VMS PAL PS layout in p22; it does
 * not prove that an entire dispatch stayed at IPL 3 or did no useful work.
 * RULE_SPIN_KERNEL is for Tru64, which idles in kernel mode at IPL 0. A sample must be in kernel mode, have the IER
 * enables last seen in user mode (always IPL 0 under OSF), and sit in a
 * loop that idle_loop_check accepts. A lock spin at IPL 0 can still pass.
 * Other CPU/PAL combinations need an appropriate state decoder and explicit
 * eligibility before relying on this qualifier. These historical figures
 * do not establish guest clock accuracy or active-workload performance.
 */
static const SIdleMethod idle_methods[] =
{
  { "off",    RULE_NONE,      "no host-side idle detection (default)" },
  { "vms",    RULE_SPIN_IPL3, "OpenVMS" },
  { "tru64",  RULE_SPIN_KERNEL, "Tru64 UNIX" },
  { "netbsd", RULE_SPIN,        "NetBSD/alpha" },
  { 0, 0, 0 }
};

/* Does the loop around a repeating PC poll, rather than compute?
 *
 * An idle loop (Tru64 idle_thread) rereads a few fixed words and writes
 * nothing. The loop is the span from the first branch back to or before
 * pc, to that branch's target. It is rejected if it:
 *  - stores, makes a non-read-only PAL call, or leaves via JMP/RET;
 *  - loads through a register it advances (p += n, p = p->next);
 *  - never branches on a value loaded from a fixed address (counters,
 *    hashing);
 *  - calls a function that no in-loop branch skips (Tru64 random_write).
 * read(va, &insn) returns false if va has no instruction source. On a
 * rejection, *at receives the deciding instruction's address.
 */
enum EIdleLoop
{
  IDLE_LOOP_UNKNOWN = 0,   // not checked yet
  IDLE_LOOP_POLLS,
  IDLE_LOOP_STORES,        // stores, a writing CALL_PAL, HW_ST or leaves via JMP/RET
  IDLE_LOOP_WALKS,         // loads through an address the loop advances
  IDLE_LOOP_COMPUTES,      // no branch tests a polled word
  IDLE_LOOP_CALLS,         // calls a function on every pass
  IDLE_LOOP_UNREADABLE,    // no back edge found, too wide, or not readable
};

static const int kIdleLoopMax = 128;   // instructions scanned for the back edge
static const int kIdleSpanMax = 256;   // instructions in the loop span

// OSF PAL calls that only read state: rdmces, draina, rdval, rdps, rdusp,
// whami, imb, rdunique.
inline bool idle_pal_reads_only(std::uint32_t function)
{
  switch (function)
  {
  case 0x10: case 0x02: case 0x32: case 0x36: case 0x3a: case 0x3c:
  case 0x86: case 0x9e:
    return true;
  default:
    return false;
  }
}

template <class Read>
int idle_loop_check(std::uint64_t pc, Read read, std::uint64_t* at = nullptr)
{
  std::uint64_t lo = 0, hi = 0;
  for (int i = 0; i < kIdleLoopMax && !hi; i++)
  {
    const std::uint64_t va = pc + 4 * (std::uint64_t) i;
    std::uint32_t insn;
    if (!read(va, &insn))
      return IDLE_LOOP_UNREADABLE;
    const std::uint32_t op = insn >> 26;
    const bool branch = op == 0x30 || (op >= 0x31 && op <= 0x33) || op >= 0x35;   // BR, FBcc, Bcc
    if (!branch)
      continue;
    const std::int64_t disp = (std::int64_t) ((std::int32_t) (insn << 11) >> 11) * 4;   // 21-bit, sign-extended
    const std::uint64_t target = va + 4 + (std::uint64_t) disp;
    if (target <= pc)                 // closes the loop containing pc
    {
      if (va - target >= 4 * (std::uint64_t) kIdleSpanMax)
        return IDLE_LOOP_UNREADABLE;
      lo = target;
      hi = va;
    }
  }
  if (!hi)
    return IDLE_LOOP_UNREADABLE;

  // A read sees the last write before it in this pass, else the loop's
  // last write from the previous pass.
  static const int kSpanMax = kIdleSpanMax;
  const int n = (int) ((hi - lo) / 4) + 1;
  std::uint32_t code[kSpanMax];
  int dest[kSpanMax];                  // register written, or -1
  std::uint32_t uses[kSpanMax];        // registers read for the value
  int base[kSpanMax];                  // load address register, or -1
  int tested[kSpanMax];                // register a Bcc tests, or -1
  std::uint64_t skip_to[kSpanMax];     // conditional branch target, or 0
  bool call[kSpanMax];                 // BSR or JSR
  int last_def[32];
  for (int r = 0; r < 32; r++)
    last_def[r] = -1;

  for (int i = 0; i < n; i++)
  {
    const std::uint64_t va = lo + 4 * (std::uint64_t) i;
    if (!read(va, &code[i]))
      return IDLE_LOOP_UNREADABLE;
    const std::uint32_t insn = code[i], op = insn >> 26;
    const std::uint32_t ra = (insn >> 21) & 31, rb = (insn >> 16) & 31, rc = insn & 31;
    dest[i] = -1;
    uses[i] = 0;
    base[i] = -1;
    tested[i] = op >= 0x38 ? (int) ra : -1;      // integer Bcc
    skip_to[i] = 0;
    if (op >= 0x38 || (op >= 0x31 && op <= 0x33) || (op >= 0x35 && op <= 0x37))
      skip_to[i] = va + 4 + (std::uint64_t) ((std::int64_t) ((std::int32_t) (insn << 11) >> 11) * 4);
    call[i] = op == 0x34 || (op == 0x1a && ((insn >> 14) & 3) == 1);
    switch (op)
    {
    case 0x00:                                    // CALL_PAL
      if (idle_pal_reads_only(insn & 0x3ffffff))
      {
        dest[i] = 0;                              // v0 receives the result
        break;
      }
      [[fallthrough]];
    case 0x0d: case 0x0e: case 0x0f:              // STW, STB, STQ_U
    case 0x1f:                                    // HW_ST
    case 0x24: case 0x25: case 0x26: case 0x27:   // STF, STG, STS, STT
    case 0x2c: case 0x2d: case 0x2e: case 0x2f:   // STL, STQ, STL_C, STQ_C
      if (at)
        *at = va;
      return IDLE_LOOP_STORES;
    case 0x1a:                                    // JMP, JSR, RET, JSR_COROUTINE
      if (((insn >> 14) & 3) != 1)                // only JSR, a call, stays in the loop
      {
        if (at)
          *at = va;
        return IDLE_LOOP_STORES;
      }
      break;
    case 0x08: case 0x09:                         // LDA, LDAH: ra = rb + disp
      dest[i] = (int) ra;
      uses[i] = 1u << rb;
      break;
    case 0x0a: case 0x0b: case 0x0c:              // LDBU, LDQ_U, LDWU
    case 0x28: case 0x29: case 0x2a: case 0x2b:   // LDL, LDQ, LDL_L, LDQ_L
      dest[i] = (int) ra;
      uses[i] = 1u << rb;
      base[i] = (int) rb;
      break;
    case 0x20: case 0x21: case 0x22: case 0x23:   // FP loads: FP destination
      base[i] = (int) rb;
      break;
    case 0x10: case 0x11: case 0x12: case 0x13: case 0x1c:   // integer operates
      dest[i] = (int) rc;
      uses[i] = (1u << ra) | ((insn & 0x1000) ? 0 : 1u << rb);
      if (op == 0x11 && ((insn >> 5) & 0x0d) == 0x04)
        uses[i] |= 1u << rc;                      // CMOVxx (function x4/x6) keeps rc when the test fails
      break;
    }
    if (dest[i] == 31)
      dest[i] = -1;
    uses[i] &= 0x7fffffffu;                       // r31 is always zero
    if (base[i] == 31)
      base[i] = -1;
    if (dest[i] >= 0)
      last_def[dest[i]] = i;
  }

  // Every call must be skippable by an earlier branch within the loop.
  for (int c = 0; c < n; c++)
  {
    if (!call[c])
      continue;
    const std::uint64_t cva = lo + 4 * (std::uint64_t) c;
    bool guarded = false;
    for (int b = 0; b < c && !guarded; b++)
      guarded = skip_to[b] > cva && skip_to[b] <= hi;
    if (!guarded)
    {
      if (at)
        *at = cva;
      return IDLE_LOOP_CALLS;
    }
  }

  // reach[i]: definitions that definition i's value derives from.
  std::bitset<kSpanMax> reach[kSpanMax];
  std::bitset<kSpanMax> nodes;
  int cur[32];
  for (int r = 0; r < 32; r++)
    cur[r] = -1;
  int base_def[kSpanMax], tested_def[kSpanMax];
  std::bitset<kSpanMax> loads;
  for (int i = 0; i < n; i++)
  {
    auto def_of = [&](int r) { return cur[r] >= 0 ? cur[r] : last_def[r]; };
    base_def[i] = base[i] >= 0 ? def_of(base[i]) : -1;
    tested_def[i] = tested[i] >= 0 && tested[i] != 31 ? def_of(tested[i]) : -1;
    if (base[i] >= 0 && dest[i] >= 0)
      loads.set(i);
    for (int r = 0; r < 31; r++)
      if ((uses[i] & (1u << r)) && def_of(r) >= 0)
        reach[i].set(def_of(r));
    if (dest[i] >= 0)
    {
      cur[dest[i]] = i;
      nodes.set(i);
    }
  }
  for (int k = 0; k < n; k++)
    for (int i = 0; i < n; i++)
      if (reach[i].test(k))
        reach[i] |= reach[k];

  // Advanced: a definition on a cycle, or derived from one.
  std::bitset<kSpanMax> advanced;
  for (int i = 0; i < n; i++)
    if (nodes.test(i) && reach[i].test(i))
      advanced.set(i);
  for (int i = 0; i < n; i++)
    if (nodes.test(i) && (reach[i] & advanced).any())
      advanced.set(i);

  for (int i = 0; i < n; i++)
    if (base_def[i] >= 0 && advanced.test(base_def[i]))
    {
      if (at)
        *at = lo + 4 * (std::uint64_t) i;
      return IDLE_LOOP_WALKS;
    }

  // Some branch must test a polled value: loaded, and not advanced.
  for (int i = 0; i < n; i++)
  {
    const int d = tested_def[i];
    if (d >= 0 && !advanced.test(d) && (loads.test(d) || (reach[d] & loads).any()))
      return IDLE_LOOP_POLLS;
  }
  if (at)
    *at = hi;
  return IDLE_LOOP_COMPUTES;
}

/* Which RAM does one pass of an accepted idle loop read?
 *
 * Runs the loop once from pc with the current registers r[0..31], following
 * the branches the current memory selects, and collects the 64-byte physical
 * lines its loads touch. Another path through the loop only runs after a
 * value read on this one changes, so watching these lines is enough to see
 * the store that ends the idle state (Tru64 thread_setrun stores
 * processor->next_thread with no IPI).
 * load(va, bytes, &value, &phys) reads guest RAM, zero-extended, and returns
 * false if va has no RAM behind it. Returns the number of lines, or -1 if
 * the pass calls, jumps, uses an unhandled instruction, reads a line it
 * cannot resolve, needs more than max lines, or does not come back to pc.
 * On -1, *at receives the deciding instruction's address.
 */
static const int kIdleTraceSteps = 2 * kIdleSpanMax;

template <class Read, class Load>
int idle_loop_trace(std::uint64_t pc, const std::uint64_t* regs, Read read, Load load,
                    std::uint64_t* lines, int max, std::uint64_t* at = nullptr)
{
  std::uint64_t r[32];
  std::memcpy(r, regs, sizeof r);
  r[31] = 0;
  int n = 0;
  std::uint64_t va = pc;
  auto fail = [&]() { if (at) *at = va; return -1; };
  auto sext32 = [](std::uint64_t v) { return (std::uint64_t) (std::int64_t) (std::int32_t) v; };
  for (int step = 0; step < kIdleTraceSteps; step++)
  {
    std::uint32_t insn;
    if (!read(va, &insn))
      return fail();
    const std::uint32_t op = insn >> 26;
    const std::uint32_t ra = (insn >> 21) & 31, rb = (insn >> 16) & 31, rc = insn & 31;
    const std::uint32_t fn = (insn >> 5) & 0x7f;
    const std::uint64_t a = r[ra];
    const std::uint64_t b = (insn & 0x1000) ? (insn >> 13) & 0xff : r[rb];   // literal form
    const std::int64_t mdisp = (std::int16_t) (insn & 0xffff);
    std::uint64_t next = va + 4;
    int dest = -1;
    std::uint64_t val = 0;
    switch (op)
    {
    case 0x08:                                    // LDA
      dest = (int) ra; val = r[rb] + (std::uint64_t) mdisp;
      break;
    case 0x09:                                    // LDAH
      dest = (int) ra; val = r[rb] + ((std::uint64_t) mdisp << 16);
      break;
    case 0x0a: case 0x0b: case 0x0c: case 0x28: case 0x29:   // LDBU, LDQ_U, LDWU, LDL, LDQ
    {
      if (ra == 31)
        break;                                    // UNOP or prefetch: no architectural read
      const int bytes = op == 0x0a ? 1 : op == 0x0c ? 2 : op == 0x28 ? 4 : 8;
      std::uint64_t ea = r[rb] + (std::uint64_t) mdisp;
      if (op == 0x0b)
        ea &= ~(std::uint64_t) 7;
      if (ea & (std::uint64_t) (bytes - 1))
        return fail();                            // would trap to PAL
      std::uint64_t phys = 0;
      if (!load(ea, bytes, &val, &phys))
        return fail();
      if (op == 0x28)
        val = sext32(val);
      dest = (int) ra;
      const std::uint64_t line = phys >> 6;
      int k = 0;
      while (k < n && lines[k] != line)
        k++;
      if (k == n)
      {
        if (n == max)
          return fail();
        lines[n++] = line;
      }
      break;
    }
    case 0x10:                                    // integer arithmetic
      dest = (int) rc;
      switch (fn)
      {
      case 0x00: case 0x40: val = sext32(a + b); break;            // ADDL(/V)
      case 0x02: val = sext32(a * 4 + b); break;                    // S4ADDL
      case 0x09: case 0x49: val = sext32(a - b); break;            // SUBL(/V)
      case 0x0b: val = sext32(a * 4 - b); break;                    // S4SUBL
      case 0x12: val = sext32(a * 8 + b); break;                    // S8ADDL
      case 0x1b: val = sext32(a * 8 - b); break;                    // S8SUBL
      case 0x1d: val = a < b; break;                                // CMPULT
      case 0x20: case 0x60: val = a + b; break;                    // ADDQ(/V)
      case 0x22: val = a * 4 + b; break;                            // S4ADDQ
      case 0x29: case 0x69: val = a - b; break;                    // SUBQ(/V)
      case 0x2b: val = a * 4 - b; break;                            // S4SUBQ
      case 0x2d: val = a == b; break;                               // CMPEQ
      case 0x32: val = a * 8 + b; break;                            // S8ADDQ
      case 0x3b: val = a * 8 - b; break;                            // S8SUBQ
      case 0x3d: val = a <= b; break;                               // CMPULE
      case 0x4d: val = (std::int64_t) a < (std::int64_t) b; break;  // CMPLT
      case 0x6d: val = (std::int64_t) a <= (std::int64_t) b; break; // CMPLE
      default: return fail();
      }
      break;
    case 0x11:                                    // logical and conditional move
    {
      dest = (int) rc;
      bool cmov = true, take = false;             // CMOVxx: does rc receive b?
      switch (fn)
      {
      case 0x00: cmov = false; val = a & b; break;     // AND
      case 0x08: cmov = false; val = a & ~b; break;    // BIC
      case 0x20: cmov = false; val = a | b; break;     // BIS
      case 0x28: cmov = false; val = a | ~b; break;    // ORNOT
      case 0x40: cmov = false; val = a ^ b; break;     // XOR
      case 0x48: cmov = false; val = a ^ ~b; break;    // EQV
      case 0x14: take = a & 1; break;             // CMOVLBS
      case 0x16: take = !(a & 1); break;          // CMOVLBC
      case 0x24: take = a == 0; break;            // CMOVEQ
      case 0x26: take = a != 0; break;            // CMOVNE
      case 0x44: take = (std::int64_t) a < 0; break;    // CMOVLT
      case 0x46: take = (std::int64_t) a >= 0; break;   // CMOVGE
      case 0x64: take = (std::int64_t) a <= 0; break;   // CMOVLE
      case 0x66: take = (std::int64_t) a > 0; break;    // CMOVGT
      default: return fail();
      }
      if (cmov)
        val = take ? b : r[rc];
      break;
    }
    case 0x12:                                    // shifts, byte manipulation
    {
      dest = (int) rc;
      const unsigned sh = (unsigned) (b & 63), bo = (unsigned) (b & 7) * 8;
      auto zapnot = [](std::uint64_t v, std::uint64_t mask) {
        std::uint64_t m = 0;
        for (int i = 0; i < 8; i++)
          if (mask & (1u << i))
            m |= (std::uint64_t) 0xff << (8 * i);
        return v & m;
      };
      switch (fn)
      {
      case 0x06: val = (a >> bo) & 0xff; break;                     // EXTBL
      case 0x16: val = (a >> bo) & 0xffff; break;                   // EXTWL
      case 0x26: val = (a >> bo) & 0xffffffffu; break;              // EXTLL
      case 0x36: val = a >> bo; break;                              // EXTQL
      case 0x30: val = zapnot(a, ~b & 0xff); break;                 // ZAP
      case 0x31: val = zapnot(a, b & 0xff); break;                  // ZAPNOT
      case 0x34: val = a >> sh; break;                              // SRL
      case 0x39: val = a << sh; break;                              // SLL
      case 0x3c: val = (std::uint64_t) ((std::int64_t) a >> sh); break;  // SRA
      default: return fail();
      }
      break;
    }
    case 0x13:                                    // integer multiply
      dest = (int) rc;
      if (fn == 0x00 || fn == 0x40)               // MULL(/V)
        val = sext32(a * b);
      else if (fn == 0x20 || fn == 0x60)          // MULQ(/V)
        val = a * b;
      else
        return fail();
      break;
    case 0x30:                                    // BR
    case 0x38: case 0x39: case 0x3a: case 0x3b:   // BLBC, BEQ, BLT, BLE
    case 0x3c: case 0x3d: case 0x3e: case 0x3f:   // BLBS, BNE, BGE, BGT
    {
      const std::int64_t s = (std::int64_t) a;
      bool taken;
      switch (op)
      {
      case 0x30: taken = true; dest = (int) ra; val = va + 4; break;
      case 0x38: taken = !(a & 1); break;
      case 0x39: taken = a == 0; break;
      case 0x3a: taken = s < 0; break;
      case 0x3b: taken = s <= 0; break;
      case 0x3c: taken = a & 1; break;
      case 0x3d: taken = a != 0; break;
      case 0x3e: taken = s >= 0; break;
      default:   taken = s > 0; break;
      }
      if (taken)
        next = va + 4 + (std::uint64_t) ((std::int64_t) ((std::int32_t) (insn << 11) >> 11) * 4);
      break;
    }
    default:                                      // calls, jumps, PAL, FP, stores, LDx_L
      return fail();
    }
    if (dest >= 0 && dest != 31)
      r[dest] = val;
    va = next;
    if (va == pc)
      return n;
  }
  return fail();
}

inline int idle_method_by_name(const char* n)
{
  if (!n)
    return RULE_NONE;
  for (int i = 0; idle_methods[i].name; i++)
    if (!strcasecmp(n, idle_methods[i].name))
      return idle_methods[i].rule;
  return -1;   // caller reports the bad name rather than silently idling wrong
}

#endif

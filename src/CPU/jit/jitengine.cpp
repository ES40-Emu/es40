/* ES40 emulator -- JIT engine: the host-independent core.
 * Copyright (C) 2026 by gdwnldsKSC
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
#ifdef ES40_JIT

#ifdef _WIN32
#include "../../config_win32.h"
#endif

#include "jitengine.h"
#include "jitengine_internal.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <chrono>   // note_exec times its own stats-print I/O (excluded from the wall-clock RPCC)
#include <cstdlib>  // abort (fatal block/trace cache allocation failure)
#ifndef ASMJIT_STATIC
#define ASMJIT_STATIC
#endif
// The engine owns the executable-memory allocator (JitRuntime) for whichever backend is
// built; only asmjit's core is needed here -- no instructions are emitted in this file.
#include <asmjit/core.h>
#if defined(ES40_JIT_X64) && defined(_MSC_VER)
#include <intrin.h>
#endif

// ---- big-table allocation: prefer large/huge pages for the randomly-indexed caches ----
// The block cache (~40 MB/CPU) is indexed by PC hash, so its accesses are TLB-hostile with 4K
// pages. Windows: MEM_LARGE_PAGES needs SeLockMemoryPrivilege ("Lock pages in memory" assigned
// to the account); enabling it on the token is best-effort and allocation falls back to normal 
// pages. 
// 
// Linux: MADV_HUGEPAGE (THP) is best-effort and needs no privilege. 
// 
// All paths return zeroed memory.
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <sys/mman.h>
#endif

static void* big_alloc(size_t bytes, bool* large)
{
  *large = false;
#ifdef _WIN32
  static bool priv_tried = false;
  if (!priv_tried) {
    priv_tried = true;
    HANDLE tok;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &tok)) {
      TOKEN_PRIVILEGES tp = {}; tp.PrivilegeCount = 1;
      if (LookupPrivilegeValueW(nullptr, L"SeLockMemoryPrivilege", &tp.Privileges[0].Luid)) {
        tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
        AdjustTokenPrivileges(tok, FALSE, &tp, 0, nullptr, nullptr);
      }
      CloseHandle(tok);
    }
  }
  const size_t lp = GetLargePageMinimum();
  if (lp) {
    const size_t rounded = (bytes + lp - 1) & ~(lp - 1);
    void* p = VirtualAlloc(nullptr, rounded, MEM_RESERVE | MEM_COMMIT | MEM_LARGE_PAGES, PAGE_READWRITE);
    if (p) { *large = true; return p; }
  }
  return VirtualAlloc(nullptr, bytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
#else
  void* p = mmap(nullptr, bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (p == MAP_FAILED) return nullptr;
#ifdef MADV_HUGEPAGE
  if (madvise(p, bytes, MADV_HUGEPAGE) == 0) *large = true;   // THP hint; kernel may still decline
#endif
  return p;
#endif
}

static void big_free(void* p, size_t bytes)
{
  if (!p) return;
#ifdef _WIN32
  (void) bytes;
  VirtualFree(p, 0, MEM_RELEASE);
#else
  munmap(p, bytes);
#endif
}

// One 64 MB block keeps all code within rel32 reach of the patched exits.
static asmjit::JitAllocator::CreateParams jit_rt_params()
{
  asmjit::JitAllocator::CreateParams p{};
  p.block_size = 64u << 20;
  return p;
}

CJitEngine::CJitEngine(const AlphaCPUProfile& profile, int cpu_id)
  : m_profile(profile), m_cpu_id(cpu_id), m_recorded(0), m_code_bytes(0), m_rt(nullptr)
{
  // flush() is lazy (gen bump), so the slots must start zeroed -- all big_alloc paths return
  // zeroed pages. Compiled code embeds absolute addresses into m_blocks..
  bool blk_lp = false, trc_lp = false;
  m_blocks = (JitBlock*) big_alloc((size_t) kCacheEntries * sizeof(JitBlock), &blk_lp);
  m_traces = (TraceFragment*) big_alloc((size_t) kTraceEntries * sizeof(TraceFragment), &trc_lp);
  m_set_rr = (uint8_t*) calloc((size_t) kSets, 1);   // 64 KB; not worth a large-page reservation
  if (!m_blocks || !m_traces || !m_set_rr) {
    fprintf(stderr, "[JIT][CPU%d] FATAL: block/trace cache allocation failed\n", m_cpu_id);
    abort();
  }
  printf("[JIT][CPU%d] block cache %zu MB (%s pages, %d sets x %d ways), trace cache %zu MB (%s pages)\n", m_cpu_id,
         ((size_t) kCacheEntries * sizeof(JitBlock)) >> 20, blk_lp ? "large" : "normal", kSets, kWays,
         ((size_t) kTraceEntries * sizeof(TraceFragment)) >> 20, trc_lp ? "large" : "normal");
  // Trace tier kill-switch (config_debug.h JIT_TRACES). OFF by default, 1-block traces preempt block
  // chaining = a net loss; re-enable when fusion closes loops in-trace.
#ifdef JIT_TRACES
  m_traces_enabled = true;
#else
  m_traces_enabled = false;
#endif
  m_rt = new asmjit::JitRuntime(jit_rt_params());
  build_trampolines();
#ifdef JIT_VERIFY
  m_v_exec = m_v_fail = 0;
  m_tv_cnt[0] = m_tv_cnt[1] = m_tv_cnt[2] = m_tv_cnt[3] = 0;
#endif
#ifdef JIT_STATS
  m_stat_native = m_stat_interp = m_stat_hot = m_stat_miss = 0;
  m_stat_compiled = m_stat_plen_sum = m_stat_code_bytes = 0;
  m_stat_wall_last_ns = (uint64_t) std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count();
  m_tsc_compiled = m_tsc_interp = m_tsc_resolver = 0;
  m_tsc_window_start = jit_rdtsc();
  m_bail_link = m_jmp_attempt = m_jmp_hit = 0;
  m_fresh_cold = m_fresh_tag = m_fresh_asn = m_fresh_phys = m_fresh_hash = 0;
  m_trace_formed = m_trace_entered = m_trace_exits = m_trace_stale = 0;
  m_licm_same = m_licm_diff = 0; m_licm_next = 0;
  memset(m_licm_pool, 0, sizeof(m_licm_pool));
  m_link_invalidations = m_links_cleared = m_asn_link_clears = m_asn_links_cleared = 0;
  m_evict_link_clears = m_evict_links_cleared = m_reclaims = m_jmp_exits = m_jc_hits = m_mb_exec = 0;
  memset(m_exit_kind, 0, sizeof(m_exit_kind));
  memset(m_bump_cause, 0, sizeof(m_bump_cause));
  m_direct_patches = m_patch_unreachable = 0;
  m_page_clears = m_page_links_cleared = 0;
  memset(m_rt_break_op, 0, sizeof(m_rt_break_op));
  memset(m_term_op, 0, sizeof(m_term_op));
  memset(m_pal_func, 0, sizeof(m_pal_func));
  memset(m_mtpr_encoding_hi, 0, sizeof(m_mtpr_encoding_hi));
  memset(m_hwld_func, 0, sizeof(m_hwld_func));
  memset(m_misc_func, 0, sizeof(m_misc_func));
  m_first_breaker_logged = false;
#endif
#ifdef JIT_DISASM
  char name[64];
  snprintf(name, sizeof name, "jit_disasm_cpu%d.txt", m_cpu_id);
  m_disasm_fp = fopen(name, "w");
  if (!m_disasm_fp)
    fprintf(stderr, "[JIT][CPU%d] could not open %s for the disassembly trace\n", m_cpu_id, name);
#endif
#ifdef JIT_VERIFY
  if (cpu_id == 0) trace_selftest();   // M0: validate trace_ok's source-coherence once (SMC/IMB/remap)
#endif
}

CJitEngine::~CJitEngine()
{
  delete (asmjit::JitRuntime*) m_rt;   // frees all compiled code (which references m_blocks) first
  big_free(m_blocks, (size_t) kCacheEntries * sizeof(JitBlock));
  free(m_set_rr);
  big_free(m_traces, (size_t) kTraceEntries * sizeof(TraceFragment));
#ifdef JIT_DISASM
  if (m_disasm_fp) fclose(m_disasm_fp);
#endif
}

CJitEngine::JitBlock* CJitEngine::record(uint64_t virt_pc, uint64_t phys_pc, uint32_t asn,
                                         bool pal_shadow, bool asm_global, uint32_t n_instr,
                                         const AlphaInstructionSource& sources)
{
  JitBlock* const set = set_base(virt_pc);

  // Set-associative: a second process running the/ same virtual PC now takes its 
  // OWN way instead of evicting the first one's compiled block.
  JitBlock* hit = nullptr;
  for (int w = 0; w < kWays; ++w)
    if (way_keyed(set[w], virt_pc, asn, pal_shadow)) { hit = &set[w]; break; }

  if (hit) {
    JitBlock& b = *hit;
    // record() is only reached after the dispatcher validated the live physical, and every returning
    // branch below verifies the code bytes (flush-fresh or hash), so stamp the chain epoch here.
    b.vgen = m_vgen_cur;
    // Still valid + flush-fresh: nothing flushed us since last seen, so the code is unchanged.
    if (b.valid && b.flush_gen == flush_generation(b.asm_global) && b.phys == phys_pc
        && b.asm_global == asm_global) {
      b.n_instr = n_instr;
      return &b;
    }
    // Revalidate: a flush dropped the block but kept the compiled code. If the bytes the prefix was
    // compiled from still hash the same, reuse it instead of recompiling. Hash over hash_len, NOT
    // the caller's n_instr -- interrupt-truncated cold passes make n vary for the same block.
    if (b.code && b.phys == phys_pc) {
      const auto source = sources.view(virt_pc);
      if (source.identity == phys_pc && source.asm_global == asm_global
          && source.asm_global == b.asm_global && source.contains(b.hash_len)
          && b.src_sum == src_hash(source.bytes, b.hash_len)) {
        b.valid = true;
        b.flush_gen = flush_generation(b.asm_global);
        b.n_instr = n_instr;
        b.jit_body = (void*) ((uint8_t*) (void*) b.code + b.body_off);   // restore chained re-entry
        return &b;
      }
    }
  }

  // New block, page remap, or modified bytes: record fresh and force a recompile. Reuse the keyed
  // way when we have one (same block, changed source); otherwise take a free way, evict on full.
  JitBlock* victim = hit;
  if (!victim) {
    for (int w = 0; w < kWays; ++w)
      if (!set[w].valid && !set[w].code) { victim = &set[w]; break; }
    if (!victim) {
      uint8_t& rr = m_set_rr[set_of(virt_pc)];
      victim = &set[rr];
      rr = (uint8_t) ((rr + 1) & (kWays - 1));
    }
  }
  // Old code still uses &link[]. If either block is global or the ASN changes,
  // make the old code unreachable before reusing the array. Otherwise old code
  // could follow the replacement's successors.
  if (victim->code && (victim->asm_global || asm_global || victim->asn != asn)) {
#ifdef JIT_STATS
    m_evict_link_clears++;
#endif
    if (m_traces_enabled) invalidate_links(kBumpEvict);   // trace code is not indexed by target page
    else unlink_block(*victim);
  }
  JitBlock& b = *victim;
  b.vgen = m_vgen_cur;
#ifdef JIT_STATS
  // Why is this a FRESH record? With ways, "asn" no longer means "the index can't tell processes
  // apart" -- it means the set ran OUT of ways for this PC, i.e. kWays is too small. "tag" is plain
  // capacity/conflict pressure on the set (the cache-size lever).
  if (hit) {
    if (b.phys != phys_pc)                m_fresh_phys++;   // page remap
    else                                  m_fresh_hash++;   // source bytes changed (self-modifying code)
  } else if (!b.code && !b.valid)         m_fresh_cold++;   // empty / reclaimed way (genuine cold or warmup)
  else {
    bool same_pc_other_asn = false;       // some way holds this PC under a different ASN -> ways exhausted
    for (int w = 0; w < kWays; ++w)
      if (set[w].code && set[w].tag == virt_pc) { same_pc_other_asn = true; break; }
    if (same_pc_other_asn) m_fresh_asn++; else m_fresh_tag++;
  }
#endif
  b.tag = virt_pc;
  b.phys = phys_pc;
  b.asn = asn;
  b.asm_global = asm_global;
  b.pal_shadow = pal_shadow;
  b.n_instr = n_instr;
  b.valid = true;
  b.flush_gen = flush_generation(b.asm_global);
  b.code = nullptr;
  b.jit_body = nullptr;   // not compiled yet -> cached links to us must miss until compile
  b.jit_body_same_page = nullptr;
  b.exec_proof = nullptr;
  for (int i = 0; i < kLinkSlots; ++i) {
    b.link[i].tag = ~uint64_t(0); b.link[i].vgen = 0; b.link[i].body = nullptr;
  }   // no cached successors yet
#ifdef JIT_STATS
  b.link_misses = 0; b.link_fanout = 0;   // instrumentation: reset successor-fanout tracking on (re)use
#endif
  b.prefix_len = 0;
  b.compiled = false;
  b.compile_encounters = 0; // eviction, remap or a flush without reusable code starts admission over
  b.hot = 0;              // fresh block: restart the trace-promotion counter 
#ifdef JIT_REGPROF
  b.rp_hits = 0;          // fresh block: restart the exec counter (resurrect/revalidate keep theirs)
#endif
  if (++m_recorded == 50000)
    printf("[JIT][CPU%d] block dispatcher active: 50000 blocks discovered.\n", m_cpu_id);
  return &b;
}

// Lazy-flush survivor: the dispatcher calls this on a lookup miss, with the LIVE physical it just
// translated. If the slot matches and its source bytes still hash the same, restamp and return it
// straight to the hot path 
CJitEngine::JitBlock* CJitEngine::revalidate_flushed(uint64_t virt_pc, uint32_t asn,
                                                      bool pal_shadow, uint64_t phys_pc,
                                                      const AlphaInstructionSource& sources)
{
  // Both flush kinds retain compiled code. Check its mapping and source hash
  // before refreshing the appropriate generation.
  JitBlock* keyed = nullptr;
  JitBlock* const set = set_base(virt_pc);
  for (int w = 0; w < kWays; ++w)
    if (set[w].code && set[w].tag == virt_pc && (set[w].asm_global || set[w].asn == asn)
        && (!(virt_pc & 1) || set[w].pal_shadow == pal_shadow))
      { keyed = &set[w]; break; }
  if (!keyed)
    return nullptr;
  JitBlock& b = *keyed;
  if (b.phys != phys_pc)
    return nullptr;
  const auto source = sources.view(virt_pc);
  if (source.identity != phys_pc || source.asm_global != b.asm_global
      || !source.contains(b.hash_len) || b.src_sum != src_hash(source.bytes, b.hash_len))
    return nullptr;
  b.valid = true;
  b.flush_gen = flush_generation(b.asm_global);
  b.vgen = m_vgen_cur;   // phys + code bytes just validated
  b.jit_body = (void*) ((uint8_t*) (void*) b.code + b.body_off);
  return &b;
}

// Trace tier - is a looked-up trace safe to enter? (review: per-segment source validation.)
// head_live_phys is the head's freshly resolved physical. The steps mirror the block dispatcher's phys
// check (jit_run ~line 832) + revalidate_flushed's hash:
//   1. head remap / ASN-recycle: the head's live physical no longer matches what we built from -> stale.
//   2. source identity/global status/extent still match; a fresh epoch can skip hashing.
//   3. epoch changed -> re-hash every fused segment. Unchanged bytes => the epoch bumped for an unrelated
//      reason (e.g. an IMB on another page): keep the trace + re-stamp. Changed bytes (SMC/remap): stale.
// Every source is queried by its full guest PC, including interior segments whose bytes
// may remain identical after a remap. Do not retain a borrowed view across another query.
bool CJitEngine::trace_ok(TraceFragment* t, uint64_t head_live_phys,
                          const AlphaInstructionSource& sources)
{
  if (t->n_segs == 0 || t->n_segs > kMaxTraceSegs || t->segs[0].phys_pc != head_live_phys)
    { note_trace_stale(); return false; }             // head remap / ASN recycle
  // A 1-block trace mirrors its head block's compiled prefix, whose length can change with NO source-byte
  // or epoch change: a fault-truncated cold record shrinks prefix_len (n_instr oscillates), a later clean
  // record regrows it invisibly to the hash below. If the live head block compiled a different length,
  // the trace is stale; drop it so the dispatcher re-forms a consistent one.
  for (uint32_t s = 0; s < t->n_segs; ++s) {   // ANY fused block's prefix_len can oscillate with no source/epoch change (not just the head)
    // Set-associative: find the segment's OWN way (its ASN), not just whatever shares the set.
    const SourceSeg& sg = t->segs[s];
    const JitBlock* const set = set_base(sg.guest_pc);
    for (int w = 0; w < kWays; ++w) {
      const JitBlock& sb = set[w];
      if (!(sb.valid && sb.tag == sg.guest_pc && (sb.asm_global || sb.asn == sg.asn))) continue;
      if (sb.prefix_len != sg.n_instr) { note_trace_stale(); return false; }
      break;
    }
  }
  const bool epoch_fresh = t->vgen == m_vgen_cur;
  for (uint32_t i = 0; i < t->n_segs; ++i) {
    const SourceSeg& s = t->segs[i];
    const auto source = sources.view(s.guest_pc);
    if (source.identity != s.phys_pc || source.asm_global != s.asm_global
        || !source.contains(s.n_instr)
        || (!epoch_fresh && s.src_sum != src_hash(source.bytes, s.n_instr)))
      { note_trace_stale(); return false; }            // unavailable, remapped or changed source
  }
  if (epoch_fresh) return true;                        // no source-byte hash on a fresh epoch
  t->vgen = m_vgen_cur;                               // all segments re-validated: re-stamp the epoch
  t->flush_gen = m_flush_gen;
  return true;
}

#ifdef JIT_VERIFY
// unit-test trace_ok's source-coherence decision (SMC/IMB, ITB-remap, head-remap, multi-segment) 
// WITHOUT real traces or the emitter. Mutates m_itb_gen/m_flush_gen but saves/restores; runs 
// once at ctor when the engine is fresh (counters 0, no live blocks). A FAIL here means the
// trace tier's coherence is broken 
void CJitEngine::trace_selftest()
{
  const uint64_t save_itb = m_itb_gen, save_flush = m_flush_gen, save_vgen = m_vgen_cur;
  uint32_t mem[8] = { 0x11111111, 0x22222222, 0x33333333, 0x44444444,
                      0x55555555, 0x66666666, 0x77777777, 0x88888888 };
  const uint8_t* d = (const uint8_t*) mem;
  struct TestSource {
    const uint8_t* bytes;
    uint32_t word_limit = 8;
    uint64_t unavailable_pc = 0;
    uint64_t remapped_pc = 0;
    bool asm_global = false;
  } context{d};
  const AlphaInstructionSource sources{&context, [](void* opaque, uint64_t pc) {
    const auto& test = *static_cast<const TestSource*>(opaque);
    if (pc < 0x2000 || pc >= 0x2020 || pc == test.unavailable_pc)
      return AlphaInstructionSourceView{};
    const uint64_t offset = pc - 0x2000;
    const uint32_t remaining = static_cast<uint32_t>((32 - offset) / 4);
    const uint32_t count = remaining < test.word_limit ? remaining : test.word_limit;
    return AlphaInstructionSourceView{test.bytes + offset,
      offset + (pc == test.remapped_pc ? 0x1000 : 0), count, test.asm_global};
  }};

  TraceFragment t = {};
  t.valid = true; t.head_tag = 0x2000; t.asn = 1; t.n_segs = 2;
  t.segs[0] = { 0x2000, 0,  4, false, 1, src_hash(d + 0,  4) };   // words[0..3] at phys 0
  t.segs[1] = { 0x2010, 16, 4, false, 1, src_hash(d + 16, 4) };   // words[4..7] at phys 16
  t.vgen = m_vgen_cur;

  bool ok = true;
  ok &= ( trace_ok(&t, 0,   sources) == true  ); // 1. fresh: epoch + source metadata match -> enter
  ok &= ( trace_ok(&t, 999, sources) == false ); // 2. head remap / ASN recycle -> drop
  note_itb_invalidate();                       // 3. ITB-invalidate, bytes unchanged:
  ok &= ( trace_ok(&t, 0, sources) == true  ); //    epoch bumps, re-hash matches -> keep ...
  ok &= ( t.vgen == m_vgen_cur );              //    ... and re-stamped to the new epoch
  mem[5] = 0xDEADBEEF;                         // 4. SMC on interior seg1 + IMB (flush bump):
  ++m_flush_gen; ++m_vgen_cur;                 //    (flush()'s bump, minus its reclaim bookkeeping)
  ok &= ( trace_ok(&t, 0, sources) == false ); //    re-hash mismatch -> drop
  mem[5] = 0x66666666;                         // 5. restore the byte, epoch still bumped:
  ok &= ( trace_ok(&t, 0, sources) == true  ); //    re-hash matches again -> keep

  context.unavailable_pc = 0x2010;             // Missing interior source, even on a fresh epoch.
  ok &= !trace_ok(&t, 0, sources);
  context.unavailable_pc = 0;
  context.word_limit = 3;                     // Frozen hash extent must fit the current view.
  ok &= !trace_ok(&t, 0, sources);
  context.word_limit = 8;
  context.remapped_pc = 0x2010;               // Same bytes at a different source identity.
  ok &= !trace_ok(&t, 0, sources);
  context.remapped_pc = 0;
  context.asm_global = true;                  // Global/process-specific sources are distinct.
  ok &= !trace_ok(&t, 0, sources);
  context.asm_global = false;
  ok &= !trace_ok(&t, 0, AlphaInstructionSource{});
  uint8_t unaligned[sizeof(mem) + 1];
  memcpy(unaligned + 1, mem, sizeof(mem));
  context.bytes = unaligned + 1;
  ++m_vgen_cur;
  ok &= trace_ok(&t, 0, sources);              // Byte-safe hashing accepts unaligned source storage.

  printf("[JIT][CPU%d] trace_ok self-test (SMC/IMB/remap/bounded sources): %s\n",
         m_cpu_id, ok ? "PASS" : "*** FAIL ***");
  m_itb_gen = save_itb; m_flush_gen = save_flush; m_vgen_cur = save_vgen;
}
#endif

// Free ALL compiled code (delete+new of the runtime; reset()-and-reuse corrupted the JitAllocator
// block tree) and drop every slot's now-dangling pointers. Safe only from this CPU's cold path
// (never while its compiled code could be executing); runtimes are per-CPU.
void CJitEngine::reclaim_code()
{
  invalidate_links(kBumpReclaim);
#ifdef JIT_STATS
  m_reclaims++;
  printf("[JIT][STATS][CPU%d] code reclaim #%llu: %llu MB freed (every block recompiles)\n", m_cpu_id,
         (unsigned long long) m_reclaims, (unsigned long long) (m_code_bytes >> 20));
#endif
  delete (asmjit::JitRuntime*) m_rt;
  m_rt = new asmjit::JitRuntime(jit_rt_params());
  build_trampolines();
  m_code_bytes = 0;
  m_reclaim_pending = false;   // a reclaim (cold-path or deferred) satisfies any pending request
  for (int i = 0; i < kCacheEntries; ++i) {
    m_blocks[i].valid = false;
    m_blocks[i].code = nullptr;
    m_blocks[i].jit_body = nullptr;
    m_blocks[i].jit_body_same_page = nullptr;
    m_blocks[i].exec_proof = nullptr;
    m_blocks[i].compiled = false;
    // Link snapshots hold raw body pointers into the runtime just freed. The epoch alone can't be
    // trusted.
    for (int sl = 0; sl < kLinkSlots; ++sl) {
      m_blocks[i].link[sl].tag = ~uint64_t(0);
      m_blocks[i].link[sl].vgen = 0;
      m_blocks[i].link[sl].body = nullptr;
    }
  }
  // Traces hold JitFns into the runtime we just deleted -- drop them too, or a post-reclaim trace
  // dispatch jumps through a freed pointer. trace_lookup keys on valid, so clearing it is enough.
  for (int i = 0; i < kTraceEntries; ++i) m_traces[i].valid = false;
}

void CJitEngine::flush()
{
  // Stale blocks miss lookup() and revalidate their source bytes on next use.
  ++m_flush_gen;
  ++m_non_global_flush_gen;
  ++m_vgen_cur;
  invalidate_links(kBumpFlush);
  if (m_rt && m_code_bytes >= kReclaimBytes)
    m_reclaim_pending = true;   // DEFER: reclaim frees all code -- unsafe from a compiled IC_FLUSH;
                                // reclaim_if_pending() does it at the next dispatch boundary.
}

// ASM-bit-clear icache flush: retain global blocks and lazily revalidate the rest.
void CJitEngine::flush_non_global()
{
  if (m_rt && m_code_bytes >= kReclaimBytes) { flush(); return; }
  ++m_non_global_flush_gen;
  ++m_vgen_cur;
  // Links hold raw entries; clear them before stale blocks can be entered again.
  invalidate_links(kBumpFlushAsm);
  // As with a full flush, trace_ok() checks every segment after the epoch change.
}

// Retargets a direct exit; false beyond rel32 reach. Same-thread write only.
static bool write_site(CJitEngine::PatchSite* s, const uint8_t* target)
{
#if defined(ES40_JIT_A64)
  // `b imm26`: +-128 MB, word aligned. The scope handles W^X hosts and the I-cache flush.
  const int64_t rel = (int64_t) (target - s->site);
  if ((rel & 3) != 0 || rel < -(int64_t(1) << 27) || rel >= (int64_t(1) << 27)) return false;
  const uint32_t ins = 0x14000000u | ((uint32_t) (rel >> 2) & 0x03ffffffu);
  {
    asmjit::VirtMem::ProtectJitReadWriteScope scope(s->site, sizeof(ins));
    memcpy(s->site, &ins, sizeof(ins));
  }
  return true;
#else
  const int64_t rel = (int64_t) (target - (s->site + 5));
  if (rel != (int64_t) (int32_t) rel) return false;
  const int32_t r32 = (int32_t) rel;
  memcpy(s->site + 1, &r32, sizeof(r32));
  return true;
#endif
}

// Where a direct exit currently jumps (its stub while unpatched).
static const uint8_t* site_target(const CJitEngine::PatchSite* s)
{
#if defined(ES40_JIT_A64)
  uint32_t ins;
  memcpy(&ins, s->site, sizeof(ins));
  return s->site + (int64_t) ((int32_t) (ins << 6) >> 6) * 4;   // sign-extended imm26
#else
  int32_t r32;
  memcpy(&r32, s->site + 1, sizeof(r32));
  return s->site + 5 + r32;
#endif
}

#if defined(ES40_JIT_X64)
void CJitEngine::serialize_code()
{
#if defined(_MSC_VER)
  int regs[4];
  _ReadWriteBarrier();
  __cpuid(regs, 0);
  _ReadWriteBarrier();
#else
  uint32_t eax, ebx, ecx, edx;
  __asm__ __volatile__("cpuid"
      : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
      : "a"(0u), "c"(0u) : "memory");
#endif
}
#endif

void CJitEngine::authorize_native(JitBlock* block, uint32_t mode)
{
  if ((block->tag & 1) || !block->exec_proof) return;
  NativeExecProof* const proof = block->exec_proof;
  const uint32_t mode_bit = uint32_t{1} << (mode & 3);
  if (proof->epoch == m_exec_epoch && (proof->mode_mask & mode_bit)) return;
  // Invalidation retires incoming links; callers recheck the live mapping.
  const uint32_t mode_mask =
      (proof->epoch == m_exec_epoch ? proof->mode_mask : 0) | mode_bit;
#if defined(ES40_JIT_X64)
  // The engine has one executing owner. Use the writable alias for this allocation.
  asmjit::JitAllocator::Span span;
  auto& allocator = static_cast<asmjit::JitRuntime*>(m_rt)->allocator();
  if (!block->code || allocator.query(asmjit::Out(span),
      reinterpret_cast<void*>(block->code)) != asmjit::Error::kOk || !span.rw())
    abort();
  const size_t proof_offset = static_cast<uint8_t*>(static_cast<void*>(proof))
      - static_cast<uint8_t*>(span.rx());
  const uint32_t back = proof->guard_imm_back;
  if (!back || back > proof_offset || proof_offset > span.size()
      || span.size() - proof_offset < sizeof(*proof))
    abort();
  const size_t immediate_offset = proof_offset - back;
  auto* const writable_proof = reinterpret_cast<NativeExecProof*>(
      static_cast<uint8_t*>(span.rw()) + proof_offset);
  auto* const immediate = static_cast<uint8_t*>(span.rw()) + immediate_offset;
  const bool patch = proof->mode_mask != mode_mask;
  {
    asmjit::VirtMem::ProtectJitReadWriteScope scope(
        static_cast<uint8_t*>(span.rx()) + immediate_offset, back + sizeof(*proof));
    writable_proof->epoch = m_exec_epoch;
    writable_proof->mode_mask = mode_mask;
    if (patch) *immediate = static_cast<uint8_t>(mode_mask);
  }
  // The W^X scope has ended. CPUID serializes instruction fetch before any re-entry.
  if (patch) serialize_code();
#else
  // The permission mask is stored in JIT memory, so writes need the W^X scope.
  asmjit::VirtMem::ProtectJitReadWriteScope scope(proof, sizeof(*proof));
  proof->epoch = m_exec_epoch;
  proof->mode_mask = mode_mask;
#endif
}

void CJitEngine::clear_edge(void* key)
{
  if ((uintptr_t) key & 2) {
    PatchSite* s = (PatchSite*) ((uintptr_t) key & ~(uintptr_t) 3);
    (void) write_site(s, s->stub);            // stub is in the same block
  } else {
    LinkSlot* links = (LinkSlot*) key;
    for (int sl = 0; sl < kLinkSlots; ++sl) {
      links[sl].tag = ~uint64_t(0);
      links[sl].vgen = 0;
      links[sl].body = nullptr;
    }
  }
}

void CJitEngine::patch_edge(void* tagged, uint64_t tag, uint64_t vgen_bits, void* body,
                            bool target_global, void* same_page_body)
{
  const uintptr_t raw = (uintptr_t) tagged;
  const bool global_src = (raw & 1) == 0;
  void* const key = (void*) (raw & ~(uintptr_t) 1);   // keeps the kind bit
  bool register_sets = true;
  if (raw & 2) {
    PatchSite* s = (PatchSite*) (raw & ~(uintptr_t) 3);
    if (s->tag != tag) return;                        // a site resolves exactly one static PC
    // Native branches leave CM and the instruction mapping unchanged.
    // Same-page targets can reuse the source's access check. Compare the full
    // VA here; all other targets use the guarded entry.
    void* const direct_body = same_page_body && !((s->source_tag | tag) & 1)
        && ((s->source_tag ^ tag) & ~UINT64_C(0x1fff)) == 0 ? same_page_body : body;
    if (!write_site(s, (const uint8_t*) direct_body)) {
#ifdef JIT_STATS
      m_patch_unreachable++;
#endif
      return;
    }
#ifdef JIT_STATS
    m_direct_patches++;
#endif
  } else {
    // Refresh the keyed slot, else fill an empty one, else shift.
    LinkSlot* lf = (LinkSlot*) key;
    const LinkSlot snap = { tag, vgen_bits, body };
    int found = -1, empty = -1;
    bool was_live = false;
    for (int i = 0; i < kLinkSlots; ++i)
      if (lf[i].body) { was_live = true; if (lf[i].tag == tag) found = i; }
      else if (empty < 0) empty = i;
    if (found >= 0) {
      lf[found] = snap;                            // already active and indexed by target page
      if (global_src && !target_global) m_global_src_links.add(key);
      return;
    }
    if (empty >= 0) lf[empty] = snap;
    else { for (int i = kLinkSlots - 1; i > 0; --i) lf[i] = lf[i - 1]; lf[0] = snap; }
    register_sets = !was_live;                       // a live array is already active
  }
  if (register_sets) m_active_links.add(key);
  // An already-live global-source array may acquire its first private target here.
  if (global_src && !target_global) m_global_src_links.add(key);
  note_edge_page(translation_page_key(tag), key);
}

void CJitEngine::note_edge_page(uint64_t page, void* key)
{
  bool inserted;
  int32_t& index = m_edge_entries.get(EdgeKey{ page, key }, inserted);
  if (inserted) {
    index = (int32_t) m_edges.size();
    m_edges.push_back(EdgeEntry{ key, -1, false });
  }
  EdgeEntry& e = m_edges[index];
  if (e.live) return;
  int32_t& head = m_page_edges.get(page, inserted);   // -1 when new or cleared
  e.live = true;
  e.next = head;
  head = index;
}

void CJitEngine::invalidate_links_page(uint64_t virt_page)
{
  ++m_exec_epoch;   // reauthorized targets discard their previous mode bits
  const uint64_t page = translation_page_key(virt_page);
  for (int i = 0; i < kJumpCacheEntries; ++i)
    if (m_jcache[i].body && translation_page_key(m_jcache[i].tag) == page)
      m_jcache[i].body = nullptr, m_jcache[i].tag = ~uint64_t(0);
  int32_t* head = m_page_edges.find(page);
  if (!head || *head < 0) return;
#ifdef JIT_STATS
  m_page_clears++;
#endif
  for (int32_t i = *head; i >= 0; i = m_edges[i].next) {
    m_edges[i].live = false;
    clear_edge(m_edges[i].edge);   // a cleared edge re-registers when re-patched
#ifdef JIT_STATS
    m_page_links_cleared++;
#endif
  }
  *head = -1;
}

// Every edge into a block's code is indexed under its tag's page, and the jump cache can
// hold the tag in one slot only. Clearing those leaves the code unreachable; it stays
// allocated until the next reclaim, so edges out of it remain safe to clear later.
void CJitEngine::unlink_block(const JitBlock& v)
{
  const void* const body = (const uint8_t*) (const void*) v.code + v.body_off;
  const void* const same_page = v.jit_body_same_page;
  auto into_v = [&](const void* p) { return p && (p == body || p == same_page); };
  JumpCacheEntry& jc = m_jcache[jcache_index(v.tag)];
  if (into_v(jc.body)) jc.body = nullptr, jc.tag = ~uint64_t(0);
  const int32_t* head = m_page_edges.find(translation_page_key(v.tag));
  if (!head) return;
  for (int32_t i = *head; i >= 0; i = m_edges[i].next) {
    void* const key = m_edges[i].edge;
    if ((uintptr_t) key & 2) {
      PatchSite* s = (PatchSite*) ((uintptr_t) key & ~(uintptr_t) 3);
      if (!into_v(site_target(s))) continue;
      (void) write_site(s, s->stub);
#ifdef JIT_STATS
      m_evict_links_cleared++;
#endif
    } else {
      LinkSlot* links = (LinkSlot*) key;
      for (int sl = 0; sl < kLinkSlots; ++sl) {
        if (!into_v(links[sl].body)) continue;
        links[sl].tag = ~uint64_t(0);
        links[sl].vgen = 0;
        links[sl].body = nullptr;
#ifdef JIT_STATS
        m_evict_links_cleared++;
#endif
      }
    }
  }
}

void CJitEngine::invalidate_links(int cause)
{
  ++m_exec_epoch;
#ifdef JIT_STATS
  m_link_invalidations++;
  m_bump_cause[cause]++;
  m_links_cleared += m_active_links.size();
#else
  (void) cause;
#endif
  ++m_jc_gen;
  m_active_links.for_each(clear_edge);
  m_active_links.clear();
  m_global_src_links.clear();
  m_page_edges.clear();
  m_edge_entries.clear();
  m_edges.clear();
}

// Native entry guards check CM. Traces have no such check, so their links
// still need to be cleared on a mode change.
void CJitEngine::invalidate_links_mode()
{
  if (m_traces_enabled) {
    invalidate_links(kBumpMode);
    return;
  }
#ifdef JIT_STATS
  m_link_invalidations++;
  m_bump_cause[kBumpMode]++;
#endif
}

// ASN switch: global-source edges only; they stay registered for the next epoch bump.
void CJitEngine::invalidate_links_global()
{
  ++m_exec_epoch;   // reauthorized targets discard mode bits from the previous ASN
#ifdef JIT_STATS
  m_asn_link_clears++;
  m_asn_links_cleared += m_global_src_links.size();
#endif
  m_global_src_links.for_each(clear_edge);
  m_global_src_links.clear();
}
#ifdef JIT_VERIFY
uint64_t CJitEngine::verify_compare(uint64_t blk_virt, const uint64_t* interp, const uint64_t* jit,
                                    const uint32_t* words, uint32_t nwords, bool other_mismatch)
{
  bool mismatch = other_mismatch;
  for (int r = 0; r < 63; ++r) {   // r0..r30 main bank + r32..r62 PALshadow bank
    if (r == 31) continue;         // zero register
    if (interp[r] != jit[r]) {
      mismatch = true;
      printf("[JIT][VERIFY] MISMATCH at block pc=%016llx: r%d interp=%016llx jit=%016llx\n",
             (unsigned long long) blk_virt, r,
             (unsigned long long) interp[r], (unsigned long long) jit[r]);
      printf("   words:");   // the compiled prefix, to decode which instruction diverged
      for (uint32_t w = 0; w < nwords && w < 16; ++w) printf(" %08x", words[w]);
      printf("\n");
      break;
    }
  }
  return record_verify_result(mismatch);
}

uint64_t CJitEngine::verify_failed()
{
  return record_verify_result(true);
}

uint64_t CJitEngine::record_verify_result(bool mismatch)
{
  m_v_exec++;
  if (mismatch) m_v_fail++;
  if ((m_v_exec % 500000) == 0) {
    const auto t0 = std::chrono::steady_clock::now();   // exclude this print's I/O stall from the
    printf("[JIT][VERIFY] %llu comparisons, %llu mismatches\n",   // wall-clock-pinned RPCC
           (unsigned long long) m_v_exec, (unsigned long long) m_v_fail);
    return (uint64_t) std::chrono::duration_cast<std::chrono::nanoseconds>(
             std::chrono::steady_clock::now() - t0).count();
  }
  return 0;
}

// Side-exit-shaped trace verify: tally how each compiled-trace pass was classified and report
// periodically. side_exit staying 0 is EXPECTED for now.
void CJitEngine::note_trace_verify(int outcome)
{
  if (outcome < 0 || outcome > 3) return;
  m_tv_cnt[outcome]++;
  const uint64_t total = m_tv_cnt[0] + m_tv_cnt[1] + m_tv_cnt[2] + m_tv_cnt[3];
  if ((total % 200000) == 0)
    printf("[JIT][VERIFY][TRACE] runs=%llu full=%llu side_exit=%llu bail=%llu count_divergence=%llu\n",
           (unsigned long long) total, (unsigned long long) m_tv_cnt[0],
           (unsigned long long) m_tv_cnt[1], (unsigned long long) m_tv_cnt[3],
           (unsigned long long) m_tv_cnt[2]);
}
#endif

#ifdef JIT_STATS
#ifdef ES40_JIT_A64
static const char* const kJitHostBytesLabel = "a64-bytes/instr";
#else
static const char* const kJitHostBytesLabel = "x86-bytes/instr";
#endif

// Short mnemonic for the opcode that ended a block's compiled prefix (the coverage gap).
// Declared in jitengine_internal.h: the codegen backends print it too.
const char* jit_opcode_name(unsigned op)
{
  switch (op) {
    case 0x00: return "CALL_PAL";
    case 0x08: return "LDA";   case 0x09: return "LDAH"; case 0x0a: return "LDBU";  case 0x0b: return "LDQ_U";
    case 0x0c: return "LDWU";  case 0x0d: return "STW";  case 0x0e: return "STB";   case 0x0f: return "STQ_U";
    case 0x10: return "INTA";  case 0x11: return "INTL"; case 0x12: return "INTS";  case 0x13: return "INTM";
    case 0x14: return "ITFP";  case 0x15: return "FLTV"; case 0x16: return "FLTI";  case 0x17: return "FLTL";
    case 0x18: return "MISC";  case 0x19: return "HWMFPR";case 0x1a: return "JMP";   case 0x1b: return "HWLD";
    case 0x1c: return "FPTI";  case 0x1d: return "HWMTPR";case 0x1e: return "HWREI"; case 0x1f: return "HWST";
    case 0x20: return "LDF";   case 0x21: return "LDG";  case 0x22: return "LDS";   case 0x23: return "LDT";
    case 0x24: return "STF";   case 0x25: return "STG";  case 0x26: return "STS";   case 0x27: return "STT";
    case 0x2a: return "LDL_L"; case 0x2b: return "LDQ_L";case 0x2e: return "STL_C"; case 0x2f: return "STQ_C";
    default:   return "op";
  }
}

uint64_t CJitEngine::note_exec(uint32_t native_instr, uint32_t interp_instr, uint64_t comp_tsc, uint64_t interp_tsc)
{
  m_stat_native += native_instr;
  m_stat_interp += interp_instr;
  m_tsc_compiled += comp_tsc;         // host cycles spent in b->code() (compiled chains)
  m_tsc_interp   += interp_tsc;       // host cycles spent in the interp fallback loop
  if (native_instr) m_stat_hot++;     // one compiled-chain dispatch
  if (interp_instr) m_stat_miss++;    // one interpreted (cold/uncompilable) block
  const uint64_t total = m_stat_native + m_stat_interp;
  if (total < 100000000) return 0;    // report every 100M instructions

  // Time this report's own blocking I/O so the caller can exclude it from the wall-clock-pinned
  // RPCC -- else the printf stall is billed to the guest cycle counter as a forward jump.
  const auto stat_t0 = std::chrono::steady_clock::now();
  const uint64_t win_tsc = jit_rdtsc() - m_tsc_window_start;   // window's host cycles (time-split denominator)
#ifdef JIT_REGPROF
  regprof_report();   // exec-weighted GPR histogram for pin selection (I/O timed within this window)
#endif
  // Build each line in a buffer and print it with ONE call: 4 CPU threads print concurrently,
  // and per-item printf loops interleave mid-line. The [CPU%d] tag attributes each line.
  const double chain = m_stat_hot ? (double) m_stat_native / (double) m_stat_hot : 0.0;
  const double avgpl = m_stat_compiled ? (double) m_stat_plen_sum / (double) m_stat_compiled : 0.0;
  char buf[512];
  int  len;
  printf("[JIT][STATS][CPU%d] native %.1f%% (%llu/%llu) | chain avg %.1f instr over %llu dispatches | interp %llu blks\n",
         m_cpu_id, 100.0 * (double) m_stat_native / (double) total,
         (unsigned long long) m_stat_native, (unsigned long long) total,
         chain, (unsigned long long) m_stat_hot, (unsigned long long) m_stat_miss);
  // Throughput + code expansion: the in-JIT proxy for the codegen-quality diagnosis. MIPS is
  // wall-clock (clock-independent: cycles/instr = host_GHz / MIPS); host-bytes/instr is the static
  // average emitted expansion. Read these off the WORKER CPU; the first window includes warmup.
  const uint64_t now_ns = (uint64_t) std::chrono::duration_cast<std::chrono::nanoseconds>(
      stat_t0.time_since_epoch()).count();
  const uint64_t dt_ns  = (now_ns > m_stat_wall_last_ns) ? now_ns - m_stat_wall_last_ns : 0;
  const double   mips   = dt_ns ? (double) total * 1000.0 / (double) dt_ns : 0.0;
  const double   expand = m_stat_plen_sum ? (double) m_stat_code_bytes / (double) m_stat_plen_sum : 0.0;
  printf("[JIT][STATS][CPU%d] throughput %.0f MIPS (%llu instr / %.1f ms) | %.1f %s (static avg)\n",
         m_cpu_id, mips, (unsigned long long) total, (double) dt_ns / 1e6, expand,
         kJitHostBytesLabel);
  // Wall-time split (host TSC): where the window actually spent time -- compiled execution vs interp
  // fallback vs the dispatcher/chaining remainder. Separates the two bottleneck candidates the
  // instruction counts can't (chain length and interp rate both just track how hot/covered the code is).
  if (win_tsc) {
    const double cf  = 100.0 * (double) m_tsc_compiled / (double) win_tsc;
    const double itf = 100.0 * (double) m_tsc_interp   / (double) win_tsc;
    printf("[JIT][STATS][CPU%d] time-split: compiled %.1f%% | interp %.1f%% | dispatch %.1f%% | resolver %.1f%% (inside compiled)\n",
           m_cpu_id, cf, itf, (cf + itf < 100.0) ? (100.0 - cf - itf) : 0.0,
           100.0 * (double) m_tsc_resolver / (double) win_tsc);
  }
  // Bail-cause: of the compiled-chain dispatches (m_stat_hot), how many bailed via a cached-link miss
  // (branches/fall-through) vs a jit_indirect miss (computed jumps) vs gate/other (budget/interrupt/
  // mid-block fault = remainder). Pinpoints whether the branch link is the dispatch lever.
  if (m_stat_hot) {
    const uint64_t jmp_bail = (m_jmp_attempt > m_jmp_hit) ? (m_jmp_attempt - m_jmp_hit) : 0;
    const uint64_t other = (m_stat_hot > m_bail_link + jmp_bail) ? (m_stat_hot - m_bail_link - jmp_bail) : 0;
    printf("[JIT][STATS][CPU%d] bail-cause: link %.0f%% | jump %.0f%% | gate/other %.0f%% (of %llu) | jump chain-rate %.0f%%\n",
           m_cpu_id, 100.0 * (double) m_bail_link / (double) m_stat_hot,
           100.0 * (double) jmp_bail / (double) m_stat_hot, 100.0 * (double) other / (double) m_stat_hot,
           (unsigned long long) m_stat_hot,
           m_jmp_attempt ? 100.0 * (double) m_jmp_hit / (double) m_jmp_attempt : 0.0);
  }
  // Fresh-record reason (per window): tag=cache aliasing, asn=cross-process same-PC, phys=remap,
  // hash=failed reuse after a flush, cold=new/warmup. Admission can defer actual compilation.
  const uint64_t fresh = m_fresh_tag + m_fresh_asn + m_fresh_phys + m_fresh_hash + m_fresh_cold;
  if (fresh)
    printf("[JIT][STATS][CPU%d] fresh-cause: tag %llu | asn %llu | phys %llu | hash %llu | cold %llu (of %llu records)\n",
           m_cpu_id, (unsigned long long) m_fresh_tag, (unsigned long long) m_fresh_asn,
           (unsigned long long) m_fresh_phys, (unsigned long long) m_fresh_hash,
           (unsigned long long) m_fresh_cold, (unsigned long long) fresh);
  if (m_trace_formed || m_trace_entered)
    printf("[JIT][STATS][CPU%d] traces: formed %llu | entered %llu | exits %llu | stale %llu (windowed)\n",
           m_cpu_id, (unsigned long long) m_trace_formed, (unsigned long long) m_trace_entered,
           (unsigned long long) m_trace_exits, (unsigned long long) m_trace_stale);
  if (m_licm_same || m_licm_diff)   // region memops re-hitting the page they hit last time
    printf("[JIT][STATS][CPU%d] licm-probe: same-page %llu | new-page %llu (%.1f%% invariant)\n",
           m_cpu_id, (unsigned long long) m_licm_same, (unsigned long long) m_licm_diff,
           100.0 * (double) m_licm_same / (double) (m_licm_same + m_licm_diff));
  // Each cleared edge costs one dispatcher round trip to re-link.
  if (m_link_invalidations || m_asn_link_clears || m_evict_link_clears)
    printf("[JIT][STATS][CPU%d] link-churn: %llu epoch bumps cleared %llu edges | %llu cross-ASN evictions cleared %llu edges | %llu ASN switches cleared %llu global-source edges | %llu page invalidates cleared %llu edges | %llu reclaims total\n",
           m_cpu_id, (unsigned long long) m_link_invalidations, (unsigned long long) m_links_cleared,
           (unsigned long long) m_evict_link_clears, (unsigned long long) m_evict_links_cleared,
           (unsigned long long) m_asn_link_clears, (unsigned long long) m_asn_links_cleared,
           (unsigned long long) m_page_clears, (unsigned long long) m_page_links_cleared,
           (unsigned long long) m_reclaims);
  printf("[JIT][STATS][CPU%d] exits: ceiling %llu | pending %llu (timers %llu, int %llu) | resolver-null %llu | mem-bail %llu | mem-trap %llu | op-bail %llu | site-stub %llu | pic-miss %llu\n",
         m_cpu_id, (unsigned long long) m_exit_kind[0], (unsigned long long) m_exit_kind[1], (unsigned long long) m_exit_kind[8], (unsigned long long) m_exit_kind[9],
         (unsigned long long) m_exit_kind[2], (unsigned long long) m_exit_kind[3], (unsigned long long) m_exit_kind[4], (unsigned long long) m_exit_kind[5],
         (unsigned long long) m_exit_kind[6], (unsigned long long) m_exit_kind[7]);
  {   // top blocks by kind-1 exits this window
    int top[6] = {-1,-1,-1,-1,-1,-1}; uint64_t topv[6] = {0,0,0,0,0,0};
    for (int i2 = 0; i2 < kCacheEntries; ++i2) {
      const uint64_t v = m_blocks[i2].st_exit1;
      if (!v) continue;
      int k = 5; if (v <= topv[5]) { m_blocks[i2].st_exit1 = 0; continue; }
      while (k > 0 && topv[k - 1] < v) { top[k] = top[k - 1]; topv[k] = topv[k - 1]; --k; }
      top[k] = i2; topv[k] = v;
    }
    for (int k = 0; k < 6; ++k) if (top[k] >= 0) {
      JitBlock& b = m_blocks[top[k]];
      printf("[JIT][STATS][CPU%d]   exit1 %llu from tag=%016llx pal=%d prefix=%u n_instr=%u asm_global=%d\n",
             m_cpu_id, (unsigned long long) topv[k], (unsigned long long) b.tag, (int) b.pal_shadow, b.prefix_len, b.n_instr, (int) b.asm_global);
      b.st_exit1 = 0;
    }
  }
  if (m_link_invalidations)
    printf("[JIT][STATS][CPU%d] bump-cause: remap %llu | tbia %llu | tbiap %llu | tbis %llu | flush %llu | flush-asm %llu | evict %llu | reclaim %llu | mode %llu\n",
           m_cpu_id, (unsigned long long) m_bump_cause[kBumpRemap], (unsigned long long) m_bump_cause[kBumpTbia],
           (unsigned long long) m_bump_cause[kBumpTbiap], (unsigned long long) m_bump_cause[kBumpTbis],
           (unsigned long long) m_bump_cause[kBumpFlush], (unsigned long long) m_bump_cause[kBumpFlushAsm],
           (unsigned long long) m_bump_cause[kBumpEvict], (unsigned long long) m_bump_cause[kBumpReclaim],
           (unsigned long long) m_bump_cause[kBumpMode]);
  if (m_direct_patches || m_patch_unreachable)
    printf("[JIT][STATS][CPU%d] direct-patch: %llu sites patched | %llu beyond rel32 reach (left unpatched)\n",
           m_cpu_id, (unsigned long long) m_direct_patches, (unsigned long long) m_patch_unreachable);
  // PIC hits never reach the resolver.
  if (m_jmp_exits || m_mb_exec)
    printf("[JIT][STATS][CPU%d] computed exits %llu | PIC hit %.1f%% | jcache hits %llu | resolver calls %llu (chained %llu) | MB fences %llu\n",
           m_cpu_id, (unsigned long long) m_jmp_exits,
           m_jmp_exits ? 100.0 * (double) (m_jmp_exits > m_jmp_attempt + m_jc_hits ? m_jmp_exits - m_jmp_attempt - m_jc_hits : 0) / (double) m_jmp_exits : 0.0,
           (unsigned long long) m_jc_hits,
           (unsigned long long) m_jmp_attempt, (unsigned long long) m_jmp_hit, (unsigned long long) m_mb_exec);
  // First interpreted opcode per interp dispatch.
  { uint64_t rb[64]; memcpy(rb, m_rt_break_op, sizeof(rb));
    uint64_t rt_total = 0; for (int op = 0; op < 64; ++op) rt_total += rb[op];
    if (rt_total) {
      len = snprintf(buf, sizeof(buf), "[JIT][STATS][CPU%d] rt-breakers (%llu interp dispatches by first op):", m_cpu_id, (unsigned long long) rt_total);
      for (int rank = 0; rank < 8 && len < (int) sizeof(buf) - 32; ++rank) {
        int best = -1; uint64_t bestv = 0;
        for (int op = 0; op < 64; ++op) if (rb[op] > bestv) { bestv = rb[op]; best = op; }
        if (best < 0) break;
        len += snprintf(buf + len, sizeof(buf) - len, " %s(0x%02x)=%llu", jit_opcode_name(best), best, (unsigned long long) bestv);
        rb[best] = 0;
      }
      printf("%s\n", buf);
    } }
  { uint64_t fh[6] = {0}, mh[6] = {0}, tm = 0;   // link-fanout: thrashing source blocks + cumulative misses, bucketed by #distinct successors
    for (int i = 0; i < kCacheEntries; ++i) {
      if (!m_blocks[i].valid || m_blocks[i].link_misses == 0) continue;
      int f = m_blocks[i].link_fanout > 5 ? 5 : m_blocks[i].link_fanout;
      fh[f]++; mh[f] += m_blocks[i].link_misses; tm += m_blocks[i].link_misses;
    }
    if (tm) printf("[JIT][STATS][CPU%d] link-fanout (srcs/misses by #distinct successors): f1=%llu/%llu f2=%llu/%llu f3=%llu/%llu f4=%llu/%llu f5+=%llu/%llu | total miss %llu\n",
      m_cpu_id, (unsigned long long) fh[1], (unsigned long long) mh[1], (unsigned long long) fh[2], (unsigned long long) mh[2], (unsigned long long) fh[3], (unsigned long long) mh[3], (unsigned long long) fh[4], (unsigned long long) mh[4], (unsigned long long) fh[5], (unsigned long long) mh[5], (unsigned long long) tm); }
  len = snprintf(buf, sizeof(buf), "[JIT][STATS][CPU%d] %llu recorded, %llu compiled (avg %.1f instr) | block-breakers:",
                 m_cpu_id, (unsigned long long) m_recorded, (unsigned long long) m_stat_compiled, avgpl);
  uint64_t hist[64];
  memcpy(hist, m_term_op, sizeof(hist));
  for (int rank = 0; rank < 8 && len < (int) sizeof(buf) - 32; ++rank) {
    int best = -1; uint64_t bestv = 0;
    for (int op = 0; op < 64; ++op) if (hist[op] > bestv) { bestv = hist[op]; best = op; }
    if (best < 0) break;
    len += snprintf(buf + len, sizeof(buf) - len, " %s(0x%02x)=%llu", jit_opcode_name(best), best, (unsigned long long) bestv);
    hist[best] = 0;
  }
  printf("%s\n", buf);
  if (m_term_op[0]) {   // CALL_PAL cut blocks -- show which function codes dominate (chain targets)
    len = snprintf(buf, sizeof(buf), "[JIT][STATS][CPU%d]   CALL_PAL by func:", m_cpu_id);
    uint64_t ph[256];
    memcpy(ph, m_pal_func, sizeof(ph));
    for (int rank = 0; rank < 8 && len < (int) sizeof(buf) - 32; ++rank) {
      int best = -1; uint64_t bestv = 0;
      for (int f = 0; f < 256; ++f) if (ph[f] > bestv) { bestv = ph[f]; best = f; }
      if (best < 0) break;
      len += snprintf(buf + len, sizeof(buf) - len, " 0x%02x=%llu", best, (unsigned long long) bestv);
      ph[best] = 0;
    }
    printf("%s\n", buf);
  }
  if (m_term_op[0x1d]) {   // HW_MTPR cut blocks, grouped by raw encoding rather than a family selector.
    len = snprintf(buf, sizeof(buf), "[JIT][STATS][CPU%d]   HW_MTPR by instruction[15:8]:", m_cpu_id);
    uint64_t mh[256];
    memcpy(mh, m_mtpr_encoding_hi, sizeof(mh));
    for (int rank = 0; rank < 8 && len < (int) sizeof(buf) - 32; ++rank) {
      int best = -1; uint64_t bestv = 0;
      for (int f = 0; f < 256; ++f) if (mh[f] > bestv) { bestv = mh[f]; best = f; }
      if (best < 0) break;
      len += snprintf(buf + len, sizeof(buf) - len, " 0x%02x=%llu", best, (unsigned long long) bestv);
      mh[best] = 0;
    }
    printf("%s\n", buf);
  }
  if (m_term_op[0x1b]) {   // HW_LD cut blocks -- which forms dominate (virt/lock/vpte/chk)
    len = snprintf(buf, sizeof(buf), "[JIT][STATS][CPU%d]   HW_LD by form:", m_cpu_id);
    uint64_t lh[16];
    memcpy(lh, m_hwld_func, sizeof(lh));
    for (int rank = 0; rank < 8 && len < (int) sizeof(buf) - 32; ++rank) {
      int best = -1; uint64_t bestv = 0;
      for (int f = 0; f < 16; ++f) if (lh[f] > bestv) { bestv = lh[f]; best = f; }
      if (best < 0) break;
      len += snprintf(buf + len, sizeof(buf) - len, " 0x%x=%llu", best, (unsigned long long) bestv);
      lh[best] = 0;
    }
    printf("%s\n", buf);
  }
  if (m_term_op[0x18]) {   // MISC cut blocks -- which Ra==31 form dominates (RPCC degenerate-noop / RC,RS soft-intr-flag)
    printf("[JIT][STATS][CPU%d]   MISC by form: RPCC=%llu RC=%llu RS=%llu\n", m_cpu_id,
           (unsigned long long) m_misc_func[0xc], (unsigned long long) m_misc_func[0xe],
           (unsigned long long) m_misc_func[0xf]);
  }
  m_stat_native = m_stat_interp = m_stat_hot = m_stat_miss = 0;   // reset the window
  m_tsc_compiled = m_tsc_interp = m_tsc_resolver = 0;
  m_tsc_window_start = jit_rdtsc();   // next window's split denominator starts after this report's I/O
  m_bail_link = m_jmp_attempt = m_jmp_hit = 0;
  m_fresh_cold = m_fresh_tag = m_fresh_asn = m_fresh_phys = m_fresh_hash = 0;
  m_trace_formed = m_trace_entered = m_trace_exits = m_trace_stale = 0;
  m_licm_same = m_licm_diff = 0;
  m_link_invalidations = m_links_cleared = m_asn_link_clears = m_asn_links_cleared = 0;
  m_evict_link_clears = m_evict_links_cleared = m_jmp_exits = m_jc_hits = m_mb_exec = 0;
  memset(m_exit_kind, 0, sizeof(m_exit_kind));
  m_direct_patches = m_patch_unreachable = 0;
  m_page_clears = m_page_links_cleared = 0;
  memset(m_bump_cause, 0, sizeof(m_bump_cause));
  memset(m_rt_break_op, 0, sizeof(m_rt_break_op));
  const auto stat_end = std::chrono::steady_clock::now();
  m_stat_wall_last_ns = (uint64_t) std::chrono::duration_cast<std::chrono::nanoseconds>(
      stat_end.time_since_epoch()).count();   // next window's throughput delta starts after this I/O
  return (uint64_t) std::chrono::duration_cast<std::chrono::nanoseconds>(stat_end - stat_t0).count();
}
#endif

#ifdef JIT_REGPROF
// Pin-selection report: attribute each live block's executions (rp_hits) to every GPR it touches
// (rp_mask), then print the heaviest. A trailing '*' marks regs that CANNOT be pinned -- R4-7/R20-23,
// the PALshadow remap targets (which also covers R23's CALL_PAL save). R31 is excluded by the mask.
void CJitEngine::regprof_report()
{
  uint64_t hist[32] = { 0 };
  uint64_t exec_instr = 0, exec_bytes = 0;   // exec-weighted: hot-path Alpha instrs and emitted x86 bytes
  for (int s = 0; s < kCacheEntries; ++s) {
    const JitBlock& b = m_blocks[s];
    if (!b.valid || b.rp_hits == 0) continue;
    for (int r = 0; r < 31; ++r) if (b.rp_mask & (1u << r)) hist[r] += b.rp_hits;
    exec_instr += b.rp_hits * (uint64_t) b.prefix_len;
    exec_bytes += b.rp_hits * (uint64_t) b.rp_csz;
  }
  // Execution-weighted code expansion -- the HOT path, not the cold-block-skewed static average.
  // Host instrs/guest instr ~= this / avg host instr size (~3.5 on x86; exactly 4 on A64);
  // with cycles/instr from the throughput line -> hot-path IPC.
  printf("[JIT][REGPROF][CPU%d] exec-weighted expansion: %.1f %s (hot path)\n",
         m_cpu_id, exec_instr ? (double) exec_bytes / (double) exec_instr : 0.0,
         kJitHostBytesLabel);
  char buf[256];
  int  len = snprintf(buf, sizeof(buf), "[JIT][REGPROF][CPU%d] hot GPRs (exec x accesses):", m_cpu_id);
  for (int rank = 0; rank < 8 && len < (int) sizeof(buf) - 24; ++rank) {
    int best = -1; uint64_t bestv = 0;
    for (int r = 0; r < 31; ++r) if (hist[r] > bestv) { bestv = hist[r]; best = r; }
    if (best < 0) break;
    const bool pinnable = (best & 0xc) != 0x4;   // exclude R4-7 / R20-23 (shadow remap; covers R23)
    len += snprintf(buf + len, sizeof(buf) - len, " R%d=%llu%s", best, (unsigned long long) bestv, pinnable ? "" : "*");
    hist[best] = 0;
  }
  printf("%s   (* = not pin-eligible)\n", buf);
  // Hot-block dump every 16th report: the top blocks by executed host bytes, guest words + host
  // code as hex, for offline disassembly (scratch tool: hotblocks.py).
  static int report_n = 0;
  if (++report_n % 16 == 0 && m_dbg_source.query) {
    char name[64]; snprintf(name, sizeof(name), "jit-hot-cpu%d.txt", m_cpu_id);
    FILE* f = fopen(name, "a");
    if (f) {
      const int K = 24; int top[K]; uint64_t topw[K]; int n = 0;
      for (int s2 = 0; s2 < kCacheEntries; ++s2) {
        const JitBlock& b = m_blocks[s2];
        if (!b.valid || !b.code || b.rp_hits == 0) continue;
        const uint64_t w = b.rp_hits * (uint64_t) b.rp_csz;
        if (n == K && w <= topw[K - 1]) continue;
        int k = n < K ? n++ : K - 1;
        while (k > 0 && topw[k - 1] < w) { top[k] = top[k - 1]; topw[k] = topw[k - 1]; --k; }
        top[k] = s2; topw[k] = w;
      }
      fprintf(f, "== report %d\n", report_n);
      for (int k = 0; k < n; ++k) {
        const JitBlock& b = m_blocks[top[k]];
        fprintf(f, "block tag=%016llx phys=%llx asn=%u hits=%llu prefix=%u hash_len=%u csz=%u\nguest:", (unsigned long long) b.tag,
                (unsigned long long) b.phys, b.asn, (unsigned long long) b.rp_hits, b.prefix_len, b.hash_len, b.rp_csz);
        const auto source = m_dbg_source.view(b.tag);
        if (source.identity == b.phys && source.asm_global == b.asm_global
            && source.contains(b.hash_len)) {
          for (uint32_t i = 0; i < b.hash_len; ++i) fprintf(f, " %08x", source.word(i));
        } else {
          fprintf(f, " <unavailable>");
        }
        fprintf(f, "\nhost:");
        for (uint32_t i = 0; i < b.rp_csz; ++i) fprintf(f, "%02x", ((const uint8_t*) (void*) b.code)[i]);
        fprintf(f, "\n");
      }
      fclose(f);
    }
  }
}
#endif


#endif // ES40_JIT

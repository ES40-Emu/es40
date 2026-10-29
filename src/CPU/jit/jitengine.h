/* ES40 emulator -- JIT engine
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
#if !defined(INCLUDED_JITENGINE_H)
#define INCLUDED_JITENGINE_H

#ifdef ES40_JIT

// Host codegen architecture, picking which backend TU is live: x86-64 has the full backend
// jitengine_x64.cpp - x86_64 host
// jitengine_a64.cpp - ARM64 aka AArch64 host.
// The host-independent engine is jitengine.cpp
#if defined(_M_X64) || defined(__x86_64__)
#define ES40_JIT_X64 1
#elif defined(_M_ARM64) || defined(__aarch64__)
#define ES40_JIT_A64 1
#else
#error "ES40_JIT requires an x86-64 or ARM64 host"
#endif

#include <cstddef>
#include <cstdint>
#include "../AlphaCPUProfile.h"
#include <vector>
#include "../../config_debug.h"   // JIT_VERIFY
#ifdef JIT_STATS
// host cycle counter for the JIT_STATS wall-time split: TSC on x86-64, CNTVCT_EL0 on ARM64
#if defined(ES40_JIT_X64)
#if defined(_MSC_VER)
#include <intrin.h>        // __rdtsc
#else
#include <x86intrin.h>
#endif
static inline uint64_t jit_rdtsc() { return __rdtsc(); }
#elif defined(_M_ARM64)
#include <intrin.h>
static inline uint64_t jit_rdtsc() { return _ReadStatusReg(ARM64_SYSREG(3, 3, 14, 0, 2)); }   // CNTVCT_EL0
#else
static inline uint64_t jit_rdtsc() { uint64_t v; __asm__ __volatile__("mrs %0, cntvct_el0" : "=r"(v)); return v; }
#endif
#endif
#if defined(JIT_REGPROF) && !defined(JIT_STATS)
#error "JIT_REGPROF needs JIT_STATS (its report rides note_exec's 100M-instruction window)"
#endif
#ifdef JIT_DISASM
#include <cstdio>              // FILE* for the per-CPU disassembly trace
#endif

class CAlphaCPU;   // compiled blocks call back into the CPU for memory accesses

static inline uint32_t jit_flat_hash(uint64_t x) { return (uint32_t) ((x * UINT64_C(0x9e3779b97f4a7c15)) >> 32); }
static inline uint32_t jit_flat_hash(void* p) { return jit_flat_hash((uint64_t) (uintptr_t) p); }

// Open-addressed key -> int32 index with no per-insert allocation. Links are wiped on every
// exec and cross-ASN eviction, and std::unordered_set's node mallocs were ~30% of host time
// in process-creation loads. clear() resets only the used slots, so capacity survives a wipe.
// No erase: callers mark their own entries dead instead.
template <class Key>
class JitFlatIndex
{
public:
  explicit JitFlatIndex(uint32_t capacity = 1024) { rebuild(capacity); }

  // The value for key; a new key gets -1 and sets inserted.
  int32_t& get(const Key& key, bool& inserted)
  {
    if ((m_used.size() + 1) * 2 > m_slots.size()) rebuild((uint32_t) m_slots.size() * 2);
    uint32_t i = jit_flat_hash(key) & m_mask;
    for (; m_slots[i].full; i = (i + 1) & m_mask)
      if (m_slots[i].key == key) { inserted = false; return m_slots[i].val; }
    m_slots[i].key = key; m_slots[i].val = -1; m_slots[i].full = true;
    m_used.push_back(i);
    inserted = true;
    return m_slots[i].val;
  }
  void add(const Key& key) { bool inserted; (void) get(key, inserted); }
  int32_t* find(const Key& key)
  {
    for (uint32_t i = jit_flat_hash(key) & m_mask; m_slots[i].full; i = (i + 1) & m_mask)
      if (m_slots[i].key == key) return &m_slots[i].val;
    return nullptr;
  }
  template <class F> void for_each(F f) const { for (uint32_t i : m_used) f(m_slots[i].key); }
  size_t size() const { return m_used.size(); }
  void clear()
  {
    for (uint32_t i : m_used) m_slots[i].full = false;
    m_used.clear();
  }

private:
  struct Slot { Key key; int32_t val; bool full; };
  std::vector<Slot> m_slots;
  std::vector<uint32_t> m_used;   // occupied slot indices, in insertion order
  uint32_t m_mask = 0;

  void rebuild(uint32_t capacity)   // capacity: a power of two
  {
    std::vector<Slot> old;
    old.swap(m_slots);
    std::vector<uint32_t> used;
    used.swap(m_used);
    m_slots.assign(capacity, Slot{});
    m_used.reserve(capacity / 2);
    m_mask = capacity - 1;
    for (uint32_t o : used) {
      uint32_t i = jit_flat_hash(old[o].key) & m_mask;
      while (m_slots[i].full) i = (i + 1) & m_mask;
      m_slots[i] = old[o];
      m_used.push_back(i);
    }
  }
};

class CJitEngine
{
public:
  // 256K slots: the OS-active CPU's block working set (50K+) thrashed the old 16K direct-mapped cache
  // (~190K recompiles/100M); 64K cut that to ~14K/100M, but a 50K set in 64K slots still conflict-evicts
  // (load ~0.8). 256K drops the load to ~0.2. JitBlock ~110 B -> ~28 MB/CPU of metadata.
  //
  // SET-ASSOCIATIVE: more slots could never fix the churn. Every Unix process has
  // its text at the SAME virtual addresses, so a virt_pc-only index puts every 
  // process's copy of a/ block in one slot with only the ASN telling them apart 
  // so they evit each other on every context switch, at any table size. 
  static constexpr int      kCacheBits = 18;
  static constexpr int      kCacheEntries = 1 << kCacheBits;   // TOTAL entries (kSets * kWays)
  static constexpr int      kWays = 8;                         // keys per set; raise if fresh-cause asn stays high
  static constexpr int      kSets = kCacheEntries / kWays;
  static constexpr uint64_t kSetMask = (uint64_t) kSets - 1;
  static_assert(kWays > 0 && (kWays & (kWays - 1)) == 0, "kWays must be a power of 2 (victim cursor masks with kWays-1)");
  static_assert(kSets * kWays == kCacheEntries, "kSets * kWays must cover the whole table (flush scans it linearly)");

  // Trace tier: a small SECOND cache, beside m_blocks, for hot superblock heads. 
  // Only the hottest loop heads are promoted, so it stays small.
  static constexpr int      kTraceBits = 12;            // 4K trace heads
  static constexpr uint64_t kTraceEntries = 1 << kTraceBits;
  static constexpr uint64_t kTraceIndexMask = kTraceEntries - 1;
  static constexpr uint32_t kMaxTraceSegs = 16;         // fused blocks per trace (multi-block coherence)
  static constexpr uint32_t kMaxTraceExits = 16;        // guards / side-exits per trace

  // Reclaim executable memory once compiled code passes this many bytes, rather
  // than tearing down the asmjit runtime on every flush (see flush()).
  static constexpr uint64_t kReclaimBytes = 128 * 1024 * 1024;   // a VMS boot alone passes 32 MB six times

  // Compiled block entry point. Runs the prefix on regs[0..31], calling back into
  // cpu for memory accesses; returns the number of instructions fully completed
  typedef uint32_t (*JitFn)(CAlphaCPU* cpu, uint64_t* regs);
  typedef uint32_t (*JitEnterFn)(CAlphaCPU* cpu, uint64_t* regs, void* body);   // shared prologue -> body

  static constexpr int kLinkSlots = 2;   // cached direct successors per block (poly-link). Instrumentation
                                         // showed the thrashing fanout is EXACTLY 2; bump only if f3/f4 appear.

  // Chain instructions before a gated exit returns; a constant so the gate loads nothing.
  static constexpr uint32_t kChainCeiling = 8192;
  static constexpr bool     kJumpCache = true;   // probe the target-keyed jump cache on a PIC miss

  // Packed successor snapshot for the poly-link chain guard: everything the emitted guard reads
  // lives in the SOURCE block's cache line meaning no dereference into the successor's JitBlock on the
  // hot path. 
  struct LinkSlot
  {
    uint64_t tag;    // this exit's target virtual PC (entries are keyed, so a hit is correct for that PC)
    uint64_t vgen;   // low 63 bits: validation epoch; bit 63: target PAL shadow variant
    void*    body;   // successor's chained entry point at patch time; null = empty slot
  };

  // Target-keyed computed-jump cache, probed on a per-site PIC miss before the resolver.
  struct JumpCacheEntry
  {
    uint64_t tag;    // target virtual PC (bit 0 = PALmode)
    uint64_t gen;    // low 63 bits: m_jc_gen at fill; bit 63: target PAL shadow variant
    void*    body;   // target's chained entry point
    uint64_t asn;    // I-stream ASN the body was resolved under
  };
  static_assert(sizeof(JumpCacheEntry) == 32, "jump-cache entry stride");
  static constexpr int kJumpCacheEntries = 4096;
  static inline uint32_t jcache_index(uint64_t target) { return (uint32_t) (target >> 2) & (kJumpCacheEntries - 1); }
  inline void jcache_fill(uint64_t target, bool shadow, void* body, uint32_t asn)
  {
    JumpCacheEntry& e = m_jcache[jcache_index(target)];
    e.tag = target; e.gen = m_jc_gen | (shadow ? (uint64_t) 1 << 63 : 0); e.body = body; e.asn = asn;
  }

  // Direct exit: a `jmp rel32` in the code allocation, retargeted by the dispatcher.
  struct PatchSite
  {
    uint8_t* site;   // E9 opcode
    uint8_t* stub;   // unpatched target
    uint64_t tag;    // static successor PC
    uint64_t source_tag; // immutable source PC, owned by this code allocation
  };
  static_assert(offsetof(PatchSite, source_tag) == 24, "direct source tag offset");
  static_assert(sizeof(PatchSite) == 32, "direct patch descriptor layout");

  // Permission bits stay with the emitted code, which can outlive its
  // JitBlock cache slot.
  struct NativeExecProof
  {
    uint64_t epoch;      // epoch of the last mode_mask change (JitBlock::exec_auth is current)
    uint32_t mode_mask;  // modes with execute access
    uint32_t guard_imm_back; // x64: bytes back from this proof to TEST's imm8; A64: zero
  };
  static_assert(offsetof(NativeExecProof, epoch) == 0, "native proof epoch offset");
  static_assert(offsetof(NativeExecProof, mode_mask) == 8, "native proof mode offset");
  static_assert(offsetof(NativeExecProof, guard_imm_back) == 12, "native guard immediate offset");
  static_assert(sizeof(NativeExecProof) == 16, "native execution proof layout");

  // FIELD ORDER IS LOAD-BEARING: everything lookup() reads -- tag, asn, asm_global,
  // pal_shadow, valid, flush_gen -- is packed into the first 40 bytes so a way costs ONE
  // cache line to test.
  struct JitBlock
  {
    uint64_t tag;         // start VIRTUAL PC (validity tag / key)
    uint64_t phys;        // start physical PC (source bytes for compilation)
    uint32_t asn;         // address space number (key; ignored when asm_global)
    bool     asm_global;  // global (ASM) page: matches any ASN, like the icache
    bool     pal_shadow;  // PALmode variant: true maps R4-7/R20-23 to the shadow bank
    uint32_t n_instr;     // instructions in the straight-line block
    bool     valid;
    uint64_t flush_gen;   // global or process-specific flush generation at last source validation
    JitFn    code;        // compiled safe-prefix, or null (prologue entry, for C calls)
    void*    jit_body;    // chained re-entry point (after the prologue); null when not compiled
    void*    jit_body_same_page; // after native mode guard; only static same-page edges, null for PAL
    NativeExecProof* exec_proof; // embedded data for this code allocation; null for PAL
    uint64_t exec_auth;   // copy of *exec_proof: epoch << 4 | mode_mask; set only via set_exec_proof
    NativeExecProof* exec_proof_rw; // x64: writable alias of exec_proof, null until first authorized
    LinkSlot link[kLinkSlots];    // cached direct successors (poly-link, round-robin back-patched)
#ifdef JIT_STATS
    uint32_t link_misses; // instrumentation: per-source link-miss count, cumulative (poly-link sizing)
    uint8_t  link_fanout; // distinct re-link targets seen (saturates at 4; >4 => a small successor cache won't help)
    uint64_t link_seen[4];
#endif
    uint32_t prefix_len;  // # safe ALU ops in code
    bool     compiled;    // compile has been attempted
    uint8_t  compile_encounters; // eligible cold passes, saturated at 2; separate from trace hotness
    uint32_t body_off;    // jit_body's offset within code -- restores the chained entry on revalidate
    uint64_t src_sum;     // hash of the source words at compile time (revalidate vs self-mod)
    uint32_t hash_len;    // word count src_sum covers -- frozen at compile time; n_instr drifts
                          // (interrupt-truncated cold passes shrink it), so it must NOT key the hash
    uint64_t vgen;        // m_vgen_cur at last full validation (phys + code bytes). The counter is
                          // monotonic, so one compare detects any invalidating event since then
                          // -- the single chain guard (see emit_chain / jit_indirect).
    uint32_t hot;         // dispatches since record; at the promote threshold -> form a trace
#ifdef JIT_STATS
    uint64_t st_exit1;    // STATS: dispatcher returns through this block's gate/guard exit (kind 1)
#endif
#ifdef JIT_REGPROF
    uint64_t rp_hits;     // REGPROF: block executions since record (body-entry inc -- counts chained runs)
    uint32_t rp_mask;     // REGPROF: Alpha GPRs touched by this block (bit r); compile-time, exec-weighted at report
    uint32_t rp_csz;      // REGPROF: emitted host bytes for this block -- rp_hits x rp_csz = exec-weighted expansion
#endif
  };

  // Per fused block: the source-coherence descriptor (review: multi-block traces need this). A trace
  // spans multiple blocks/pages, so a single head tag + epoch is not enough -- trace_ok() re-hashes
  // these on an epoch change, mirroring revalidate_flushed, and also stores what to re-form.
  struct SourceSeg {
    uint64_t guest_pc;    // segment start virtual PC
    uint64_t phys_pc;     // segment start physical PC (source bytes)
    uint32_t n_instr;     // instructions in the segment (hash length)
    bool     asm_global;  // global (ASM) segment
    uint32_t asn;         // ASN (ignored when asm_global)
    uint64_t src_sum;     // hash of the segment's source words at build
  };

  // which guest regs are held in host registers (not yet copied back to state.r[]) at a side-exit.
  struct Snapshot { uint64_t dirty_gpr; uint64_t dirty_fpr; };

  struct TraceExit {
    LinkSlot link[kLinkSlots];   // chain-out: this exit's cached successors
    uint64_t guest_pc;    // resume PC handed to the block dispatcher (compile-time constant)
    Snapshot snap;        // M4+ (zero until then)
  };

  struct TraceFragment {
    uint64_t  head_tag;       // entry virtual PC (key)
    uint32_t  asn;            // key (ignored when asm_global)
    bool      asm_global;
    bool      valid;
    JitFn     code;           // single entry; null = empty slot
    void*     chain_entry;    // chain-in: tail-jmp entry (frame live, global pins)
    uint32_t  underrun;       // side-exits taken since the last loop closure 
    uint64_t  vgen;           // build epoch = m_vgen_cur for coherence
    uint64_t  flush_gen;      // IC-flush epoch at build
    uint32_t  n_blocks, n_instr;
    SourceSeg segs[kMaxTraceSegs];   uint32_t n_segs;
    TraceExit exits[kMaxTraceExits]; uint32_t n_exits;
  };

  // Byte offsets (from the CAlphaCPU*) of the fields the inline load fast path reads,
  // so compiled code can touch them via [cpu + offset]. Filled once by set_offsets().
  struct JitOffsets {
    uint32_t dpc_valid, dpc_virt_page, dpc_phys_base, dpc_host_bias, dpc_cm, dpc_asn;  // offsets of READ slot [0][0]
    uint32_t dpc_stride, dpc_mask;   // direct-mapped page cache: per-slot byte stride, index mask
    uint32_t dpc_write_row;          // byte distance from read cache [0] to write cache [1] (store fast path)
    uint32_t dpc_bank;               // u32 byte offset of the current CM's bank (added to the slot index)
    uint32_t dpc_ll_watch_tag;       // value: monitored-page marker, not a CPU field offset
    uint32_t state_cm, jit_mode_bit, state_asn0, state_asn, dram_ptr, dram_size, state_pc, state_current_pc;
    uint32_t fpen, exc_sum, fpcr, f_base;   // FP inline path: FPSTART gate + FPCR (rounding/INE) + f[] base (f[i] = f_base + i*8)
    uint32_t f_from_r;   // byte distance from state.r[0] to state.f[0]
    // For chaining: the budget ceiling and the interrupt-poll flags the compiled epilogue
    // checks before jumping on; link_from is where the epilogue records a link-patch request.
    uint32_t jit_budget, check_int, check_timers, link_from;
    uint32_t exc_addr, pal_base, sde;   // CALL_PAL: exc_addr save, PAL entry base, PALshadow enable
    uint32_t helpers;   // CPU-resident helper fn table 
    uint32_t jit_exit;  // CPU-resident pointer to the shared epilogue (blocks `jmp [cpu+jit_exit]`)
    uint32_t mm_stat, fault_va;   // pure HW_MFPR fault-reporting fields
  };
  void set_offsets(const JitOffsets& o) { m_off = o; }
  // Set before compiling: CSystem's LL/SC buckets and watch map.
  // Bucket layout: 64-byte stride, sequence at 0, byte lock at 8.
  void set_smp_memory(bool enabled, void* buckets, void* watched_lines) {
    m_smp_memory = enabled;
    m_ll_buckets = reinterpret_cast<uintptr_t>(buckets);
    m_ll_watch = reinterpret_cast<uintptr_t>(watched_lines);
  }

  // Per-op helper function pointers
  struct HelperSet {
    void* read_helper;        void* write_helper;     void* opcdec_helper;   void* hw_mfpr_helper;
    void* hw_ld_helper;       void* hw_mtpr_helper;   void* hw_st_helper;    void* indirect_helper;
    void* read_locked_helper; void* stc_helper;       void* misc_helper;     void* read_vpte_helper;
    void* read_wchk_helper;   void* itof_helper;      void* ftoi_helper;     void* fltl_helper;
    void* fp_read_helper;     void* fp_write_helper;  void* fltv_helper;
  };

  explicit CJitEngine(const AlphaCPUProfile& profile, int cpu_id = 0);   // cpu_id tags the stats/diagnostic prints
  ~CJitEngine();

  static inline uint64_t set_of(uint64_t virt_pc) { return (virt_pc >> 2) & kSetMask; }
  static inline uint64_t trace_index_of(uint64_t virt_pc) { return (virt_pc >> 2) & kTraceIndexMask; }

  // The kWays entries a PC maps to, contiguous. A set is ~kWays * sizeof(JitBlock) (~1.2 KB at
  // 8 ways), so a full scan is a MISS-path cost, not a hit-path one.
  inline JitBlock* set_base(uint64_t virt_pc) { return &m_blocks[set_of(virt_pc) * kWays]; }

  // Does this way hold the block for (virt_pc, asn)? A global (ASM) block matches any ASN,
  // mirroring the icache's hit rule. 
  static inline bool way_keyed(const JitBlock& b, uint64_t virt_pc, uint32_t asn,
                               bool pal_shadow)
  {
    return (b.valid || b.code) && b.tag == virt_pc && (b.asm_global || b.asn == asn)
        && (!(virt_pc & 1) || b.pal_shadow == pal_shadow);
  }

  inline uint64_t flush_generation(bool asm_global) const
  {
    return asm_global ? m_flush_gen : m_non_global_flush_gen;
  }

  // Virtual+ASN keyed: no translation on the dispatch hot path. flush_gen-stale blocks
  // miss here; revalidate_flushed() resurrects them after a source-hash check.
  inline JitBlock* lookup(uint64_t virt_pc, uint32_t asn, bool pal_shadow)
  {
    JitBlock* const set = set_base(virt_pc);
    for (int w = 0; w < kWays; ++w) {
      JitBlock& b = set[w];
      if (b.valid && b.flush_gen == flush_generation(b.asm_global) && b.tag == virt_pc
          && (b.asm_global || b.asn == asn)
          && (!(virt_pc & 1) || b.pal_shadow == pal_shadow))
        return &b;
    }
    return nullptr;
  }

  // Trace tier (M0+): the global kill-switch + the trace-cache lookup. traces_enabled() is false until
  // M1 enables a region, so the dispatcher hook is inert (one predictable-not-taken branch) and the
  // engine is bit-identical to the block-only build. Unlike the block lookup, this does NOT gate on
  // flush_gen -- trace_ok() owns all staleness (so an unrelated flush re-validates instead of dropping).
  inline bool traces_enabled() const { return m_traces_enabled; }
  inline void set_traces_enabled(bool e) { m_traces_enabled = e; }

  // the slot a head PC maps to (formation fills it). Unlike trace_lookup, returns the slot
  // unconditionally so  the caller decides whether to (re)form into it.
  inline TraceFragment* trace_slot(uint64_t head_pc) { return &m_traces[trace_index_of(head_pc)]; }
  // a loop trace that stopped looping (side-exits without closures) is a net tax, drop it. 
  inline void demote_trace(TraceFragment* tf) {
    tf->valid = false; tf->chain_entry = nullptr; tf->underrun = 0;
    ++m_vgen_cur;
    invalidate_links();
    note_trace_stale();
  }
  inline void note_trace_stale() {   // always defined (callable from trace_ok); counts only under JIT_STATS
#ifdef JIT_STATS
    m_trace_stale++;
#endif
  }
  inline void note_link_edge(JitBlock* src, uint64_t tgt_tag) {   // instrument a source block's successor fanout (poly-link sizing)
#ifdef JIT_STATS
    src->link_misses++;
    for (int i = 0; i < src->link_fanout && i < 4; ++i) if (src->link_seen[i] == tgt_tag) return;   // already counted
    if (src->link_fanout < 4) src->link_seen[src->link_fanout] = tgt_tag;
    if (src->link_fanout < 250) src->link_fanout++;
#else
    (void) src; (void) tgt_tag;
#endif
  }
#ifdef JIT_STATS
  inline void trace_entered() { m_trace_entered++; }
  inline void trace_exited()  { m_trace_exits++; }   // a trace side-exited / underran its first-pass span
#endif

  inline TraceFragment* trace_lookup(uint64_t virt_pc, uint32_t asn)
  {
    TraceFragment& t = m_traces[trace_index_of(virt_pc)];
    return (t.valid && t.head_tag == virt_pc && (t.asm_global || t.asn == asn)) ? &t : nullptr;
  }

  // Source-coherence check (review: per-segment, from M0). head_live_phys is the head's freshly
  // resolved physical; on a remap/flush since build, fall back to blocks + re-form. See the .cpp.
  bool trace_ok(TraceFragment* t, uint64_t head_live_phys, const AlphaInstructionSource& sources);

  // Lazy-flush survivor: hash-revalidate the slot in place (no interpreted pass, no re-record).
  JitBlock* revalidate_flushed(uint64_t virt_pc, uint32_t asn, bool pal_shadow,
                               uint64_t phys_pc, const AlphaInstructionSource& sources);

  JitBlock* record(uint64_t virt_pc, uint64_t phys_pc, uint32_t asn, bool pal_shadow,
                   bool asm_global, uint32_t n_instr, const AlphaInstructionSource& sources);
  void compile_block(JitBlock* b, const AlphaInstructionSource& sources, void* read_helper, void* write_helper, void* opcdec_helper, void* hw_mfpr_helper, void* hw_ld_helper, void* hw_mtpr_helper, void* hw_st_helper, void* indirect_helper, void* read_locked_helper, void* stc_helper, void* misc_helper, void* read_vpte_helper, void* read_wchk_helper, void* itof_helper, void* ftoi_helper, void* fltl_helper, void* fp_read_helper, void* fp_write_helper, void* fltv_helper);
  void flush();

  // Per-op codegen state, shared by compile_block and compile_trace within a backend.
  // Each backend defines its own register roles, forwarding state, and memory fast-path state.
  struct RegAlloc;

  // cold: opaque backend-owned per-body context. collecting outlined memop slow paths;
  // the caller emits them after its epilogue - helper calls stay out of hot path
  void emit_op(void* a, const uint8_t* gpa, void* done, const HelperSet& hs,
               bool pal_block, JitBlock* b, uint32_t ins, uint32_t i, RegAlloc& regalloc,
               void* cold, bool defer_pc = false);

  // compile an N-block trace into slot t (reuses emit_op per block; blocks fused with a guard -> side-exit
  // between them). n_blocks==1 is the single-block case; the exit returns to the dispatcher.
  void compile_trace(TraceFragment* t, JitBlock** blocks, uint32_t n_blocks, const AlphaInstructionSource& sources, const HelperSet& hs);

  void flush_non_global();   // flush only !asm_global blocks (the ASM-bit-clear / ASN icache flush)
  void reclaim_code();       // free ALL compiled code once past kReclaimBytes (cold-path only)
  // flush() can be reached from a compiled IC_FLUSH, so it DEFERS the reclaim (sets m_reclaim_pending);
  // the dispatcher calls this at a safe point (no compiled frame live) to actually free the code.
  inline void reclaim_if_pending() { if (m_reclaim_pending) { m_reclaim_pending = false; reclaim_code(); } }

  // ITB-generation counter for the indirect-chain staleness check (jit_indirect). Bumped on every
  // I-stream TB invalidate (tbia/tbiap/tbis, ACCESS_EXEC) ... those can remap a code page WITHOUT
  // flushing the JIT, so a chained block could run stale bytes.
  enum BumpCause { kBumpRemap, kBumpTbia, kBumpTbiap, kBumpTbis, kBumpFlush, kBumpFlushAsm, kBumpEvict, kBumpReclaim, kBumpMode, kBumpCauses };
  inline void     note_itb_invalidate(int cause = kBumpTbia) { ++m_itb_gen; ++m_vgen_cur; invalidate_links(cause); }
  // ASN switch: only global-source edges can hold a wrong-ASN successor.
  inline void     note_asn_change()     { invalidate_links_global(); }
  // Native entry guards check CM; traces still need their links cleared.
  inline void     note_mode_change()    { ++m_vgen_cur; invalidate_links_mode(); }
  void authorize_native(JitBlock* block, uint32_t mode); // call after checking instruction access
  // A new proof starts with no modes authorized (epoch 0 never matches m_exec_epoch).
  static inline void set_exec_proof(JitBlock& b, NativeExecProof* proof)
  {
    b.exec_proof = proof; b.exec_auth = 0; b.exec_proof_rw = nullptr;
  }
#if defined(ES40_JIT_X64)
  static void serialize_code(); // after code writes or taking ownership of generated code
#endif
  // Combined validation epoch, maintained (not summed) so the emitted chain guard reads ONE qword.
  inline uint64_t vgen() const          { return m_vgen_cur; }
#ifdef JIT_STATS
  uint64_t* exit_kind_counter(int k) { return &m_exit_kind[k]; }
#endif
  // One prologue/epilogue per engine instead of per block (x64); A64 blocks keep their own.
  void build_trampolines();
  void set_exit_slot(void** slot) { m_exit_slot = slot; if (slot) *slot = m_exit; }
  inline uint32_t run_block(CAlphaCPU* cpu, uint64_t* regs, JitBlock* b)
  {
#if defined(ES40_JIT_X64)
    return m_enter(cpu, regs, b->jit_body);
#else
    return b->code(cpu, regs);
#endif
  }
#ifdef JIT_REGPROF
  struct HostRange { uint64_t start; uint32_t size; uint32_t idx; };
  void host_code_table(std::vector<HostRange>& out) const
  {
    out.clear();
    for (int i = 0; i < kCacheEntries; ++i)
      if (m_blocks[i].code && m_blocks[i].rp_csz)
        out.push_back({ (uint64_t) (uintptr_t) (void*) m_blocks[i].code, m_blocks[i].rp_csz, (uint32_t) i });
  }
  const JitBlock& block_at(uint32_t idx) const { return m_blocks[idx]; }
#endif
  // Tagged request pointer: bit 1 = PatchSite (else LinkSlot[]), bit 0 = non-global source.
  // target_global: a global-source edge is ASN-sensitive only when its target is not global.
  // same_page_body is used only by a static native edge within one full-VA 8 KiB page.
  void patch_edge(void* tagged, uint64_t tag, uint64_t vgen_bits, void* body,
                  bool target_global = false, void* same_page_body = nullptr);
  void invalidate_links(int cause = kBumpFlush);          // every patched edge (epoch change)
  void invalidate_links_mode();     // guarded native blocks survive privilege-mode changes
  void invalidate_links_global();   // only edges out of global-source blocks (ASN change)
  // Clear links and jump-cache entries targeting this translation page.
  void invalidate_links_page(uint64_t virt_page);
  // Clear only the edges and jump-cache entry that reach an evicted block's code.
  void unlink_block(const JitBlock& v);
#ifdef JIT_STATS
  inline void note_interp_breaker(uint32_t ins) { m_rt_break_op[ins >> 26]++; }   // first interpreted op
#else
  inline void note_interp_breaker(uint32_t) {}
#endif

  // Bail-cause counters (JIT_STATS): why a compiled chain returned to the dispatcher -- a branch/
  // fall-through cached-link miss vs a computed-jump (jit_indirect) miss. Empty when stats are off,
  // so the call sites need no #ifdef.
#ifdef JIT_STATS
  void note_link_bail()   { m_bail_link++; }
  void note_jmp_attempt() { m_jmp_attempt++; }
  void note_jmp_hit()     { m_jmp_hit++; }
#else
  void note_link_bail()   {}
  void note_jmp_attempt() {}
  void note_jmp_hit()     {}
#endif

#ifdef JIT_VERIFY
  // Compare normal/shadow GPRs and combine with failures from other output checks.
  // Count each compared execution once, even when several outputs differ. Return
  // progress-print time so the dispatcher can exclude it from wall-clock RPCC.
  uint64_t verify_compare(uint64_t blk_virt, const uint64_t* interp, const uint64_t* jit,
                          const uint32_t* words, uint32_t nwords, bool other_mismatch = false);
  // A detected failure with no valid GPR comparison span (e.g. trace count divergence).
  uint64_t verify_failed();
  uint64_t verify_comparisons() const { return m_v_exec; }
  uint64_t verify_mismatches() const { return m_v_fail; }
  void trace_selftest();   // M0: unit-test trace_ok's source-coherence (SMC/IMB/ITB-remap/head-remap)
  // Side-exit-shaped trace-verify outcome: 0 = full-span compare, 1 = legitimate boundary
  // side-exit (compared at that boundary's snapshot), 2 = count mismatch (true divergence),
  // 3 = deferred-op mid-block bail. These classify run shape; verify_mismatches
  // counts output failures for full-span and boundary comparisons as well.
  void note_trace_verify(int outcome);
#endif

#ifdef JIT_STATS
  // Accumulate native vs interpreted instruction counts; prints coverage periodically.
  // Returns the wall-clock ns spent in this call's stats-print I/O (0 when it doesn't report),
  // so the dispatcher can exclude that stall from the wall-clock-pinned RPCC.
  uint64_t note_exec(uint32_t native_instr, uint32_t interp_instr, uint64_t comp_tsc = 0, uint64_t interp_tsc = 0);
  inline void note_resolver_tsc(uint64_t t) { m_tsc_resolver += t; }
#endif

#ifdef JIT_REGPROF
  // Pin-selection profiler: per-block executions (rp_hits) x the block's GPR-access mask (rp_mask),
  // summed over the live cache -> an execution-weighted histogram of which Alpha GPRs dominate the
  // hot path. Prints the top registers periodically so we can choose the global pin set.
  void regprof_report();
#endif

private:
  // Compiled code embeds these values; the profile cannot change during reuse.
  const AlphaCPUProfile m_profile;
  bool m_smp_memory = false;
  uintptr_t m_ll_buckets = 0;
  uintptr_t m_ll_watch = 0;
  // Both caches are heap allocations (see the ctor), preferring large/huge pages: the block
  // cache is ~40 MB indexed by PC hash, effectively random access, so 4K pages thrash the
  JitBlock* m_blocks;
  uint8_t*  m_set_rr;        // per-set round-robin victim cursor (only consulted when every way is live)
  TraceFragment* m_traces;   // M0+: the trace tier's cache (inert until M1)
  bool     m_traces_enabled = false;       // global kill-switch; default OFF -> bit-identical
  int      m_cpu_id;
  uint64_t m_recorded;
  uint64_t m_itb_gen = 0; // current ITB generation (bumped on every I-stream TB invalidate)
  uint64_t m_flush_gen = 0; // current icache-flush generation (bumped by flush(); lazy IC_FLUSH/IMB)
  uint64_t m_non_global_flush_gen = 0; // process-specific code: bumped by either kind of flush
  uint64_t m_vgen_cur = 0;  // maintained epoch = itb + flush + non-global-flush bumps 
  uint64_t m_exec_epoch = 1; // permission/coherence changes, excluding CM switches
  alignas(64) JumpCacheEntry m_jcache[kJumpCacheEntries] = {};   // computed-jump cache (per CPU)
  uint64_t m_jc_gen = 1;    // every invalidate_links() retires all jump-cache entries
  JitEnterFn m_enter = nullptr;   // shared prologue (x64)
  void*      m_exit = nullptr;    // shared epilogue (x64)
  void**     m_exit_slot = nullptr;   // CPU field that compiled code jumps through
#ifdef JIT_REGPROF
  AlphaInstructionSource m_dbg_source; // Bound reader, never a borrowed source view.
#endif
  JitFlatIndex<void*> m_active_links{8192};     // patched edges, bit 1 = PatchSite
  JitFlatIndex<void*> m_global_src_links;       // subset with global sources
  // ITB matching ignores VA bits 63:48. Use the same mask for link lookup
  // and TBIS, including sign-extended aliases.
  static constexpr uint64_t translation_page_key(uint64_t address) {
    return (address & UINT64_C(0x0000ffffffffe000)) >> 13;
  }
  // Target page -> patched edges: one chain per page through m_edges. A page clear marks its
  // entries dead and a re-patch revives them, so m_edges only grows with distinct (page, edge)
  // pairs and is emptied with the other link sets.
  struct EdgeKey
  {
    uint64_t page;
    void*    edge;
    bool operator==(const EdgeKey& o) const { return page == o.page && edge == o.edge; }
    friend uint32_t jit_flat_hash(const EdgeKey& k)
    {
      return jit_flat_hash(k.page * UINT64_C(0x100000001b3) ^ (uint64_t) (uintptr_t) k.edge);
    }
  };
  struct EdgeEntry { void* edge; int32_t next; bool live; };
  JitFlatIndex<uint64_t> m_page_edges{4096};    // target page -> first live m_edges entry, or -1
  JitFlatIndex<EdgeKey>  m_edge_entries{8192};  // (page, edge) -> its m_edges entry
  std::vector<EdgeEntry> m_edges;
  void note_edge_page(uint64_t page, void* key);
  static void clear_edge(void* key);            // un-patch a site or empty a LinkSlot array
  uint64_t m_code_bytes;  // compiled bytes since last reclaim (see flush())
  bool     m_reclaim_pending = false;   // flush() hit kReclaimBytes; reclaim at the next dispatch boundary
  void*    m_rt;          // asmjit::JitRuntime*
  JitOffsets m_off = {};  // field offsets for the inline load fast path
#ifdef JIT_DISASM
  FILE*    m_disasm_fp = nullptr;   // per-CPU disassembly trace file (jit_disasm_cpuN.txt)
#endif
#ifdef JIT_VERIFY
  uint64_t m_v_exec, m_v_fail;
  uint64_t record_verify_result(bool mismatch);
  uint64_t m_tv_cnt[4];   // trace-verify outcomes: full-span / boundary side-exit / count mismatch / deferred-op bail
#endif
#ifdef JIT_STATS
  uint64_t m_stat_native, m_stat_interp;        // windowed: instrs run native vs interpreted
  uint64_t m_stat_hot, m_stat_miss;             // windowed: compiled-chain dispatches, interp blocks
  uint64_t m_stat_compiled, m_stat_plen_sum;    // cumulative: compiled blocks, sum of their lengths
  uint64_t m_stat_code_bytes;                   // cumulative: emitted host-code bytes (code expansion = /plen_sum)
  uint64_t m_stat_wall_last_ns;                 // steady_clock ns at the last window report (throughput delta)
  uint64_t m_tsc_compiled, m_tsc_interp;        // windowed: host TSC cycles in b->code() vs interp fallback
  uint64_t m_tsc_resolver;                      // windowed: host TSC cycles inside jit_indirect (billed under compiled)
  uint64_t m_tsc_window_start;                  // host TSC at window start (the time-split denominator)
  uint64_t m_bail_link, m_jmp_attempt, m_jmp_hit;   // windowed: link-miss bails, jit_indirect attempts/hits
  uint64_t m_fresh_cold, m_fresh_tag, m_fresh_asn, m_fresh_phys, m_fresh_hash;  // windowed: record() step-4 fresh-compile reason
  uint64_t m_trace_formed, m_trace_entered, m_trace_exits, m_trace_stale;       // windowed: trace tier activity (M1+)
  uint64_t m_link_invalidations, m_links_cleared;      // windowed: epoch bumps / edges cleared
  uint64_t m_asn_link_clears, m_asn_links_cleared;     // windowed: ASN switches / global-source edges cleared
  uint64_t m_evict_link_clears, m_evict_links_cleared; // windowed: cross-ASN evictions unlinked / edges they cleared
  uint64_t m_bump_cause[kBumpCauses];                  // windowed: epoch bumps by cause
  uint64_t m_page_clears, m_page_links_cleared;        // windowed: single-page ITB invalidates / edges they cleared
  uint64_t m_direct_patches, m_patch_unreachable;      // windowed: sites patched / beyond rel32 reach
  uint64_t m_reclaims;                                 // cumulative: code reclaims
  uint64_t m_jmp_exits;                                // windowed: computed-jump exits
  uint64_t m_jc_hits;                                  // windowed: computed exits served by the jump cache
  uint64_t m_exit_kind[12];                            // windowed: dispatcher returns by cause (x64 emitter)
  uint64_t m_mb_exec;                                  // windowed: MB fences executed
  uint64_t m_rt_break_op[64];                          // windowed: interp dispatches by breaking opcode
  uint64_t m_licm_same, m_licm_diff;   // region memops hitting the same page as last time
  uint64_t m_licm_pool[4096];          // per-memop last-page slots (shared pool; collisions just blur the stat)
  uint32_t m_licm_next;
  uint64_t m_term_op[64];                       // cumulative: opcode that ended a block's compiled prefix
  uint64_t m_pal_func[256];                     // cumulative: CALL_PAL function code that ended a block
  uint64_t m_mtpr_encoding_hi[256];             // cumulative: raw HW_MTPR instruction[15:8] that ended a block
  uint64_t m_hwld_func[16];                     // cumulative: HW_LD (0x1b) form (ins>>12 & 0xf) that ended a block
  uint64_t m_misc_func[16];                     // cumulative: MISC (0x18) Ra==31 form (ins>>12 & 0xf: RPCC/RC/RS) that ended a block
  bool     m_first_breaker_logged;              // one-shot guard for the punch-list print
#endif
};

#endif // ES40_JIT
#endif // INCLUDED_JITENGINE_H

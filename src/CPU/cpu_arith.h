/* ES40 Emulator.
 * Copyright (C) 2007-2008 by the ES40 Emulator Project
 * Copyright (C) 2025-2026 by gdwnldsKSC
 *
 * WWW    : https://github.com/ES40-Emu/es40
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 * Although this is not required, the author would appreciate being notified of,
 * and receiving any modifications you may make to the source code that might serve
 * the general public.
 *
 */

 /**
  * \file
  * Contains code macros for the processor integer arithmetic instructions.
  * Based on ARM chapter 4.4.
  *
  * $Id$
  *
  * X-1.14       Camiel Vanderhoeven                             14-MAR-2008
  *   1. More meaningful exceptions replace throwing (int) 1.
  *   2. U64 macro replaces X64 macro.
  *
  * X-1.13       Camiel Vanderhoeven                             28-JAN-2008
  *      Better floating-point exception handling.
  *
  * X-1.12      Camiel Vanderhoeven                             24-JAN-2008
  *      Fixed some overflow-detection issues.
  *
  * X-1.11      Camiel Vanderhoeven                             22-JAN-2008
  *      Also fixed MULQ/V.
  *
  * X-1.10      Camiel Vanderhoeven                             22-JAN-2008
  *      Use RA, RAV style macro's for integer registers; fixed MULQ.
  *
  * X-1.9       Camiel Vanderhoeven                             22-JAN-2008
  *      Implemented missing /V integer instructions.
  *
  * X-1.8       Camiel Vanderhoeven                             21-JAN-2008
  *      Fixed misunderstanding of the INT bit in integer overflow traps.
  *
  * X-1.7       Camiel Vanderhoeven                             18-JAN-2008
  *      Replaced sext_64 inlines with sext_u64_<bits> inlines for
  *      performance reasons (thanks to David Hittner for spotting this!);
  *
  * X-1.6        David Hittner                                   16-JAN-2008
  *      Added ADDL/V instruction
  *
  * X-1.5        Camiel Vanderhoeven                             2-DEC-2007
  *      Use sext_64 inline.
  *
  * X-1.4        Camiel Vanderhoeven                             11-APR-2007
  *      Moved all data that should be saved to a state file to a structure
  *      "state".
  *
  * X-1.3        Camiel Vanderhoeven                             30-MAR-2007
  *      Added old changelog comments.
  *
  * X-1.2        Camiel Vanderhoeven                             18-MAR-2007
  *      Bugfix in CTLZ and CTTZ instructions. Fixes the INCON_SCHED
  *      bugcheck (bug # 1680064).
  *
  * X-1.1        Camiel Vanderhoeven                             18-FEB-2007
  *      File created. Contains code previously found in AlphaCPU.h
  *
  * \author Camiel Vanderhoeven
  **/

#ifdef DEBUG_ARITH_TRAP
#define ARITH_TRAP_PRINTF(...) printf(__VA_ARGS__)
#else
#define ARITH_TRAP_PRINTF(...) ((void) 0)
#endif

  /* comparison */
#define DO_CMPEQ  RCV = (RAV == RBV) ? 1 : 0;
#define DO_CMPLT  RCV = ((s64) RAV < (s64) RBV) ? 1 : 0;
#define DO_CMPLE  RCV = ((s64) RAV <= (s64) RBV) ? 1 : 0;

/* addition */
#define DO_ADDQ   RCV = RAV + RBV;
#define DO_S4ADDQ RCV = (RAV * 4) + RBV;
#define DO_S8ADDQ RCV = (RAV * 8) + RBV;

#define DO_ADDQ_V                                                               \
  {                                                                             \
    u64 rav = RAV;                                                              \
    u64 rbv = RBV;                                                              \
    RCV = rav + rbv;                                                            \
                                                                             \
    /* test for integer overflow */                                             \
    if(((~rav ^ rbv) & (rav ^ RCV)) & Q_SIGN)                                   \
    {                                                                           \
      ARITH_TRAP_I(TRAP_IOV, I_GETRC(ins));                                     \
      ARITH_TRAP_PRINTF("ADDQ_V %016" PRIx64 " + %016" PRIx64 " = %016" PRIx64 " + TRAP.\n", \
                        rav, rbv, RCV);                                          \
    }                                                                           \
  }

#define DO_ADDL   RCV = sext_u64_32(RAV + RBV);
#define DO_S4ADDL RCV = sext_u64_32((RAV * 4) + RBV);
#define DO_S8ADDL RCV = sext_u64_32((RAV * 8) + RBV);

#define DO_ADDL_V                                                               \
  {                                                                             \
    u64 rav = RAV;                                                              \
    u64 rbv = RBV;                                                              \
    RCV = sext_u64_32(rav + rbv);                                               \
                                                                             \
    /* test for integer overflow */                                             \
    if(((~rav ^ rbv) & (rav ^ RCV)) & L_SIGN)                                   \
    {                                                                           \
      ARITH_TRAP_I(TRAP_IOV, I_GETRC(ins));                                     \
      ARITH_TRAP_PRINTF("ADDL_V %016" PRIx64 " + %016" PRIx64 " = %016" PRIx64 " + TRAP.\n", \
                        rav, rbv, RCV);                                          \
    }                                                                           \
  }

#ifndef _MSC_VER
#define DO_CTLZ   temp_64 = 0; \
  temp_64_2 = RBV;             \
  for(i = 63; i >= 0; i--)     \
    if((temp_64_2 >> i) & 1)   \
      break;                   \
    else                       \
      temp_64++;               \
  RCV = temp_64;

#define DO_CTPOP  temp_64 = 0; \
  temp_64_2 = RBV;             \
  for(i = 0; i < 64; i++)      \
    if((temp_64_2 >> i) & 1)   \
      temp_64++;               \
  RCV = temp_64;

#define DO_CTTZ   temp_64 = 0; \
  temp_64_2 = RBV;             \
  for(i = 0; i < 64; i++)      \
    if((temp_64_2 >> i) & 1)   \
      break;                   \
    else                       \
      temp_64++;               \
  RCV = temp_64;
#else
/* === Fast bit-ops: use x86-64/MSVC intrinsics when available === */
#if defined(_MSC_VER) && defined(_M_X64)

#include <intrin.h>

__forceinline static unsigned __int64 alpha_clz64(unsigned __int64 x) {
  unsigned long idx;
  return _BitScanReverse64(&idx, x) ? (unsigned __int64)(63u - idx) : 64ull;
}

__forceinline static unsigned __int64 alpha_ctz64(unsigned __int64 x) {
  unsigned long idx;
  return _BitScanForward64(&idx, x) ? (unsigned __int64)idx : 64ull;
}

__forceinline static unsigned __int64 alpha_popcnt64(unsigned __int64 x) {
  return (unsigned __int64)__popcnt64(x);
}

#define ES40_HAVE_FAST_BITOPS 1
#else
  /* Portable fallbacks */
__forceinline static unsigned __int64 alpha_clz64(unsigned __int64 x) {
  if (!x) return 64ull;
  unsigned __int64 n = 0;
  for (int i = 63; i >= 0; --i) { if ((x >> i) & 1) break; else ++n; }
  return n;
}
__forceinline static unsigned __int64 alpha_ctz64(unsigned __int64 x) {
  if (!x) return 64ull;
  unsigned __int64 n = 0;
  for (int i = 0; i < 64; ++i) { if ((x >> i) & 1) break; else ++n; }
  return n;
}
__forceinline static unsigned __int64 alpha_popcnt64(unsigned __int64 x) {
  unsigned __int64 n = 0;
  for (int i = 0; i < 64; ++i) { n += (x >> i) & 1; }
  return n;
}
#endif
/* ================================================================= */
#define DO_CTLZ   RCV = alpha_clz64((u64)RBV);

#define DO_CTPOP  RCV = alpha_popcnt64((u64)RBV);

#define DO_CTTZ   RCV = alpha_ctz64((u64)RBV);
#endif

#define DO_CMPULT RCV = ((u64) RAV < (u64) RBV) ? 1 : 0;
#define DO_CMPULE RCV = ((u64) RAV <= (u64) RBV) ? 1 : 0;

/* multiplication */
#define DO_MULL RCV = sext_u64_32(sext_u64_32(RAV) * sext_u64_32(RBV));

#define DO_MULL_V                                                               \
  {                                                                             \
    u64 rav = RAV;                                                              \
    u64 rbv = RBV;                                                              \
    u64 sr = sext_u64_32(rav) * sext_u64_32(rbv);                               \
    RCV = sext_u64_32(sr);                                                      \
    if((RCV ^ sr) & U64(0xffffffff00000000))                                    \
    {                                                                           \
      ARITH_TRAP_I(TRAP_IOV, I_GETRC(ins));                                     \
      ARITH_TRAP_PRINTF("MULL_V %016" PRIx64 " * %016" PRIx64 " = %016" PRIx64 " + TRAP.\n", \
                        rav, rbv, RCV);                                          \
    }                                                                           \
  }

#define DO_MULQ RCV = RAV * RBV;

#define DO_MULQ_V                                                               \
  {                                                                             \
    u64 rav = RAV;                                                              \
    u64 rbv = RBV;                                                              \
    u64 t64;                                                                    \
    RCV = uemul64(rav, rbv, &t64);                                              \
    if(Q_GETSIGN(rav))                                                          \
      t64 -= rbv;                                                               \
    if(Q_GETSIGN(rbv))                                                          \
      t64 -= rav;                                                               \
    if(Q_GETSIGN(RCV) ? (t64 != X64_QUAD) : (t64 != 0))                         \
    {                                                                           \
      ARITH_TRAP_I(TRAP_IOV, I_GETRC(ins));                                     \
      ARITH_TRAP_PRINTF("MULQ_V %016" PRIx64 " * %016" PRIx64 " = %016" PRIx64 " + TRAP.\n", \
                        rav, rbv, RCV);                                          \
    }                                                                           \
  }

#define DO_UMULH  uemul64(RAV, RBV, &RCV);

/* subtraction */
#define DO_SUBQ   RCV = RAV - RBV;
#define DO_S4SUBQ RCV = (RAV * 4) - RBV;
#define DO_S8SUBQ RCV = (RAV * 8) - RBV;

#define DO_SUBQ_V                                                               \
  {                                                                             \
    u64 rav = RAV;                                                              \
    u64 rbv = RBV;                                                              \
    RCV = rav - rbv;                                                            \
                                                                             \
    /* test for integer overflow */                                             \
    if(((rav ^ rbv) & (rav ^ RCV)) & Q_SIGN)                                    \
    {                                                                           \
      ARITH_TRAP_I(TRAP_IOV, I_GETRC(ins));                                     \
      ARITH_TRAP_PRINTF("SUBQ_V %016" PRIx64 " - %016" PRIx64 " = %016" PRIx64 " + TRAP.\n", \
                        rav, rbv, RCV);                                          \
    }                                                                           \
  }

#define DO_SUBL   RCV = sext_u64_32(RAV - RBV);
#define DO_S4SUBL RCV = sext_u64_32((RAV * 4) - RBV);
#define DO_S8SUBL RCV = sext_u64_32((RAV * 8) - RBV);

#define DO_SUBL_V                                                               \
  {                                                                             \
    u64 rav = RAV;                                                              \
    u64 rbv = RBV;                                                              \
    RCV = sext_u64_32(rav - rbv);                                               \
                                                                             \
    /* test for integer overflow */                                             \
    if(((rav ^ rbv) & (rav ^ RCV)) & L_SIGN)                                    \
    {                                                                           \
      ARITH_TRAP_I(TRAP_IOV, I_GETRC(ins));                                     \
      ARITH_TRAP_PRINTF("SUBL_V %016" PRIx64 " - %016" PRIx64 " = %016" PRIx64 " + TRAP.\n", \
                        rav, rbv, RCV);                                          \
    }                                                                           \
  }

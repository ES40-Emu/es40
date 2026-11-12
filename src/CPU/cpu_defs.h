/* ES40 Emulator.
 * Copyright (C) 2007-2008 by the ES40 Emulator Project
 * Copyright (C) 2023, 2025-2026 by gdwnldsKSC
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
 * Parts of this file are based upon the SIMH Alpha simulator:
 *
 * Copyright (c) 2003-2006, Robert M Supnik
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * ROBERT M SUPNIK BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * Except as contained in this notice, the name of Robert M Supnik shall not be
 * used in advertising or otherwise to promote the sale, use or other dealings
 * in this Software without prior written authorization from Robert M Supnik.
 *
 * The SIMH square root and convert-to-integer routines this file derives
 * from are in turn derived from SoftFloat, under the following terms:
 *
 * Written by John R. Hauser.  This work was made possible in part by the
 * International Computer Science Institute, located at Suite 600, 1947 Center
 * Street, Berkeley, California 94704.  Funding was partially provided by the
 * National Science Foundation under grant MIP-9311980.  The original version
 * of this code was written as part of a project to build a fixed-point vector
 * processor in collaboration with the University of California at Berkeley,
 * overseen by Profs. Nelson Morgan and John Wawrzynek.  More information
 * is available through the Web page 'http://www.cs.berkeley.edu/~jhauser/
 * arithmetic/SoftFloat.html'.
 *
 * THIS SOFTWARE IS DISTRIBUTED AS IS, FOR FREE.  Although reasonable effort has
 * been made to avoid it, THIS SOFTWARE MAY CONTAIN FAULTS THAT WILL AT TIMES
 * RESULT IN INCORRECT BEHAVIOR.  USE OF THIS SOFTWARE IS RESTRICTED TO PERSONS
 * AND ORGANIZATIONS WHO CAN AND WILL TAKE FULL RESPONSIBILITY FOR ALL LOSSES,
 * COSTS, OR OTHER PROBLEMS THEY INCUR DUE TO THE SOFTWARE, AND WHO FURTHERMORE
 * EFFECTIVELY INDEMNIFY JOHN HAUSER AND THE INTERNATIONAL COMPUTER SCIENCE
 * INSTITUTE (possibly via similar legal warning) AGAINST ALL LOSSES, COSTS, OR
 * OTHER PROBLEMS INCURRED BY THEIR CUSTOMERS AND CLIENTS DUE TO THE SOFTWARE.
 *
 * Derivative works are acceptable, even for commercial purposes, so long as
 * (1) the source code for the derivative work includes prominent notice that
 * the work is derivative, and (2) the source code includes prominent notice with
 * these four paragraphs for those parts of this code that are retained.
 */

 /**
  * \file
  * Contains some macro definitions and some inline functions for the Alpha CPU.
  *
  * $Id$
  *
  * X-1.15       Camiel Vanderhoeven                             15-MAR-2009
  *      Fixed a bug in unaligned memory accesses crossing a page boundary,
  *      discovered by Volker Halle.
  *
  * X-1.14       Camiel Vanderhoeven                             12-JUN-2008
  *      Support for last written and last read memory locations.
  *
  * X-1.13       Camiel Vanderhoeven                             14-MAR-2008
  *      Formatting.
  *
  * X-1.12       Camiel Vanderhoeven                             14-MAR-2008
  *   1. More meaningful exceptions replace throwing (int) 1.
  *   2. U64 macro replaces X64 macro.
  *
  * X-1.11       Camiel Vanderhoeven                             05-MAR-2008
  *      Multi-threading version.
  *
  * X-1.10       Camiel Vanderhoeven                             08-FEB-2008
  *      Show originating device name on memory errors.
  *
  * X-1.9        Camiel Vanderhoeven                             05-FEB-2008
  *      Bug description added.
  *
  * X-1.8        Camiel Vanderhoeven                             01-FEB-2008
  *      Disable unaligned access check alltogether; it doesn't work
  *      properly for some reason.
  *
  * X-1.7        Camiel Vanderhoeven                             01-FEB-2008
  *      Avoid unnecessary shift-operations to calculate constant values.
  *
  * X-1.6        Camiel Vanderhoeven                             28-JAN-2008
  *      Better floating-point exception handling.
  *
  * X-1.5        Brian Wheeler                                   26-JAN-2008
  *      Make file end in newline.
  *
  * X-1.4        Camiel Vanderhoeven                             26-JAN-2008
  *      Do unaligned trap only when a page boundary is crossed. Something
  *      is causing alignment traps in the SRM console, with the DAT bit set
  *      to false, and no OS handler in place. Also, when OpenVMS boots there
  *      are alignment traps that shouldn't happen. None of these cross page
  *      boundaries, so we're safe for now.
  *
  * X-1.3        Camiel Vanderhoeven                             25-JAN-2008
  *      Trap on unalogned memory access. The previous implementation where
  *      unaligned accesses were silently allowed could go wrong when page
  *      boundaries are crossed.
  *
  * X-1.2        Camiel Vanderhoeven                             22-JAN-2008
  *      Added RA, RAV style macro's for integer registers.
  *
  * X-1.1        Camiel Vanderhoeven                             21-JAN-2008
  *      File created. Contains code pulled from various older source files,
  *      and some floating-point definitions based upon the SIMH Alpha pre-
  *      implementation, which is Copyright (c) 2003, Robert M Supnik.
  *
  * \bug Fix unaligned access traps.
  **/
#if !defined(__CPU_DEFS__)
#define __CPU_DEFS__

  /* Instruction formats */
#define I_V_OP        26        /* opcode */
#define I_M_OP        0x3F
#define I_OP          (I_M_OP << I_V_OP)
#define I_V_RA        21        /* Ra */
#define I_M_RA        0x1F
#define I_V_RB        16        /* Rb */
#define I_M_RB        0x1F
#define I_V_FTRP      13        /* floating trap mode */
#define I_M_FTRP      0x7
#define I_FTRP        (I_M_FTRP << I_V_FTRP)
#define I_F_VAXRSV    0x4800    /* VAX reserved */
#define I_FTRP_V      0x2000    /* /V trap */
#define I_FTRP_U      0x2000    /* /U trap */
#define I_FTRP_S      0x8000    /* /S trap */
#define I_FTRP_SUI    0xE000    /* /SUI trap */
#define I_FTRP_SVI    0xE000    /* /SVI trap */
#define I_V_FRND      11        /* floating round mode */
#define I_M_FRND      0x3
#define I_FRND        (I_M_FRND << I_V_FRND)
#define I_FRND_C      0         /* chopped */
#define I_FRND_M      1         /* to minus inf */
#define I_FRND_N      2         /* normal */
#define I_FRND_D      3         /* dynamic */
#define I_FRND_P      3         /* in FPCR: plus inf */
#define I_V_FSRC      9         /* floating source */
#define I_M_FSRC      0x3
#define I_FSRC        (I_M_FSRC << I_V_FSRC)
#define I_FSRC_X      0x0200    /* data type X */
#define I_V_FFNC      5         /* floating function */
#define I_M_FFNC      0x3F
#define I_V_LIT8      13        /* integer 8b literal */
#define I_M_LIT8      0xFF
#define I_V_ILIT      12        /* literal flag */
#define I_ILIT        (1u << I_V_ILIT)
#define I_V_IFNC      5         /* integer function */
#define I_M_IFNC      0x3F
#define I_V_RC        0         /* Rc */
#define I_M_RC        0x1F
#define I_V_MDSP      0         /* memory displacement */
#define I_M_MDSP      0xFFFF
#define I_V_BDSP      0
#define I_M_BDSP      0x1FFFFF  /* branch displacement */
#define I_V_PALOP     0
#define I_M_PALOP     0x3FFFFFF /* PAL subopcode */
#define I_GETOP(x)    (((x) >> I_V_OP) & I_M_OP)
#define I_GETRA(x)    (((x) >> I_V_RA) & I_M_RA)
#define I_GETRB(x)    (((x) >> I_V_RB) & I_M_RB)
#define I_GETLIT8(x)  (((x) >> I_V_LIT8) & I_M_LIT8)
#define I_GETIFNC(x)  (((x) >> I_V_IFNC) & I_M_IFNC)
#define I_GETFRND(x)  (((x) >> I_V_FRND) & I_M_FRND)
#define I_GETFFNC(x)  (((x) >> I_V_FFNC) & I_M_FFNC)
#define I_GETRC(x)    (((x) >> I_V_RC) & I_M_RC)
#define I_GETMDSP(x)  (((x) >> I_V_MDSP) & I_M_MDSP)
#define I_GETBDSP(x)  (((x) >> I_V_BDSP) & I_M_BDSP)
#define I_GETPAL(x)   (((x) >> I_V_PALOP) & I_M_PALOP)

/* Floating point types */
#define DT_F  0 /* type F */
#define DT_G  1 /* type G */
#define DT_S  0 /* type S */
#define DT_T  1 /* type T */

/* Floating point memory format (VAX F) */
#define F_V_SIGN      15
#define F_SIGN        (1u << F_V_SIGN)
#define F_V_EXP       7
#define F_M_EXP       0xFF
#define F_BIAS        0x80
#define F_EXP         (F_M_EXP << F_V_EXP)
#define F_V_FRAC      29
#define F_GETEXP(x)   (((x) >> F_V_EXP) & F_M_EXP)
#define SWAP_VAXF(x)  ((((x) >> 16) & 0xFFFF) | (((x) & 0xFFFF) << 16))

/* Floating point memory format (VAX G) */
#define G_V_SIGN    15
#define G_SIGN      (1u << F_V_SIGN)
#define G_V_EXP     4
#define G_M_EXP     0x7FF
#define G_BIAS      0x400
#define G_EXP       (G_M_EXP << G_V_EXP)
#define G_GETEXP(x) (((x) >> G_V_EXP) & G_M_EXP)
#define SWAP_VAXG(x)                                \
    (                                               \
      (((x) & U64(0x000000000000FFFF)) << 48) |     \
        (((x) & U64(0x00000000FFFF0000)) << 16) |   \
          (((x) >> 16) & U64(0x00000000FFFF0000)) | \
            (((x) >> 48) & U64(0x000000000000FFFF)) \
    )

/* Floating memory format (IEEE S) */
#define S_V_SIGN    31
#define S_SIGN      (1u << S_V_SIGN)
#define S_V_EXP     23
#define S_M_EXP     0xFF
#define S_BIAS      0x7F
#define S_NAN       0xFF
#define S_EXP       (S_M_EXP << S_V_EXP)
#define S_V_FRAC    29
#define S_GETEXP(x) (((x) >> S_V_EXP) & S_M_EXP)

/* Floating point memory format (IEEE T) */
#define T_V_SIGN    63
#define T_SIGN      U64(0x8000000000000000)
#define T_V_EXP     52
#define T_M_EXP     0x7FF
#define T_BIAS      0x3FF
#define T_NAN       0x7FF
#define T_EXP       U64(0x7FF0000000000000)
#define T_FRAC      U64(0x000FFFFFFFFFFFFF)
#define T_GETEXP(x) (((u32) ((x) >> T_V_EXP)) & T_M_EXP)

/* Floating point register format (all except VAX D) */
#define FPR_V_SIGN      63
#define FPR_SIGN        U64(0x8000000000000000)
#define FPR_V_EXP       52
#define FPR_M_EXP       0x7FF
#define FPR_NAN         0x7FF
#define FPR_EXP         U64(0x7FF0000000000000)
#define FPR_HB          U64(0x0010000000000000)
#define FPR_FRAC        U64(0x000FFFFFFFFFFFFF)
#define FPR_GUARD       (UF_V_NM - FPR_V_EXP)
#define FPR_GETSIGN(x)  (((u32) ((x) >> FPR_V_SIGN)) & 1)
#define FPR_GETEXP(x)   (((u32) ((x) >> FPR_V_EXP)) & FPR_M_EXP)
#define FPR_GETFRAC(x)  ((x) & FPR_FRAC)
#define FP_TRUE         U64(0x4000000000000000) /* 0.5/2.0 in reg */

/* Floating point register format (VAX D) */
#define FDR_V_SIGN      63
#define FDR_SIGN        U64(0x8000000000000000)
#define FDR_V_EXP       55
#define FDR_M_EXP       0xFF
#define FDR_EXP         U64(0x7F80000000000000)
#define FDR_HB          U64(0x0080000000000000)
#define FDR_FRAC        U64(0x007FFFFFFFFFFFFF)
#define FDR_GUARD       (UF_V_NM - FDR_V_EXP)
#define FDR_GETSIGN(x)  (((u32) ((x) >> FDR_V_SIGN)) & 1)
#define FDR_GETEXP(x)   (((u32) ((x) >> FDR_V_EXP)) & FDR_M_EXP)
#define FDR_GETFRAC(x)  ((x) & FDR_FRAC)
#define D_BIAS          0x80

static inline u64 dram_read(const char* dram_ptr, u64 phys, int dsize)
{
  const char* p = dram_ptr + phys;
  switch (dsize) {
  case  8: return *(const u8*)p;
  case 16: return *(const u16*)p;
  case 32: return *(const u32*)p;
  case 64: return *(const u64*)p;
  default: return 0; // unreachable in practice
  }
}

static inline void dram_write(char* dram_ptr, u64 phys, int dsize, u64 data)
{
  char* p = dram_ptr + phys;
  switch (dsize) {
  case  8: *(u8*)p = (u8)data;  break;
  case 16: *(u16*)p = (u16)data; break;
  case 32: *(u32*)p = (u32)data; break;
  case 64: *(u64*)p = data;      break;
  }
}

/* Atomic compare-and-swap on a DRAM location for the emulator's MP LL/SC model.
 * Used only when STx_C targets the same physical address as the matching LDx_L. */
#if defined(_MSC_VER)
#include <intrin.h>
static inline bool dram_cas32(char* p, u32 expected, u32 desired)
{
  return (u32)_InterlockedCompareExchange((volatile long*)p, (long)desired, (long)expected) == expected;
}
static inline bool dram_cas64(char* p, u64 expected, u64 desired)
{
  return (u64)_InterlockedCompareExchange64((volatile long long*)p, (long long)desired, (long long)expected) == expected;
}
#else
static inline bool dram_cas32(char* p, u32 expected, u32 desired)
{
  return __atomic_compare_exchange_n((u32*)p, &expected, desired, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}
static inline bool dram_cas64(char* p, u64 expected, u64 desired)
{
  return __atomic_compare_exchange_n((u64*)p, &expected, desired, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}
#endif
static inline bool dram_cas(char* dram_ptr, u64 phys, u64 expected, u64 desired, int size)
{
  char* p = dram_ptr + phys;
  return (size == 32) ? dram_cas32(p, (u32)expected, (u32)desired)
                      : dram_cas64(p, expected, desired);
}

/* Unpacked floating point number */
struct ufp
{
  u32 sign;
  s32 exp;
  u64 frac;
};

typedef struct ufp  UFP;

#define UF_V_NM 63
#define UF_NM   U64(0x8000000000000000)         /* normalized */

/* Bit patterns */
#define X64_BYTE      U64(0xff)
#define X64_WORD      U64(0xffff)
#define X64_LONG      U64(0xffffffff)
#define X64_QUAD      U64(0xffffffffffffffff)
#define B_SIGN        U64(0x80)
#define W_SIGN        U64(0x8000)
#define L_SIGN        U64(0x80000000)
#define Q_SIGN        U64(0x8000000000000000)
#define Q_GETSIGN(x)  (((x) >> 63) & 1)

/* IEEE control register (left 32b only) */
#define FPCR_SUM        U64(0x8000000000000000) /* summary */
#define FPCR_INED       U64(0x4000000000000000) /* inexact disable */
#define FPCR_UNFD       U64(0x2000000000000000) /* underflow disable */
#define FPCR_UNDZ       U64(0x1000000000000000) /* underflow to 0 */
#define FPCR_V_RMOD     58  /* rounding mode */
#define FPCR_M_RMOD     0x3
#define FPCR_IOV        U64(0x0200000000000000) /* integer overflow */
#define FPCR_INE        U64(0x0100000000000000) /* inexact */
#define FPCR_UNF        U64(0x0080000000000000) /* underflow */
#define FPCR_OVF        U64(0x0040000000000000) /* overflow */
#define FPCR_DZE        U64(0x0020000000000000) /* div by zero */
#define FPCR_INV        U64(0x0010000000000000) /* invalid operation */
#define FPCR_OVFD       U64(0x0008000000000000) /* overflow disable */
#define FPCR_DZED       U64(0x0004000000000000) /* div by zero disable */
#define FPCR_INVD       U64(0x0002000000000000) /* invalid op disable */
#define FPCR_DNZ        U64(0x0001000000000000) /* denormal to zero */
#define FPCR_DNOD       U64(0x0000800000000000) /* denormal disable */
#define FPCR_RAZ        U64(0x00007FFF00000000) /* zero */
#define FPCR_ERR        (FPCR_IOV | FPCR_INE | FPCR_UNF | FPCR_OVF | FPCR_DZE | FPCR_INV)
#define FPCR_GETFRND(x) (((x) >> FPCR_V_RMOD) & FPCR_M_RMOD)
#define NEG_Q(x)        ((~(x) + 1) & X64_QUAD)
#define ABS_Q(x)        (((x) & Q_SIGN) ? NEG_Q(x) : (x))

/* IEEE */
#define UFT_ZERO    0 /* unpacked: zero */
#define UFT_FIN     1 /* finite */
#define UFT_DENORM  2 /* denormal */
#define UFT_INF     3 /* infinity */
#define UFT_NAN     4 /* not a number */

#define Q_FINITE(x) ((x) <= UFT_FIN)  /* finite */
#define Q_SUI(x)    (((x) & I_FTRP) == I_FTRP_SVI)

/* 64b * 64b unsigned multiply */
inline u64 uemul64(u64 a, u64 b, u64* hi)
{
#if defined(_MSC_VER) && defined(_M_X64)
  /* Full 128b product in one MULX; bit-identical to the portable path below. */
  u64 h;
  const u64 lo = _umul128(a, b, &h);
  if (hi)
    *hi = h;
  return lo;
#elif defined(__SIZEOF_INT128__)
  const unsigned __int128 p = (unsigned __int128)a * (unsigned __int128)b;
  if (hi)
    *hi = (u64)(p >> 64);
  return (u64)p;
#else
  u64 ahi;

  u64 alo;

  u64 bhi;

  u64 blo;

  u64 rhi;

  u64 rmid1;

  u64 rmid2;

  u64 rlo;

  ahi = (a >> 32) & X64_LONG;
  alo = a & X64_LONG;
  bhi = (b >> 32) & X64_LONG;
  blo = b & X64_LONG;
  rhi = ahi * bhi;
  rmid1 = ahi * blo;
  rmid2 = alo * bhi;
  rlo = alo * blo;
  rhi = rhi + ((rmid1 >> 32) & X64_LONG) + ((rmid2 >> 32) & X64_LONG);
  rmid1 = (rmid1 << 32) & X64_QUAD;
  rmid2 = (rmid2 << 32) & X64_QUAD;
  rlo = (rlo + rmid1) & X64_QUAD;
  if (rlo < rmid1)
    rhi = rhi + 1;
  rlo = (rlo + rmid2) & X64_QUAD;
  if (rlo < rmid2)
    rhi = rhi + 1;
  if (hi)
    *hi = rhi & X64_QUAD;
  return rlo;
#endif
}

/* 64b / 64b unsigned fraction divide */
inline u64 ufdiv64(u64 dvd, u64 dvr, u32 prec, u32* sticky)
{
  u64 quo;
  u32 i;

  quo = 0;          /* clear quotient */
  for (i = 0; (i < prec) && dvd; i++)
  {                 /* divide loop */
    quo = quo << 1; /* shift quo */
    if (dvd >= dvr)
    { /* div step ok? */
      dvd = dvd - dvr;  /* subtract */
      quo = quo + 1;
    } /* quo bit = 1 */

    dvd = dvd << 1;
  }   /* shift divd */

  quo = quo << (UF_V_NM - i + 1); /* shift quo */
  if (sticky)
    *sticky = (dvd ? 1 : 0);      /* set sticky bit */
  return quo; /* return quotient */
}

/* SoftFloat estimateDiv128To64: approximate (a0:a1)/b to 64 bits; the
   result is never greater than the true quotient and within 2 of it.
   Requires b > a0 (guaranteed by fsqrt64: b has bit 63 set beyond a0). */
inline u64 udiv128to64(u64 a0, u64 a1, u64 b)
{
  u64 b0, b1, rem0, rem1, term0, term1, z;
  if (b <= a0)
    return X64_QUAD;
  b0 = b >> 32;
  z = ((b0 << 32) <= a0) ? U64(0xffffffff00000000) : (a0 / b0) << 32;
  term1 = uemul64(b, z, &term0);                  /* term0:term1 = b*z */
  rem0 = a0 - term0 - ((a1 < term1) ? 1 : 0);     /* a0:a1 - term0:term1 */
  rem1 = a1 - term1;
  while (Q_GETSIGN(rem0) != 0)
  {
    z -= U64(0x100000000);
    b1 = b << 32;
    rem1 = (rem1 + b1) & X64_QUAD;
    rem0 = (rem0 + b0 + ((rem1 < b1) ? 1 : 0)) & X64_QUAD;
  }
  rem0 = (rem0 << 32) | (rem1 >> 32);
  z |= ((b0 << 32) <= rem0) ? U64(0xffffffff) : rem0 / b0;
  return z;
}

/* Fraction square root routine - code from SoftFloat */
inline u64 fsqrt64(u64 asig, s32 exp)
{
  static const u32  sqrtOdd[] = {
    0x0004, 0x0022, 0x005D, 0x00B1, 0x011D, 0x019F, 0x0236, 0x02E0,
    0x039C, 0x0468, 0x0545, 0x0631, 0x072B, 0x0832, 0x0946, 0x0A67 };
  static const u32  sqrtEven[] = {
    0x0A2D, 0x08AF, 0x075A, 0x0629, 0x051A, 0x0429, 0x0356, 0x029E,
    0x0200, 0x0179, 0x0109, 0x00AF, 0x0068, 0x0034, 0x0012, 0x0002 };

  u64               zsig;
  u64               remh;
  u64               reml;
  u64               t;
  u32               index;
  u32               z;
  u32               a;
  u32               sticky = 0;

  /* Calculate an approximation to the square root of the 32-bit significand given
   by 'a'.  Considered as an integer, 'a' must be at least 2^31.  If bit 0 of
   'exp' (the least significant bit) is 1, the integer returned approximates
   2^31*sqrt('a'/2^31), where 'a' is considered an integer.  If bit 0 of 'exp'
   is 0, the integer returned approximates 2^31*sqrt('a'/2^30).  In either
   case, the approximation returned lies strictly within +/-2 of the exact
   value. */
  a = (u32)(asig >> 32);   /* high order frac */
  index = (a >> 27) & 0xF;  /* bits<30:27> */
  if (exp & 1)
  { /* odd exp? */
    z = 0x4000 + (a >> 17) - sqrtOdd[index];  /* initial guess */
    z = ((a / z) << 14) + (z << 15);          /* Newton iteration */
    a = a >> 1;
  }
  else
  {
    z = 0x8000 + (a >> 17) - sqrtEven[index]; /* initial guess */
    z = (a / z) + z;  /* Newton iteration */
    z = (z >= 0x20000) ? 0xFFFF8000 : (z << 15);
    if (z <= a)
      z = (a >> 1) | 0x80000000;
  }

  zsig = (((((u64)a) << 31) / ((u64)z)) + (z >> 1)) & X64_LONG;

  /* Calculate the final answer in two steps.  First, do one iteration of
   Newton's approximation.  The divide-by-2 is accomplished by clever
   positioning of the operands.  Then, check the bits just below the
   (double precision) rounding bit to see if they are close to zero
   (that is, the rounding bits are close to midpoint).  If so, make
   sure that the result^2 is <below> the input operand */
  asig = asig >> ((exp & 1) ? 3 : 2); /* leave 2b guard */
  /* Newton iteration: asig*2^64 / (zsig*2^32), per SoftFloat.
     ufdiv64 was wrong here: with zsig >= 2^31 (always, on the even-exp path)
     its divisor has bit 63 set, the shifted dividend wraps past 2^64, and the
     garbage estimate sends the correction loop below walking ~2^60 ULPs */
  zsig = udiv128to64(asig, 0, zsig << 32) + (zsig << 30);
  if ((zsig & 0x1FF) <= 5)
  { /* close to even? */
    reml = uemul64(zsig, zsig, &remh);  /* result^2 (returns lo, *hi = hi) */
    remh = (asig - remh - (reml ? 1 : 0)) & X64_QUAD; /* arg - result^2 */
    reml = NEG_Q(reml);
    while (Q_GETSIGN(remh) != 0)
    { /* if arg < result^2 */
      zsig = (zsig - 1) & X64_QUAD;     /* decr result */
      t = ((zsig << 1) & X64_QUAD) | 1; /* incr result^2 */
      reml = (reml + t) & X64_QUAD;     /* and retest */
      remh = (remh + (zsig >> 63) + ((reml < t) ? 1 : 0)) & X64_QUAD;
    }

    if ((remh | reml) != 0)
      sticky = 1;
  } /* not exact? */

  zsig = (zsig << 1) | sticky;  /* left justify result */
  return zsig;
}

#define DISP_12   (sext_u64_12(ins))
#define DISP_13   (sext_u64_13(ins))
#define DISP_16   (sext_u64_16(ins))
#define DISP_21   (sext_u64_21(ins))

#define DATA_PHYS_NT(addr, flags)                                               \
  {                                                                             \
    u64 _dpc_va = (addr);                                                       \
    if constexpr (((flags) & ~ACCESS_WRITE) == 0) {                             \
      /* Normal read/write — try data page cache. The hit must match the      \
         current mode (cm) and data ASN (asn0): a hit bypasses virt2phys, so   \
         the per-mode protection check and ASN tag are only safe to skip when  \
         both are unchanged from fill time (HRM: protection is per-mode, per   \
         address space). */                                                     \
      int _dpc_rw = (flags) & ACCESS_WRITE;                                     \
      u64 _dpc_vp = _dpc_va & ~U64(0x1FFF);                                     \
      SDataPageCache& _dpc = dpc_slot(_dpc_rw, _dpc_va);                         \
      if ((_dpc.virt_page & ~(_dpc_rw ? kDpcLLWatchTag : 0)) == _dpc_vp       \
          && _dpc.valid                                                         \
          && _dpc.cm  == state.cm                                              \
          && _dpc.asn == state.asn0) {                                          \
        phys_address = _dpc.phys_base | (_dpc_va & U64(0x1FFF));                \
      } else {                                                                  \
        if (virt2phys(_dpc_va, &phys_address, flags, NULL, ins))                \
          ES40_EXECUTE_END();                                                   \
        _dpc.phys_base = phys_address & ~U64(0x1FFF);                           \
        _dpc.host_bias = ((phys_address | U64(0x1FFF)) < dram_size)                 \
                         ? ((u64) dram_ptr + (phys_address & ~U64(0x1FFF))           \
                            - _dpc_vp) : 0;                                         \
        _dpc.virt_page = _dpc.host_bias                                         \
          ? (_dpc_rw ? write_dpc_tag(_dpc_vp, phys_address) : _dpc_vp)           \
          : ~U64(0);                                                           \
        _dpc.cm        = state.cm;                                              \
        _dpc.asn       = state.asn0;                                            \
        _dpc.valid     = _dpc.host_bias != 0;                                   \
      }                                                                         \
    } else {                                                                    \
      /* PAL privileged access (NO_CHECK, VPTE, ALT, etc) — skip cache */       \
      if (virt2phys(_dpc_va, &phys_address, flags, NULL, ins))                  \
          ES40_EXECUTE_END();                                                   \
    }                                                                           \
  }

#define ALIGN_PHYS(a)                 (phys_address &~((u64) ((a) - 1)))

#define ALPHA_BASE_PAGE_MASK          U64(0x1fff)
#define TB_INDEX_DATA                 0
#define TB_INDEX_ITB                  1

#define UNALIGN_CHECK(addr, flags, align)                                        \
  if((addr) & (align))                                                           \
  {                                                                             \
    /* HRM 6.8.2: UNALIGN is a fault regardless of translation page size.       \
     * Enter PAL before translation or any memory/device side effect.          \
     * HW_LD/HW_ST use DATA_PHYS_NT; LDQ_U/STQ_U already align their address. */ \
    m_profile.data_fault.unaligned(*this, (addr), (flags), ins, (align));        \
    ES40_EXECUTE_END();                                                         \
  }

/* LDx_L / STx_C: misalignment always traps. */
#define DATA_PHYS_TRAP(addr, flags, align)                                       \
  UNALIGN_CHECK(addr, flags, align)                                              \
  DATA_PHYS_NT(addr, flags)

/* unaligned_fixup: a misaligned plain load/store takes the byte-split path (pbc) below the
 * macro instead of trapping; the first page still translates here. */
#define DATA_PHYS(addr, flags, align)                                            \
  if(((addr) & (align)) && m_unaligned_fixup) { pbc = true; }                    \
  else { UNALIGN_CHECK(addr, flags, align) }                                     \
  DATA_PHYS_NT(addr, flags)

/**
 * Normal variant of read action
 * DATA_PHYS faults on misalignment before the read. Guest PALcode or the
 * operating system decides whether to emulate the faulting instruction.
 **/
#if defined(IDB)
#define LLR last_read_loc = phys_address
#define LWR last_write_loc = phys_address
#else
#define LLR
#define LWR
#endif

#define READ_PHYS(size)                                 \
  (phys_address < dram_size                             \
    ? dram_read(dram_ptr, phys_address, size)           \
    : cSystem->ReadMem(phys_address, size, this));      \
  LLR

#define READ_VIRT(va, size, dest)                       \
  pbc = false;                                          \
  DATA_PHYS(va, ACCESS_READ, (size/8)-1);               \
  LLR;                         \
  if (pbc) {                                            \
    const u64 _ua_va = (va);                            \
    dest = 0;                                           \
    for (int ii=0; ii<(size/8); ii++) {                 \
      DATA_PHYS(_ua_va+ii, ACCESS_READ,0);             \
      dest |= (cSystem->ReadMem(phys_address, 8, this) << (ii*8));  \
    }                                                   \
  } else {                                              \
    dest = (phys_address < dram_size ? dram_read(dram_ptr, phys_address, size) : cSystem->ReadMem(phys_address, size, this));  \
  }

#define READ_VIRT_LOCK(va, size, dest)                  \
  pbc = false;                                          \
  DATA_PHYS_TRAP(va, ACCESS_READ, (size/8)-1);          \
  LLR;                         \
  dest = cSystem->cpu_read_locked(state.iProcNum, phys_address, size, this);

#define READ_VIRT_F(va, size, dest, f)                    \
  pbc = false;                                            \
  DATA_PHYS(va, ACCESS_READ, (size/8)-1);                 \
  LLR;                           \
  if (pbc) {                                              \
    u64 aa = 0;                                           \
    for (int ii=0; ii<(size/8); ii++) {                   \
      DATA_PHYS(va+ii, ACCESS_READ,0);                    \
      aa |= (cSystem->ReadMem(phys_address, 8, this) << (ii*8));  \
    }                                                     \
    dest = f(aa);                                         \
  } else {                                                \
    dest = f((phys_address < dram_size ? dram_read(dram_ptr, phys_address, size) : cSystem->ReadMem(phys_address, size, this))); \
  }                                                       \

#define READ_VIRT_LOCK_F(va, size, dest, f)               \
  pbc = false;                                            \
  DATA_PHYS_TRAP(va, ACCESS_READ, (size/8)-1);            \
  LLR;                           \
  dest = f(cSystem->cpu_read_locked(state.iProcNum, phys_address, size, this));

 /**
  * Normal variant of write action
  * DATA_PHYS faults on misalignment before the write. Guest PALcode or the
  * operating system decides whether to emulate the faulting instruction.
  **/
#define WRITE_PHYS(data, size)                         \
  if (phys_address < dram_size)                        \
  {                                                     \
    cSystem->cpu_write_ram(phys_address, size, data);  \
  }                                                     \
  else                                                 \
    cSystem->WriteMem(phys_address, size, data, this); \
  LWR

#define WRITE_VIRT(va, size, src)                           \
  pbc = false;                                              \
  DATA_PHYS(va, ACCESS_WRITE, (size/8)-1);                  \
  LWR;                                                      \
  if (pbc) {                                                \
    u64 aa = src;                                           \
    for (int ii=0; ii<(size/8); ii++) {                     \
      DATA_PHYS(va+ii, ACCESS_WRITE, 0);                    \
      if (phys_address < dram_size)                         \
      {                                                     \
        cSystem->cpu_write_ram(phys_address, 8, aa);        \
      }                                                     \
      else                                                  \
        cSystem->WriteMem(phys_address, 8, aa, this);       \
      aa >>= 8;                                             \
    }                                                       \
  } else {                                                  \
    if (phys_address < dram_size)                           \
    {                                                       \
      cSystem->cpu_write_ram(phys_address, size, src);      \
    }                                                       \
    else                                                    \
      cSystem->WriteMem(phys_address, size, src, this);     \
  }

#define WRITE_VIRT_COND(va, size, src, dest)                \
  {                                                         \
    u64 _stc_va = (va);                                     \
    u64 _stc_data = (src);                                  \
    pbc = false;                                            \
    DATA_PHYS_TRAP(_stc_va, ACCESS_WRITE, (size/8)-1);      \
    if (pbc)                                                \
    {                                                       \
      /* page-crossing STx_C fails; still consumes the lock */ \
      u64 _stc_exp; bool _stc_sa;                           \
      cSystem->cpu_take_lock(state.iProcNum, phys_address, &_stc_exp, &_stc_sa); \
      dest = 0;                                             \
    }                                                       \
    else                                                    \
    {                                                       \
      LWR;                                                  \
      dest = cSystem->cpu_stx_c(state.iProcNum, phys_address, size, _stc_data, dram_ptr, dram_size, this); \
    }                                                       \
  }

  /**
   * NO-TRAP (NT) variants of read action.
   * This is used for HW_LD, where alignment traps are
   * inhibited. We'll align the adress and read using the aligned
   * address.
   **/
#define READ_PHYS_NT(size)                                         \
  (ALIGN_PHYS((size) / 8) < dram_size                              \
    ? dram_read(dram_ptr, ALIGN_PHYS((size) / 8), size)            \
    : cSystem->ReadMem(ALIGN_PHYS((size) / 8), size, this));       \
  LLR;

   /**
    * NO-TRAP (NT) variants of write action.
    * This is used for HW_ST, where alignment traps are
    * inhibited. We'll align the adress and write using the aligned
    * address.
    **/
#if defined(IDB)
#define WRITE_PHYS_NT(data, size)                                          \
  { u64 _pa = ALIGN_PHYS((size) / 8);                                     \
    if (_pa < dram_size) { cSystem->cpu_write_ram(_pa, size, data); } \
    else cSystem->WriteMem(_pa, size, data, this); }                       \
  LWR
#else
#define WRITE_PHYS_NT(data, size)                                          \
  { u64 _pa = ALIGN_PHYS((size) / 8);                                     \
    if (_pa < dram_size) { cSystem->cpu_write_ram(_pa, size, data); } \
    else cSystem->WriteMem(_pa, size, data, this); }
#endif

#define REG_1         RREG(I_GETRA(ins))
#define REG_2         RREG(I_GETRB(ins))
#define REG_3         RREG(I_GETRC(ins))
#define FREG_1        (I_GETRA(ins))
#define FREG_2        (I_GETRB(ins))
#define FREG_3        (I_GETRC(ins))
#define RA            REG_1
#define RAV           state.r[RA]
#define RB            REG_2
#define RBV           ((ins & 0x1000) ? ((ins >> 13) & 0xff) : state.r[RB])
#define V_2           RBV
#define RC            REG_3
#define RCV           state.r[RC]

#define ACCESS_READ   0
#define ACCESS_WRITE  1
#define ACCESS_EXEC   2
#define ACCESS_MODE   3
#define NO_CHECK      4
#define VPTE          8
#define FAKE          16
#define ALT           32
#define WRCHK         64    /* HW_LD WrChk variants: also check write protection */
#define RECUR         128
#define PROBE         256
#define PROBEW        512
#define VPTE_IFETCH   1024   /* interpreter VPTE data read on behalf of an instruction fetch */
#define PAL_KSTACK    2048   /* interpreter kernel-stack access: memory faults must halt */

#define FPSTART       if(state.fpen == 0) /* flt point disabled? */ \
  {                                                                 \
    GO_PAL(AlphaPALException::FloatingPointDisabled);            /* set trap */ \
    break;                  /* and stop current instruction */      \
  }                                                                 \
  state.exc_sum = 0;

    /* Traps - corresponds to arithmetic trap summary register */
#define TRAP_SWC  U64(0x01) /* software completion */
#define TRAP_INV  U64(0x02) /* invalid operand */
#define TRAP_DZE  U64(0x04) /* divide by zero */
#define TRAP_OVF  U64(0x08) /* overflow */
#define TRAP_UNF  U64(0x10) /* underflow */
#define TRAP_INE  U64(0x20) /* inexact */
#define TRAP_IOV  U64(0x40) /* integer overflow */

#define TRAP_INT  U64(0x80) /* exception register is integer reg */

#define ARITH_TRAP(flags, reg)                                     \
  {                                                                \
    state.exc_sum |= flags; /* cause of trap */                    \
    state.exc_sum |= (reg & 0x1f) << 8; /* destination register */ \
    GO_PAL(AlphaPALException::Arithmetic);  /* trap */                        \
  }

#define ARITH_TRAP_I(flags, reg)      \
  {                                   \
    state.exc_sum = 0;                \
    ARITH_TRAP(TRAP_INT | flags, reg) \
  }

#endif

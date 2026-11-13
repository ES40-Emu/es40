/* ES40 Emulator.
 * Copyright (C) 2007-2008 by the ES40 Emulator Project
 * Copyright (C) 2023, 2025-2026 by gdwnldsKSC
 * Copyright (C) 2026 by Yufeng Gao
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
  * Contains IEEE floating point code for the Alpha CPU.
  *
  * $Id$
  *
  * X-1.8        Camiel Vanderhoeven                             02-APR-2008
  *      Fixed pointless comparison of U64 value against zero.
  *
  * X-1.7        Camiel Vanderhoeven                             14-MAR-2008
  *   1. More meaningful exceptions replace throwing (int) 1.
  *   2. U64 macro replaces X64 macro.
  *
  * X-1.6        Camiel Vanderhoeven                             29-JAN-2008
  *      Comments.
  *
  * X-1.5        Camiel Vanderhoeven                             28-JAN-2008
  *      Better floating-point exception handling.
  *
  * X-1.4        Camiel Vanderhoeven                             27-JAN-2008
  *      Comments.
  *
  * X-1.3        Camiel Vanderhoeven                             27-JAN-2008
  *      Minor floating-point improvements.
  *
  * X-1.2        Camiel Vanderhoeven                             27-JAN-2008
  *      Bugfix in ieee_sts.
  *
  * X-1.1        Camiel Vanderhoeven                             21-JAN-2008
  *      File created. Contains code based upon the SIMH Alpha pre-
  *      implementation, which is Copyright (c) 2003, Robert M Supnik.
  **/
#include "StdAfx.h"
#include "AlphaCPU.h"
#include "cpu_debug.h"

  /***************************************************************************/

  /**
   * \page IEEE IEEE floating point arithmetic
   *
   * \section IEEE_fmt IEEE Floating-point formats
   * The Alpha processor supports two different floating-point formats; S- and T-
   * floating formats.
   *
   * \subsection IEEE_S S-Floating
   * An IEEE single precision, or S-Floating, is a 32-bit floating point value.
   * In memory, it's layout is as follows:
   * \code
   *     31 30      23 22                      0
   *   +---+----------+-------------------------+
   *   | S | Exponent |         Fraction        |
   *   +---+----------+-------------------------+
   * \endcode
   *
   * In the floating point registers, the S-Floating is left-justified to occupy
   * 64 bits:
   * \code
   *     63 62          52 51                     29 28                   0
   *   +---+--------------+-------------------------+----------------------+
   *   | S |    Exponent  |         Fraction        |            0         |
   *   +---+--------------+-------------------------+----------------------+
   * \endcode
   *
   * The exponent is mapped from the 8-bit memory-format exponent to the 11-bit
   * register-format as follows:
   * \code
   *  +----------------+------------------+--------------------------------+
   *  | Memory <30:23> | Register <62:52> | Meaning                        |
   *  +----------------+------------------+--------------------------------+
   *  |      1 1111111 |    1 111 1111111 | frac <> 0: NaN (not-a-number)  |
   *  |                |                  | frac == 0: +/- Infinity        |
   *  +----------------+------------------+--------------------------------+
   *  |      1 xxxxxxx |    1 000 xxxxxxx | Finite number                  |
   *  |      0 xxxxxxx |    0 111 xxxxxxx | Finite number                  |
   *  +----------------+------------------+--------------------------------+
   *  |      0 0000000 |    0 000 0000000 | frac <> 0: Subnormal finite    |
   *  |                |                  | frac == 0: +/- Zero            |
   *  +----------------+------------------+--------------------------------+
   * \endcode
   *
   * \subsection IEEE_T T-Floating
   * An IEEE double precision, or T-Floating, is a 64-bit floating point value.
   * Both in memory, and in the flowting point registers it's layout is as
   * follows:
   * \code
   *     63 62          52 51                                             0
   *   +---+--------------+------------------------------------------------+
   *   | S |    Exponent  |                   Fraction                     |
   *   +---+--------------+------------------------------------------------+
   * \endcode
   *
   * The value (V) of a T-Floating value can be determined from the Sign (S),
   * Exponent (E) and Fraction (F) as follows:
   * \code
   *  +--------------+--------+--------+--------------------------+
   *  | E == 2047    | F <> 0 |        | V = NaN                  |
   *  +--------------+--------+--------+--------------------------+
   *  | E == 2047    | F == 0 | S == 0 | V = + Infinity           |
   *  +--------------+--------+--------+--------------------------+
   *  | E == 2047    | F == 0 | S == 1 | V = - Infinity           |
   *  +--------------+--------+--------+--------------------------+
   *  | 0 < E < 2047 |        | S == 0 | V = + (1.F) * 2^(E-1023) |
   *  +--------------+--------+--------+--------------------------+
   *  | 0 < E < 2047 |        | S == 1 | V = - (1.F) * 2^(E-1023) |
   *  +--------------+--------+--------+--------------------------+
   *  | E == 0       | F <> 0 | S == 0 | V = + (0.F) * 2^(-1022)  |
   *  +--------------+--------+--------+--------------------------+
   *  | E == 0       | F <> 0 | S == 1 | V = - (0.F) * 2^(-1022)  |
   *  +--------------+--------+--------+--------------------------+
   *  | E == 0       | F == 0 | S == 0 | V = + 0                  |
   *  +--------------+--------+--------+--------------------------+
   *  | E == 0       | F == 0 | S == 1 | V = - 0                  |
   *  +--------------+--------+--------+--------------------------+
   * \endcode
   *
   ******************************************************************************/

   /* Register format constants */
#define QNAN    U64(0x0008000000000000) /* quiet NaN flag */
#define CQNAN   U64(0xFFF8000000000000) /* canonical quiet NaN */
#define FPZERO  U64(0x0000000000000000) /* plus zero (fp) */
#define FMZERO  U64(0x8000000000000000) /* minus zero (fp) */
#define FPINF   U64(0x7FF0000000000000) /* plus infinity (fp) */
#define FMINF   U64(0xFFF0000000000000) /* minus infinity (fp) */
#define FPMAX_T U64(0x7FEFFFFFFFFFFFFF) /* plus MAX (T) */
#define FPMAX_S U64(0x47EFFFFFE0000000) /* plus MAX (S, register format) */
#define IPMAX   U64(0x7FFFFFFFFFFFFFFF) /* plus MAX (int) */
#define IMMAX   U64(0x8000000000000000) /* minus MAX (int) */
#define FPSLOW  U64(0x000000001FFFFFFF) /* S lower bits <28:0> */

/* Unpacked rounding constants */
#define UF_SRND U64(0x0000008000000000) /* S normal round */
#define UF_SINF U64(0x000000FFFFFFFFFF) /* S infinity round */
#define UF_TRND U64(0x0000000000000400) /* T normal round */
#define UF_TINF U64(0x00000000000007FF) /* T infinity round */

/***************************************************************************/

/**
 * \name IEEE_fp_load_store
 * IEEE floating point load and store functions.
 ******************************************************************************/

 //\{

static inline u64 ieee_quiet_nan_result(u64 op, u32 dp)
{
	op |= QNAN;
	if (dp == DT_S)         /* AAR 4.7.10.1 */
		op &= ~FPSLOW;
	return op;
}

 /**
  * \brief Convert an IEEE S-floating from memory format to register format.
  *
  * Adjust the exponent base, and widen the exponent and fraction fields.
  *
  * \param op	IEEE S-floating value in memory format.
  * \return		IEEE S-floating value in register format.
  **/
u64 CAlphaCPU::ieee_lds(u32 op)
{
	u32 exp = S_GETEXP(op);         /* get exponent */

	if (exp == S_NAN)
		exp = FPR_NAN;                /* inf or NaN? */
	else if (exp != 0)
		exp = exp + T_BIAS - S_BIAS;  /* zero or denorm? */
	return(((u64)(op & S_SIGN)) ? FPR_SIGN : 0) |  /* reg format */
		(((u64)exp) << FPR_V_EXP) | (((u64)(op & ~(S_SIGN | S_EXP))) << S_V_FRAC);
}

/**
 * \brief Convert an IEEE S-floating from register format to memory format.
 *
 * Adjust the exponent base, and make the exponent and fraction fields smaller.
 *
 * \param op  IEEE S-floating value in register format.
 * \return    IEEE S-floating value in memory format.
 **/
u32 CAlphaCPU::ieee_sts(u64 op)
{
	return (u32)((((op >> 62) & 0x3) << 30) | ((op >> 29) & 0x3FFFFFFF));
}

//\}

/***************************************************************************/

/**
 * \name IEEE_fp_conversion
 * IEEE floating point conversion routines
 ******************************************************************************/

 //\{

 /**
  * \brief Convert an IEEE S-floating to an IEEE T-floating.
  *
  * LDS doesn't handle denorms correctly.
  *
  * \param op  IEEE S-floating value.
  * \param ins The instruction currently being executed. Used to properly
  *            handle exceptions.
  * \return	  IEEE T-floating value.
  **/
u64 CAlphaCPU::ieee_cvtst(u64 op, u32 ins)
{
	UFP b;

	/* S-denormal conversion requires normalization and an exponent rebias
	   to T format (ARM 4.10.14). This describes the numerical conversion;
	   denormal-operand trap handling is a separate requirement. */
	if (FPR_GETEXP(op) == 0 && FPR_GETFRAC(op) != 0
	    && !(state.fpcr & FPCR_DNZ))
	{
		b.sign = FPR_GETSIGN(op);
		b.exp = 1; /* encoded exponent zero means 1 - S_BIAS (ARM 2.2.6.1) */
		b.frac = FPR_GETFRAC(op) << FPR_GUARD;
		ieee_norm(&b);                     /* renormalize denormal */
		b.exp = b.exp + T_BIAS - S_BIAS;   /* rebias S-exp to T-exp */
		return ieee_rpack(&b, ins, DT_T);
	}

	/* For zero / normal / infinity / NaN the S-format bit pattern is
	   already a valid T value. ieee_unpack raises INV on sNaN; we must
	   also silence the NaN to qNaN on the result (HRM 4.8.4 / IEEE-754
	   7.2: an invalid-op result that returns a NaN must set the QNAN bit).
	   Mirrors the sibling helper ieee_cvtts. */
	u32 ftpb = ieee_unpack(op, &b, ins);
	if (ftpb == UFT_NAN)
		return op | QNAN;
	if (ftpb == UFT_ZERO)
		return op & FPR_SIGN; /* DNZ flushed a denormal to its signed zero (AAR 4.7.7.11) */
	return op;
}

/**
 * \brief Convert an IEEE T-floating to an IEEE S-floating.
 *
 * \param op  IEEE T-floating value.
 * \param ins The instruction currently being executed. Used to properly
 *            handle exceptions and to determine the rounding mode.
 * \return	  IEEE S-floating.
 **/
u64 CAlphaCPU::ieee_cvtts(u64 op, u32 ins)
{
	UFP b;
	u32 ftpb;

	ftpb = ieee_unpack(op, &b, ins);    /* unpack */
	if (Q_FINITE(ftpb))
		return ieee_rpack(&b, ins, DT_S); /* finite? round, pack */
	if (ftpb == UFT_NAN)
		return (op | QNAN) & ~FPSLOW; /* nan? cvt to quiet */
	if (ftpb == UFT_INF)
		return op;          /* inf? unchanged */
	return 0; /* denorm? 0 */
}

//\}

/***************************************************************************/

/**
 * \name IEEE_fp_operations
 * IEEE floating point operations
 ******************************************************************************/

 //\{

 /**
  * \brief Compare 2 IEEE floating-point values.
  *
  * The following steps are taken:
  *   - Take care of NaNs
  *   - Force -0 to +0
  *   - Then normal compare will work (even on inf and denorms)
  *   .
  *
  * \param s1  First IEEE floating to be compared.
  * \param s1  Second IEEE floating to be compared.
  * \param ins The instruction currently being executed. Used to properly
  *            handle exceptions.
  * \return    0 if s1==s2, 1 if s1>s2, -1 if s1<s2.
  **/
s32 CAlphaCPU::ieee_fcmp(u64 s1, u64 s2, u32 ins, u32 trap_nan)
{
	UFP a;

	UFP b;
	u32 ftpa;
	u32 ftpb;

	ftpa = ieee_unpack(s1, &a, ins);
	ftpb = ieee_unpack(s2, &b, ins);
	if ((ftpa == UFT_NAN) || (ftpb == UFT_NAN))
	{ /* NaN involved? */
		if (trap_nan)
			ieee_trap(TRAP_INV, 1, FPCR_INVD, ins);
		return 2; // FIXED: Return 2 for unordered, not +1
	} /* force failure */

	if (ftpa == UFT_ZERO)
		a.sign = 0; /* only +0 allowed */
	if (ftpb == UFT_ZERO)
		b.sign = 0;
	if (a.sign != b.sign)
		return(a.sign ? -1 : +1); /* unequal signs? */
	if (a.exp != b.exp)
		return((a.sign ^ (a.exp < b.exp)) ? -1 : +1);
	if (a.frac != b.frac)
		return((a.sign ^ (a.frac < b.frac)) ? -1 : +1);
	return 0;
}

/**
 * \brief Convert 64-bit signed integer to IEEE floating-point value.
 *
 * \param val 64-bit signed integer to be converted.
 * \param ins The instruction currently being executed. Used to properly
 *            handle exceptions and to determine the rounding mode.
 * \param dp  DT_S for S-floating or DT_T for T-floating.
 * \return    IEEE floating.
 **/
u64 CAlphaCPU::ieee_cvtif(u64 val, u32 ins, u32 dp)
{
	UFP a;

	if (val == 0)
		return 0;   /* 0? return +0 */
	if (((s64)val) < 0)
	{             /* < 0? */
		a.sign = 1; /* set sign */
		val = NEG_Q(val);
	} /* |val| */
	else
		a.sign = 0;
	a.exp = 63 + T_BIAS;            /* set exp */
	a.frac = val;                   /* set frac */
	ieee_norm(&a);                  /* normalize */
	return ieee_rpack(&a, ins, dp); /* round and pack */
}

/**
 * \brief Convert IEEE floating-point value to 64-bit signed integer.
 *
 * Rounding code from SoftFloat.
 *
 * The Alpha architecture specifies return of the low order bits of
 * the true result, whereas the IEEE standard specifies the return
 * of the maximum plus or minus value
 *
 * \param op  IEEE floating to be converted.
 * \param ins The instruction currently being executed. Used to properly
 *            handle exceptions.
 * \return    64-bit signed integer.
 **/
u64 CAlphaCPU::ieee_cvtfi(u64 op, u32 ins)
{
	UFP a;
	u64 sticky;
	u32 rndm;
	u32 ftpa;
	u32 ovf = 0;
	s32 ubexp;

	ftpa = ieee_unpack(op, &a, ins);  /* unpack */
	if (!Q_FINITE(ftpa)) /* inf, NaN, dnorm? */
	{
		ieee_trap(TRAP_INV, 1, FPCR_INVD, ins); /* inv operation */
		return 0;
	}

	if (ftpa == UFT_ZERO)
		return 0; /* zero? */
	ubexp = a.exp - T_BIAS;   /* unbiased exp */
	if (ubexp < 0)             /* < 1? */
	{
		if (ubexp == -1)
			sticky = a.frac;      /* [.5,1)? */
		else
			sticky = 1;           /* (0,.5) */
		a.frac = 0;
	}
	else if (ubexp <= UF_V_NM) /* in range? */
	{
		/* When ubexp == UF_V_NM (63), the shift amount (UF_V_NM - ubexp) is
		   zero and (64 - 0) is 64, which is undefined behavior for `<< 64`
		   on a 64-bit value. The architectural meaning at the boundary is
		   "no bits shifted out" — the value is exact in integer form. */
		s32 sh = UF_V_NM - ubexp;
		sticky = sh ? ((a.frac << (64 - sh)) & X64_QUAD) : 0;
		a.frac = a.frac >> sh;                /* result */
	}
	else
	{
		if ((ubexp - UF_V_NM) > 63)
			a.frac = 0;         /* out of range */
		else
			a.frac = (a.frac << (ubexp - UF_V_NM)) & X64_QUAD;
		ovf = 1;              /* overflow */
		sticky = 0;           /* no rounding */
	}

	rndm = I_GETFRND(ins);  /* get round mode */
	if (rndm == I_FRND_D)
		rndm = FPCR_GETFRND(state.fpcr);  /* dynamic? use FPCR<DYN> */
	if (((rndm == I_FRND_N) && (sticky & Q_SIGN))  /* nearest? */
		|| ((rndm == I_FRND_P) && !a.sign && sticky) /* +inf and +? */
		|| ((rndm == I_FRND_M) && a.sign && sticky)) /* -inf and -? */
	{
		a.frac = (a.frac + 1) & X64_QUAD;
		if (a.frac == 0)
			ovf = 1;  /* overflow? */
		if ((rndm == I_FRND_N) && (sticky == Q_SIGN))  /* round nearest hack */
			a.frac = a.frac & ~1;
	}

	if (a.frac > (a.sign ? IMMAX : IPMAX))
		ovf = 1;  /* overflow? */

	if (ovf)
		/* Pass `ins` (not 0) so I_GETRC(ins) reports the real destination
		   register and ins & I_FTRP_S sets TRAP_SWC correctly on /S. */
		ieee_trap(TRAP_IOV, ins & I_FTRP_V, 0, ins);  /* overflow trap */
	if (ovf || sticky) /* ovflo or round? */
		ieee_trap(TRAP_INE, Q_SUI(ins), FPCR_INED, ins);
	return(a.sign ? NEG_Q(a.frac) : a.frac);
}

/**
 * \brief Add or subtract 2 IEEE floating-point values.
 *
 * The following steps are taken:
 *   - Take care of NaNs and infinites
 *   - Test for zero (fast exit)
 *   - Sticky logic for floating add
 *	    - If result normalized, sticky in right place
 *	    - If result carries out, renormalize, retain sticky
 *      .
 *   - Sticky logic for floating subtract
 *	    - If shift < guard, no sticky bits; 64b result is exact
 *	    - If shift <= 1, result may require extensive normalization,
 *	      but there are no sticky bits to worry about
 *	    - If shift >= guard, there is a sticky bit,
 *	      but normalization is at most 1 place, sticky bit is retained
 *	      for rounding purposes (but not in low order bit)
 *      .
 *   .
 *
 * \param s1  Augend or minuend.
 * \param s2  Addend or subtrahend.
 * \param ins The instruction currently being executed. Used to properly
 *            handle exceptions.
 * \param dp  DT_S for S-floating or DT_T for T-floating.
 * \param sub subtract if true, add if false.
 * \return    IEEE floating.
 **/
u64 CAlphaCPU::ieee_fadd(u64 s1, u64 s2, u32 ins, u32 dp, bool sub)
{
	UFP a;

	UFP b;

	UFP t;
	u32 ftpa;
	u32 ftpb;
	u32 sticky;
	s32 ediff;

	ftpa = ieee_unpack(s1, &a, ins);  /* unpack operands */
	ftpb = ieee_unpack(s2, &b, ins);
	if (ftpb == UFT_NAN)
		return ieee_quiet_nan_result(s2, dp); /* B = NaN? quiet B */
	if (ftpa == UFT_NAN)
		return ieee_quiet_nan_result(s1, dp); /* A = NaN? quiet A */
	if (sub)
		b.sign = b.sign ^ 1;  /* sign of B */
	if (ftpb == UFT_INF)
	{   /* B = inf? */
		if ((ftpa == UFT_INF) && (a.sign ^ b.sign))
		{ /* eff sub of inf? */
			ieee_trap(TRAP_INV, 1, FPCR_INVD, ins); /* inv op trap */
			return CQNAN;
		}           /* canonical NaN */

		return(sub ? (s2 ^ FPR_SIGN) : s2);
	}             /* return B */

	if (ftpa == UFT_INF)
		return s1;  /* A = inf? ret A */
	if (ftpa == UFT_ZERO)
	{
		if (ftpb == UFT_ZERO && (a.sign != b.sign))
		{
			/* Both operands zero, opposite effective signs (e.g. +0 + -0,
			   or +0 - +0). IEEE-754 6.3: sign is '+' except under
			   roundTowardNegative where it is '-'. */
			u32 rndm = I_GETFRND(ins);
			if (rndm == I_FRND_D) rndm = FPCR_GETFRND(state.fpcr);
			a.sign = (rndm == I_FRND_M) ? 1 : 0;
			a.exp = 0; a.frac = 0;
		}
		else
		{
			a = b;  /* s1 = 0, result is b */
		}
	}
	else if (ftpb != UFT_ZERO)
	{ /* s2 != 0? */
		if ((a.exp < b.exp)      /* s1 < s2? swap */
			|| ((a.exp == b.exp) && (a.frac < b.frac)))
		{
			t = a;
			a = b;
			b = t;
		}

		ediff = a.exp - b.exp;  /* exp diff */
		if (ediff > 63)
			b.frac = 1;           /* >63? retain sticky */
		else if (ediff)
		{ /* [1,63]? shift */
			sticky = ((b.frac << (64 - ediff)) & X64_QUAD) ? 1 : 0; /* lost bits */
			b.frac = ((b.frac >> ediff) & X64_QUAD) | sticky;
		}

		if (a.sign ^ b.sign)
		{ /* eff sub? */
			a.frac = (a.frac - b.frac) & X64_QUAD;  /* subtract fractions */
			ieee_norm(&a);
			if (a.frac == 0)
			{
				/* Exact cancellation. IEEE-754 6.3: sign is '+' except
				   under roundTowardNegative where it is '-'. */
				u32 rndm = I_GETFRND(ins);
				if (rndm == I_FRND_D) rndm = FPCR_GETFRND(state.fpcr);
				a.sign = (rndm == I_FRND_M) ? 1 : 0;
			}
		} /* normalize */
		else
		{ /* eff add */
			a.frac = (a.frac + b.frac) & X64_QUAD;  /* add frac */
			if (a.frac < b.frac)
			{ /* chk for carry */
				a.frac = UF_NM | (a.frac >> 1) |  /* shift in carry */
					(a.frac & 1); /* retain sticky */
				a.exp = a.exp + 1;
			}
		} /* skip norm */
	}   /* end else if */

	return ieee_rpack(&a, ins, dp); /* round and pack */
}

/**
 * \brief Multiply 2 IEEE floating-point values.
 *
 * The following steps are taken:
 *   - Take care of NaNs and infinites
 *   - Test for zero operands (fast exit)
 *   - 64b x 64b fraction multiply, yielding 128b result
 *   - Normalize (at most 1 bit)
 *   - Insert "sticky" bit in low order fraction, for rounding
 *   .
 *
 * Because IEEE fractions have a range of [1,2), the result can have a range
 * of [1,4).  Results in the range of [1,2) appear to be denormalized by one
 * place, when in fact they are correct.  Results in the range of [2,4) appear
 * to be in correct, when in fact they are 2X larger.  This problem is taken
 * care of in the result exponent calculation.
 *
 * \param s1  Multiplicand.
 * \param s2  Multiplier.
 * \param ins The instruction currently being executed. Used to properly
 *            handle exceptions.
 * \param dp  DT_S for S-floating or DT_T for T-floating.
 * \return    IEEE floating.
 **/
u64 CAlphaCPU::ieee_fmul(u64 s1, u64 s2, u32 ins, u32 dp)
{
	UFP a;

	UFP b;
	u32 ftpa;
	u32 ftpb;
	u64 resl;

	ftpa = ieee_unpack(s1, &a, ins);  /* unpack operands */
	ftpb = ieee_unpack(s2, &b, ins);
	if (ftpb == UFT_NAN)
		return ieee_quiet_nan_result(s2, dp); /* B = NaN? quiet B */
	if (ftpa == UFT_NAN)
		return ieee_quiet_nan_result(s1, dp); /* A = NaN? quiet A */
	a.sign = a.sign ^ b.sign; /* sign of result */
	if ((ftpa == UFT_ZERO) || (ftpb == UFT_ZERO))
	{   /* zero operand? */
		if ((ftpa == UFT_INF) || (ftpb == UFT_INF))
		{ /* 0 * inf? */
			ieee_trap(TRAP_INV, 1, FPCR_INVD, ins); /* inv op trap */
			return CQNAN;
		} /* canonical NaN */

		return(a.sign ? FMZERO : FPZERO);
	}   /* return signed 0 */

	if (ftpb == UFT_INF)
		return(a.sign ? FMINF : FPINF);         /* B = inf? */
	if (ftpa == UFT_INF)
		return(a.sign ? FMINF : FPINF);         /* A = inf? */
	a.exp = a.exp + b.exp + 1 - T_BIAS;       /* add exponents */
	resl = uemul64(a.frac, b.frac, &a.frac);  /* multiply fracs */
	ieee_norm(&a);  /* normalize */
	a.frac = a.frac | (resl ? 1 : 0); /* sticky bit */
	return ieee_rpack(&a, ins, dp);   /* round and pack */
}

/**
 * \brief Divide 2 IEEE floating-point values.
 *
 * The following steps are taken:
 *   - Take care of NaNs and infinites
 *   - Check for zero cases
 *   - Divide fractions (55b to develop a rounding bit)
 *   - Set sticky bit if remainder non-zero
 *   .
 *
 * Because IEEE fractions have a range of [1,2), the result can have a range
 * of (.5,2).  Results in the range of [1,2) are correct.  Results in the
 * range of (.5,1) need to be normalized by one place.
 *
 * \param s1  Dividend.
 * \param s2  Divisor.
 * \param ins The instruction currently being executed. Used to properly
 *            handle exceptions.
 * \param dp  DT_S for S-floating or DT_T for T-floating.
 * \return    IEEE floating.
 **/
u64 CAlphaCPU::ieee_fdiv(u64 s1, u64 s2, u32 ins, u32 dp)
{
	UFP a;

	UFP b;
	u32 ftpa;
	u32 ftpb;
	u32 sticky;

	ftpa = ieee_unpack(s1, &a, ins);
	ftpb = ieee_unpack(s2, &b, ins);
	if (ftpb == UFT_NAN)
		return ieee_quiet_nan_result(s2, dp); /* B = NaN? quiet B */
	if (ftpa == UFT_NAN)
		return ieee_quiet_nan_result(s1, dp); /* A = NaN? quiet A */
	a.sign = a.sign ^ b.sign; /* sign of result */
	if (ftpb == UFT_INF)
	{   /* B = inf? */
		if (ftpa == UFT_INF)
		{ /* inf/inf? */
			ieee_trap(TRAP_INV, 1, FPCR_INVD, ins); /* inv op trap */
			return CQNAN;
		} /* canonical NaN */

		return(a.sign ? FMZERO : FPZERO);
	}   /* !inf/inf, ret 0 */

	if (ftpa == UFT_INF)
	{   /* A = inf? */
		/* inf/0 returns +/-inf with NO exception per IEEE-754 7.3:
		   DZE is signaled only when the dividend is finite non-zero.
		   Previously raised DZE here, breaking inf-arithmetic in libm. */
		return(a.sign ? FMINF : FPINF);
	}   /* return inf */

	if (ftpb == UFT_ZERO)
	{   /* B = 0? */
		if (ftpa == UFT_ZERO)
		{ /* 0/0? */
			ieee_trap(TRAP_INV, 1, FPCR_INVD, ins); /* inv op trap */
			return CQNAN;
		} /* canonical NaN */

		ieee_trap(TRAP_DZE, 1, FPCR_DZED, ins); /* div by 0 trap */
		return(a.sign ? FMINF : FPINF);
	} /* return inf */

	if (ftpa == UFT_ZERO)
		return(a.sign ? FMZERO : FPZERO); /* A = 0? */
	a.exp = a.exp - b.exp + T_BIAS;     /* unbiased exp */
	a.frac = a.frac >> 1; /* allow 1 bit left */
	b.frac = b.frac >> 1;
	a.frac = ufdiv64(a.frac, b.frac, 55, &sticky);  /* divide */
	ieee_norm(&a);            /* normalize */
	a.frac = a.frac | sticky; /* insert sticky */
	return ieee_rpack(&a, ins, dp); /* round and pack */
}

/**
 * \brief Determine principal square root of a IEEE floating-point value.
 *
 * The following steps are taken:
 *   - Take care of NaNs, +infinite, zero
 *   - Check for negative operand
 *   - Compute result exponent
 *   - Compute sqrt of fraction
 *   .
 *
 * \param op  IEEE floating.
 * \param ins The instruction currently being executed. Used to properly
 *            handle exceptions.
 * \param dp  DT_S for S-floating or DT_T for T-floating.
 * \return    IEEE floating.
 **/
u64 CAlphaCPU::ieee_sqrt(u64 op, u32 ins, u32 dp)
{
	u32 ftpb;
	UFP b;

	ftpb = ieee_unpack(op, &b, ins);  /* unpack */
	if (ftpb == UFT_NAN)
		return ieee_quiet_nan_result(op, dp); /* NaN? */
	if ((ftpb == UFT_ZERO) || /* zero? */ ((ftpb == UFT_INF) && !b.sign))
		return op;        /* +infinity? */
	if (b.sign)
	{ /* minus? */
		ieee_trap(TRAP_INV, 1, FPCR_INVD, ins); /* signal inv op */
		return CQNAN;
	}

	const s32 in_exp = b.exp;                 /* fsqrt64 keys on INPUT exp parity */
	b.exp = ((b.exp - T_BIAS) >> 1) + T_BIAS; /* result exponent */
	b.frac = fsqrt64(b.frac, in_exp);         /* result fraction */
	return ieee_rpack(&b, ins, dp);           /* round and pack */
}

//\}

/***************************************************************************/

/**
 * \name IEEE_fp_support
 * IEEE floating point support functions
 ******************************************************************************/

 //\{

 /**
  * \brief Unpack IEEE floating-point value
  *
  * Converts a IEEE floating-point value to it's sign, exponent and fraction
  * components.
  *
  * \param op  IEEE floating.
  * \param r   Pointer to the unpacked-floating-point UFP structure
  *            where the results are to be returned.
  * \param ins The instruction currently being executed. Used to properly
  *            handle exceptions.
  * \return    Returns the type of value (UFT_ZERO, UFT_FIN, etc.).
  **/
int CAlphaCPU::ieee_unpack(u64 op, UFP* r, u32 ins)
{
	r->sign = FPR_GETSIGN(op);  /* get sign */
	r->exp = FPR_GETEXP(op);    /* get exponent */
	r->frac = FPR_GETFRAC(op);  /* get fraction */
	if (r->exp == 0)       /* exponent = 0? */
	{
		if (r->frac == 0)
			return UFT_ZERO;  /* frac = 0? then true 0 */
		if (state.fpcr & FPCR_DNZ)       /* denorms to 0? */
		{
			r->frac = 0;                  /* clear fraction */
			return UFT_ZERO;
		}

		r->exp = 1; /* encoded exponent zero means 1 - T_BIAS (ARM 2.2.6.2) */
		r->frac = r->frac << FPR_GUARD; /* guard fraction */
		ieee_norm(r); /* normalize dnorm */
		/* Denormal input operands take an *unmaskable* trap when
		   FPCR[DNZ] is clear (HRM A.11: "denormal input operands for
		   arithmetic operations produce an unmaskable denormal trap").
		   Pass fpcrdsb = 0 so neither the /S qualifier nor FPCR[INVD]
		   can suppress it -- same idiom as the unmaskable integer-
		   overflow trap in ieee_cvtfi(). */
		ieee_trap(TRAP_INV, 1, 0, ins); /* signal denormal-operand trap */
		return UFT_DENORM;
	}

	if (r->exp == FPR_NAN)   /* exponent = max? */
	{
		if (r->frac == 0)
			return UFT_INF;     /* frac = 0? then inf */
		if (!(r->frac & QNAN)) /* signaling NaN? */
			ieee_trap(TRAP_INV, 1, FPCR_INVD, ins); /* signal inv op */
		return UFT_NAN;
	}

	r->frac = (r->frac | FPR_HB) << FPR_GUARD;  /* ins hidden bit, guard */
	return UFT_FIN; /* finite */
}

/**
 * \brief Normalize IEEE floating-point value
 *
 * Normalize exponent and fraction components. Input must be zero, finite, or denorm.
 *
 * \param r   Pointer to the unpacked-floating-point UFP structure
 *            containing the value to be normalized, and where the
 *            results are to be returned.
 **/
void CAlphaCPU::ieee_norm(UFP* r)
{
	s32         i;
	static u64  normmask[5] = {
	  U64(0xc000000000000000), U64(0xf000000000000000), U64(0xff00000000000000),
	  U64(0xffff000000000000), U64(0xffffffff00000000) };
	static s32  normtab[6] = { 1, 2, 4, 8, 16, 32 };

	r->frac = r->frac & X64_QUAD;
	if (r->frac == 0)
	{ /* if fraction = 0 */
		r->exp = 0; /* result is signed 0 */
		return;
	}

	while ((r->frac & UF_NM) == 0)
	{   /* normalized? */
		for (i = 0; i < 5; i++)
		{ /* find first 1 */
			if (r->frac & normmask[i])
				break;
		}

		r->frac = r->frac << normtab[i];  /* shift frac */
		r->exp = r->exp - normtab[i];
	} /* decr exp */

	return;
}

/**
 * \brief Round and pack IEEE floating-point value
 *
 * Converts sign, exponent and fraction components to an IEEE floating
 * point value.
 *
 * Much of the treachery of the IEEE standard is buried here:
 *   - Rounding modes (chopped, +infinity, nearest, -infinity).
 *   - Inexact (set if there are any rounding bits, regardless of rounding).
 *   - Overflow (result is infinite if rounded, max if not).
 *   - Underflow (no denorms!).
 *   .
 *
 * Underflow handling is particularly complicated:
 *   - Result is always 0.
 *   - UNF and INE are always set in FPCR.
 *   - If /U is set,
 *      - If /S is clear, trap.
 *      - If /S is set, UNFD is set, but UNFZ is clear, ignore UNFD and
 *        trap, because the hardware cannot produce denormals.
 *      - If /S is set, UNFD is set, and UNFZ is set, do not trap.
 *      .
 *   - If /SUI is set, and INED is clear, trap
 *   .
 *
 * \param r   Pointer to the unpacked-floating-point UFP structure
 *            to be packed.
 * \param ins The instruction currently being executed. Used to properly
 *            handle exceptions and to determine the rounding mode.
 * \param dp  DT_S for S-floating or DT_T for T-floating.
 * \return    IEEE floating.
 **/
u64 CAlphaCPU::ieee_rpack(UFP* r, u32 ins, u32 dp)
{
	static const u64  stdrnd[2] = { UF_SRND, UF_TRND };
	static const u64  infrnd[2] = { UF_SINF, UF_TINF };
	static const s32  expmax[2] = { T_BIAS - S_BIAS + S_M_EXP - 1, T_M_EXP - 1 };
	static const s32  expmin[2] = { T_BIAS - S_BIAS, 0 };
	u64               rndadd;
	u64               rndbits;
	u64               res;
	u64               rndm;

	if (r->frac == 0)
		return(((u64)r->sign) << FPR_V_SIGN);  /* result 0? */
	rndm = I_GETFRND(ins);  /* inst round mode */
	if (rndm == I_FRND_D)
		rndm = FPCR_GETFRND(state.fpcr);  /* dynamic? use FPCR */
	rndbits = r->frac & infrnd[dp];     /* isolate round bits */
	if (rndm == I_FRND_N)
		rndadd = stdrnd[dp];  /* round to nearest? */
	else if (((rndm == I_FRND_P) && !r->sign)  /* round to inf and */
		|| ((rndm == I_FRND_M) && r->sign)) /* right sign? */
		rndadd = infrnd[dp];
	else
		rndadd = 0;
	r->frac = (r->frac + rndadd) & X64_QUAD;  /* round */
	if ((r->frac & UF_NM) == 0)
	{ /* carry out? */
		r->frac = (r->frac >> 1) | UF_NM; /* renormalize */
		r->exp = r->exp + 1;
	}

	if (rndbits) /* inexact? */
		ieee_trap(TRAP_INE, Q_SUI(ins), FPCR_INED, ins);  /* set inexact */
	if (r->exp > expmax[dp])
	{ /* ovflo? */
		ieee_trap(TRAP_OVF, 1, FPCR_OVFD, ins);           /* set overflow trap */
		ieee_trap(TRAP_INE, Q_SUI(ins), FPCR_INED, ins);  /* set inexact */
		if (rndadd)  /* did we round? */
			return(r->sign ? FMINF : FPINF);  /* return infinity */
		/* ARM 4.7.7.6: use the destination format's largest finite value. */
		return (((u64)r->sign) << FPR_V_SIGN) | (dp == DT_S ? FPMAX_S : FPMAX_T);
	} /* no, return max */

	if (r->exp <= expmin[dp])
	{ /* underflow? */
		/* UNFD (with /S) disables the underflow trap; UNDZ separately controls
		   flush-to-zero result. The two bits are independent per HRM 4.7.7.1. */
		ieee_trap(TRAP_UNF, ins & I_FTRP_U, FPCR_UNFD, ins);
		ieee_trap(TRAP_INE, Q_SUI(ins), FPCR_INED, ins);          /* set inexact */
		/* Preserve sign of zero on underflow per IEEE-754 / HRM 4.7.7.3. */
		return ((u64)r->sign) << FPR_V_SIGN;
	} /* underflow to signed zero */

	res = (((u64)r->sign) << FPR_V_SIGN) |           /* form result */
		(((u64)r->exp) << FPR_V_EXP) | ((r->frac >> FPR_GUARD) & FPR_FRAC);
	if ((rndm == I_FRND_N) && (rndbits == stdrnd[dp])) /* halfway -> round to even (S LSB=bit29, T LSB=bit0) */
		res &= ~(dp == DT_S ? U64(0x0000000020000000) : U64(1));
	if (dp == DT_S)                                    /* S_floating canonical: fraction <28:0> zero (ARM Fig 2-12) */
		res &= ~U64(0x000000001FFFFFFF);
	return res;
}

/**
 * \brief Set IEEE floating-point trap
 *
 * Called when a IEEE floating-point operation detects an exception.
 *
 * \param trap    A bitmask in which the bits are set that correspond to
 *                the exception that occurred.
 * \param instenb True if the exception is enabled in the instruction.
 * \param fpcrdsb A bitmask containing the bits that, if set in the floating-
 *                point control register (fpcr), disable the trap.
 * \param ins     The instruction currently being executed. Used to properly
 *                set some registers for the trap to be handled.
 **/
void CAlphaCPU::ieee_trap(u64 trap, u32 instenb, u64 fpcrdsb, u32 ins)
{
	const auto result = m_profile.ieee_trap.evaluate(*this, trap, instenb, fpcrdsb, ins);
	if (result.deliver)
		ARITH_TRAP(result.flags, I_GETRC(ins));
}
//\}

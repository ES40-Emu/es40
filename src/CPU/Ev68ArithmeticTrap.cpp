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

// EV68 arithmetic-trap state extracted from cpu_defs.h.
#include "StdAfx.h"
#include "AlphaCPU.h"
#include "Ev68ArithmeticTrap.h"

void Ev68ArithmeticTrap::report(CAlphaCPU& cpu, std::uint64_t flags,
                                std::uint32_t destination, AlphaArithmeticTrapKind kind)
{
  auto& state = cpu.state;
  if (kind == AlphaArithmeticTrapKind::Integer)
  {
    state.exc_sum = 0;
    flags |= U64(0x80); // Integer destination.
  }
  state.exc_sum |= flags; /* cause of trap */
  state.exc_sum |= (destination & 0x1f) << 8; /* destination register */
}

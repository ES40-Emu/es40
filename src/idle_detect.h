/* ES40 emulator -- host-side idle detection methods
 *
 * SPDX-License-Identifier: BSD-1-Clause
 *
 * A guest that spins instead of halting holds a host core at 100%. WTINT
 * covers guests that ask to sleep; this covers guests that never do, such as
 * HP OpenVMS Alpha 8.4.
 *
 * You name the guest OS in the config; nothing is auto-detected, so naming one
 * that does not match the running guest simply never triggers.
 *
 * Prior art: SIMH "SET CPU IDLE=VMS" (MIT, Copyright (c) Robert M Supnik).
 * No code copied -- only the approach of testing processor state instead of
 * looking for a particular address.
 */
#if !defined(INCLUDED_IDLE_DETECT_H)
#define INCLUDED_IDLE_DETECT_H

#include <cstring>

/* Internal rules. Several guest OSes can share one, and the user never names
 * a rule -- only their OS, so a rule can be improved without anyone's config
 * changing. Same split as SIMH's os_tab[].
 */
enum EIdleRule
{
  RULE_NONE      = 0,
  RULE_SPIN      = 1,   // spinning within a small set of PCs
  RULE_SPIN_IPL3 = 2    // ...and wholly at IPL 3
};

struct SIdleMethod
{
  const char*  name;
  int          rule;
  const char*  about;
};

/* One row per guest OS, the way SIMH's os_tab[] works. Idle host CPU measured
 * single CPU with the JIT on:
 *
 *   HP OpenVMS Alpha 8.4            101.2% -> 7.5%   vms
 *   Tru64 UNIX 5.1B-4               100.8% -> 9.4%   tru64
 *   NetBSD 10.1/alpha stock GENERIC 100.8% -> 20.7%  netbsd
 *
 * RULE_SPIN on its own cannot tell an idle loop from a tight compute loop and
 * sleeps in both: on OpenVMS it gives the same idle figure as RULE_SPIN_IPL3
 * but takes boot from 55s to 220s. tru64 and netbsd still use it because no
 * equivalent state check has been found for them.
 */
static const SIdleMethod idle_methods[] =
{
  { "off",    RULE_NONE,      "no host-side idle detection (default)" },
  { "vms",    RULE_SPIN_IPL3, "OpenVMS" },
  { "tru64",  RULE_SPIN,      "Tru64 UNIX" },
  { "netbsd", RULE_SPIN,      "NetBSD/alpha" },
  { 0, 0, 0 }
};

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

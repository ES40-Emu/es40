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

#include <cstring>

/* Internal rules. Several guest OSes can share one, and the user never names
 * a rule -- only an OS label, so a rule can be improved without anyone's
 * config changing. Same split as SIMH's os_tab[].
 */
enum EIdleRule
{
  RULE_NONE      = 0,
  RULE_SPIN      = 1,   // spinning within a small set of PCs
  RULE_SPIN_IPL3 = 2    // ...and VMS PAL PS reports IPL 3 at each qualifying sample
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
 * can repeatedly sleep during useful work. tru64 and netbsd both select it:
 * reduced host CPU use trades off against guest throughput and latency.
 * RULE_SPIN_IPL3 additionally samples the VMS PAL PS layout in p22; it does
 * not prove that an entire dispatch stayed at IPL 3 or did no useful work.
 * Other CPU/PAL combinations need an appropriate state decoder and explicit
 * eligibility before relying on this qualifier. These historical figures
 * do not establish guest clock accuracy or active-workload performance.
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

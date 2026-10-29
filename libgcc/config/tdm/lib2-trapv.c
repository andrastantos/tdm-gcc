/* 16-bit (word) arithmetic for TDM that aborts on overflow, for
   -ftrapv.
   Copyright (C) 2026 Free Software Foundation, Inc.

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation; either version 3, or (at your option) any later
version.

GCC is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

Under Section 7 of GPL version 3, you are granted additional
permissions described in the GCC Runtime Library Exception, version
3.1, as published by the Free Software Foundation.

You should have received a copy of the GNU General Public License and
a copy of the GCC Runtime Library Exception along with this program;
see the files COPYING3 and COPYING.RUNTIME respectively.  If not, see
<http://www.gnu.org/licenses/>.  */

/* Only code compiled with -ftrapv uses these.  They are kept apart
   from lib2-hi.c, which libc uses: pulled in after libc, their calls
   to abort would be left unresolved.  */

typedef int HItype __attribute__ ((mode (HI)));
typedef unsigned int UHItype __attribute__ ((mode (HI)));
typedef int SItype __attribute__ ((mode (SI)));

extern void abort (void);

HItype __addvhi3 (HItype, HItype);
HItype __subvhi3 (HItype, HItype);
HItype __mulvhi3 (HItype, HItype);
HItype __negvhi2 (HItype);
HItype __absvhi2 (HItype);

HItype
__addvhi3 (HItype a, HItype b)
{
  HItype w = (UHItype) a + (UHItype) b;

  if (b >= 0 ? w < a : w > a)
    abort ();
  return w;
}

HItype
__subvhi3 (HItype a, HItype b)
{
  HItype w = (UHItype) a - (UHItype) b;

  if (b >= 0 ? w > a : w < a)
    abort ();
  return w;
}

HItype
__mulvhi3 (HItype a, HItype b)
{
  SItype w = (SItype) a * (SItype) b;

  if (w != (HItype) w)
    abort ();
  return w;
}

HItype
__negvhi2 (HItype a)
{
  if (a == -0x7fff - 1)
    abort ();
  return -a;
}

HItype
__absvhi2 (HItype a)
{
  if (a == -0x7fff - 1)
    abort ();
  return a < 0 ? -a : a;
}

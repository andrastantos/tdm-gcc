/* 32-bit arithmetic for TDM: what libgcc2 expects to be native for its
   (default 32-bit) word.
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

/* Like lib2-hi.c, these only use addition, logic and shifts by
   constants, which GCC does inline word by word.  */

typedef int SItype __attribute__ ((mode (SI)));
typedef unsigned int USItype __attribute__ ((mode (SI)));
typedef int HItype __attribute__ ((mode (HI)));

SItype __mulsi3 (SItype, SItype);
USItype __udivsi3 (USItype, USItype);
USItype __umodsi3 (USItype, USItype);
SItype __divsi3 (SItype, SItype);
SItype __modsi3 (SItype, SItype);
SItype __ashlsi3 (SItype, HItype);
SItype __ashrsi3 (SItype, HItype);
SItype __lshrsi3 (SItype, HItype);

SItype
__mulsi3 (SItype a, SItype b)
{
  USItype x = a, y = b, r = 0;

  while (y)
    {
      if (y & 1)
	r += x;
      x <<= 1;
      y >>= 1;
    }
  return r;
}

static USItype
udivmodsi4 (USItype n, USItype d, USItype *rem)
{
  USItype q = 0, bit = 1;

  if (d == 0)
    {
      *rem = n;
      return 0;
    }
  while (d < n && !(d & 0x80000000))
    {
      d <<= 1;
      bit <<= 1;
    }
  while (bit)
    {
      if (n >= d)
	{
	  n -= d;
	  q |= bit;
	}
      d >>= 1;
      bit >>= 1;
    }
  *rem = n;
  return q;
}

USItype
__udivsi3 (USItype a, USItype b)
{
  USItype r;
  return udivmodsi4 (a, b, &r);
}

USItype
__umodsi3 (USItype a, USItype b)
{
  USItype r;
  udivmodsi4 (a, b, &r);
  return r;
}

SItype
__divsi3 (SItype a, SItype b)
{
  USItype r, q;
  q = udivmodsi4 (a < 0 ? -(USItype) a : (USItype) a,
		  b < 0 ? -(USItype) b : (USItype) b, &r);
  return (a < 0) != (b < 0) ? -q : q;
}

SItype
__modsi3 (SItype a, SItype b)
{
  USItype r;
  udivmodsi4 (a < 0 ? -(USItype) a : (USItype) a,
	      b < 0 ? -(USItype) b : (USItype) b, &r);
  return a < 0 ? -r : r;
}

SItype
__ashlsi3 (SItype a, HItype n)
{
  USItype x = a;

  n &= 31;
  if (n & 16)
    x <<= 16;
  if (n & 8)
    x <<= 8;
  if (n & 4)
    x <<= 4;
  if (n & 2)
    x <<= 2;
  if (n & 1)
    x <<= 1;
  return x;
}

SItype
__ashrsi3 (SItype a, HItype n)
{
  n &= 31;
  if (n & 16)
    a >>= 16;
  if (n & 8)
    a >>= 8;
  if (n & 4)
    a >>= 4;
  if (n & 2)
    a >>= 2;
  if (n & 1)
    a >>= 1;
  return a;
}

SItype
__lshrsi3 (SItype a, HItype n)
{
  USItype x = a;

  n &= 31;
  if (n & 16)
    x >>= 16;
  if (n & 8)
    x >>= 8;
  if (n & 4)
    x >>= 4;
  if (n & 2)
    x >>= 2;
  if (n & 1)
    x >>= 1;
  return x;
}

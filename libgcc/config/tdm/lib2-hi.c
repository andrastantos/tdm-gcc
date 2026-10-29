/* 16-bit (word) arithmetic for TDM, which has no multiply, divide or
   variable shift instructions.
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

/* These are what the operations themselves turn into, so they only
   use addition, logic and shifts by constants (which TDM does inline),
   and no loops that GCC could recognize as a bit count.  */

typedef int HItype __attribute__ ((mode (HI)));
typedef unsigned int UHItype __attribute__ ((mode (HI)));

HItype __mulhi3 (HItype, HItype);
UHItype __udivhi3 (UHItype, UHItype);
UHItype __umodhi3 (UHItype, UHItype);
HItype __divhi3 (HItype, HItype);
HItype __modhi3 (HItype, HItype);
HItype __ashlhi3 (HItype, HItype);
HItype __ashrhi3 (HItype, HItype);
HItype __lshrhi3 (HItype, HItype);
HItype __clzhi2 (UHItype);
HItype __ctzhi2 (UHItype);
HItype __ffshi2 (UHItype);
HItype __popcounthi2 (UHItype);
HItype __parityhi2 (UHItype);

HItype
__mulhi3 (HItype a, HItype b)
{
  UHItype x = a, y = b, r = 0;

  while (y)
    {
      if (y & 1)
	r += x;
      x <<= 1;
      y >>= 1;
    }
  return r;
}

/* Shift and subtract; the remainder goes to *REM.  */

static UHItype
udivmodhi4 (UHItype n, UHItype d, UHItype *rem)
{
  UHItype q = 0, bit = 1;

  if (d == 0)
    {
      *rem = n;
      return 0;
    }
  while (d < n && !(d & 0x8000))
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

UHItype
__udivhi3 (UHItype a, UHItype b)
{
  UHItype r;
  return udivmodhi4 (a, b, &r);
}

UHItype
__umodhi3 (UHItype a, UHItype b)
{
  UHItype r;
  udivmodhi4 (a, b, &r);
  return r;
}

/* Division truncates: the quotient is negative when the signs differ,
   the remainder has the sign of the dividend.  */

HItype
__divhi3 (HItype a, HItype b)
{
  UHItype r, q;
  q = udivmodhi4 (a < 0 ? -(UHItype) a : (UHItype) a,
		  b < 0 ? -(UHItype) b : (UHItype) b, &r);
  return (a < 0) != (b < 0) ? -q : q;
}

HItype
__modhi3 (HItype a, HItype b)
{
  UHItype r;
  udivmodhi4 (a < 0 ? -(UHItype) a : (UHItype) a,
	      b < 0 ? -(UHItype) b : (UHItype) b, &r);
  return a < 0 ? -r : r;
}

HItype
__ashlhi3 (HItype a, HItype n)
{
  UHItype x = a;

  n &= 15;
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

HItype
__ashrhi3 (HItype a, HItype n)
{
  n &= 15;
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

HItype
__lshrhi3 (HItype a, HItype n)
{
  UHItype x = a;

  n &= 15;
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

/* The number of leading zero bits; 16 for 0.  */

HItype
__clzhi2 (UHItype x)
{
  HItype n = 0;

  if (x == 0)
    return 16;
  if (!(x & 0xff00))
    {
      n += 8;
      x <<= 8;
    }
  if (!(x & 0xf000))
    {
      n += 4;
      x <<= 4;
    }
  if (!(x & 0xc000))
    {
      n += 2;
      x <<= 2;
    }
  if (!(x & 0x8000))
    n += 1;
  return n;
}

/* The number of trailing zero bits; 16 for 0.  */

HItype
__ctzhi2 (UHItype x)
{
  HItype n = 0;

  if (x == 0)
    return 16;
  if (!(x & 0x00ff))
    {
      n += 8;
      x >>= 8;
    }
  if (!(x & 0x000f))
    {
      n += 4;
      x >>= 4;
    }
  if (!(x & 0x0003))
    {
      n += 2;
      x >>= 2;
    }
  if (!(x & 0x0001))
    n += 1;
  return n;
}

HItype
__ffshi2 (UHItype x)
{
  return x ? __ctzhi2 (x) + 1 : 0;
}

HItype
__popcounthi2 (UHItype x)
{
  x = x - ((x >> 1) & 0x5555);
  x = (x & 0x3333) + ((x >> 2) & 0x3333);
  x = (x + (x >> 4)) & 0x0f0f;
  return (x + (x >> 8)) & 0x1f;
}

HItype
__parityhi2 (UHItype x)
{
  x ^= x >> 8;
  x ^= x >> 4;
  x ^= x >> 2;
  x ^= x >> 1;
  return x & 1;
}

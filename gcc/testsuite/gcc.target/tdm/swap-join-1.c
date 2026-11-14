/* An operation on a memory register goes through $r1, swapped in and
   back.  Two operations in a row on the same one (a sign extension is
   a mask, an XOR and a subtraction) don't swap back and in again in
   between, and a swap followed by a move between the same two is one
   move.  */
/* { dg-do run } */
/* { dg-additional-options "-save-temps" } */
/* { dg-skip-if "values stay on the stack" { *-*-* } { "-O0" } { "" } } */

#define NOIPA __attribute__ ((noipa))

NOIPA int
use (int a, int b, int c, int d)
{
  return a + b * 3 + c * 5 + d * 7;
}

/* A, B, C and D live across the calls, in memory registers.  */
NOIPA int
ext (const signed char *p, int n)
{
  int s = 0;
  for (int i = 0; i + 3 < n; i += 4)
    {
      int a = p[i], b = p[i + 1], c = p[i + 2], d = p[i + 3];
      s += use (a, b, c, d);
      s ^= use (d, c, b, a);
    }
  return s;
}

NOIPA long
carry (unsigned long a, unsigned long b, unsigned long c)
{
  return a + b - c + (a > c);
}

static const signed char v[8] = { 1, -2, 3, -128, 127, -1, 0, 5 };

int
main (void)
{
  /* use (1, -2, 3, -128) = -886, use (-128, 3, -2, 1) = -122;
     use (127, -1, 0, 5) = 159, use (5, 0, -1, 127) = 889.  */
  int s = 0;
  s += -886;
  s ^= -122;
  s += 159;
  s ^= 889;
  if (ext (v, 8) != s)
    __builtin_abort ();
  if (carry (0x12345678UL, 0x0000ffffUL, 0x00010001UL) != 0x12345678L + 0xffffL - 0x10001L + 1)
    __builtin_abort ();
  if (carry (1UL, 0x7fffffffUL, 2UL) != 0x7ffffffeL)
    __builtin_abort ();
  return 0;
}

/* { dg-final { scan-assembler-not {\tswap\t(\$r[01], [^\n]*)\n\tswap\t\1\n} } } */
/* { dg-final { scan-assembler-not {\tswap\t(\$r[01]), (\$mr[0-9]+)\n\tmov\t\1, \2\n} } } */

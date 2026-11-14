/* Of consecutive accesses to stack slots beyond [$sp+31] on the same
   line, the later ones address their slots from where the first one
   left $r0, when they are within reach, instead of loading $r0 with
   $sp+31 (and an offset) again.  */
/* { dg-do run } */
/* { dg-additional-options "-save-temps" } */
/* { dg-skip-if "values stay on the stack" { *-*-* } { "-O0" } { "" } } */

#define NOIPA __attribute__ ((noipa))

NOIPA void
fill (volatile int *p, int n)
{
  for (int i = 0; i < n; i++)
    p[i] = i * 3 + 1;
}

/* A, B and C live past the 31 words that [$sp+N] reaches.  */
NOIPA long
far (int x, long a, long b, long c)
{
  volatile int pad[40];
  fill (pad, 40);
  long r = a + b + c + pad[x] + pad[39];
  fill (pad, 40);
  return r + a * 2 + b + pad[x];
}

/* And A and B past 62 words, where $r0 takes an addition as well.  */
NOIPA long long
far64 (int x, long long a, long long b)
{
  volatile int pad[70];
  fill (pad, 70);
  long long r = (a ^ b) + pad[x];
  fill (pad, 70);
  return r + (a & b);
}

int
main (void)
{
  long r = far (5, 100000L, 200000L, -50000L);
  if (r != 100000L + 200000L - 50000L + 16 + 118 + 200000L + 200000L + 16)
    __builtin_abort ();
  long long q = far64 (9, 0x123456789abcLL, 0x0f0f0f0f0f0fLL);
  if (q != (0x123456789abcLL ^ 0x0f0f0f0f0f0fLL) + 28
	   + (0x123456789abcLL & 0x0f0f0f0f0f0fLL))
    __builtin_abort ();
  return 0;
}

/* No second load of $r0 without a restore of it in between.  */
/* { dg-final { scan-assembler-not {\tmov\t\$r0, \$sp\+31\n(\t(?!mov\t\$r0, \[\$sp\]|mov\t\$pc|swap\t\$pc)[^\n]*\n){1,6}\tmov\t\$r0, \$sp\+31\n} } } */

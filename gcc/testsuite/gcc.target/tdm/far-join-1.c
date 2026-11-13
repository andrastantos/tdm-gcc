/* Consecutive accesses to stack slots beyond [$sp+31] on the same line
   keep $r0 pointing into the frame: the restore of $r0 from [$sp] that
   ends one and the save that starts the next go.  */
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

int
main (void)
{
  long r = far (5, 100000L, 200000L, -50000L);
  if (r != 100000L + 200000L - 50000L + 16 + 118 + 200000L + 200000L + 16)
    __builtin_abort ();
  return 0;
}

/* { dg-final { scan-assembler-not {mov\t\$r0, \[\$sp\]\n\tmov\t\[\$sp\], \$r0\n} } } */

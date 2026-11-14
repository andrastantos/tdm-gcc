/* A store to a stack slot or a memory register through $r1 doesn't
   load the old value first, also when $r0 is rotated in between; a
   restore of $r1 goes when the next insn overwrites it; and $r0 isn't
   restored after an access to a far stack slot when it is dead.  */
/* { dg-do run } */
/* { dg-additional-options "-save-temps" } */
/* { dg-skip-if "values stay on the stack" { *-*-* } { "-O0" } { "" } } */

#define NOIPA __attribute__ ((noipa))

struct s { int a, b, c, d; };

NOIPA int
use (struct s *p, int x)
{
  return p->a + p->b * 2 + p->c * 3 + p->d * 4 + x;
}

/* The words of *P are copied to the stack one by one.  */
NOIPA int
copy (const struct s *p, int x, int y)
{
  struct s t = *p;
  x += y;
  return use (&t, x) + x;
}

NOIPA void
fill (struct s *p, int v)
{
  p->a = v;
  p->b = v;
  p->c = 7;
  p->d = 9;
}

/* A and B live past the 31 words that [$sp+N] reaches.  */
NOIPA long
far (int x, long a, long b)
{
  volatile int pad[40];
  pad[x] = 3;
  long r = a + b + pad[x];
  return r + a;
}

int
main (void)
{
  struct s v;
  fill (&v, 5);
  if (v.a != 5 || v.b != 5 || v.c != 7 || v.d != 9)
    __builtin_abort ();
  if (copy (&v, 100, 11) != 5 + 10 + 21 + 36 + 111 + 111)
    __builtin_abort ();
  if (far (7, 100000L, -30000L) != 170003L)
    __builtin_abort ();
  return 0;
}

/* { dg-final { scan-assembler-not {\tmov\t\$r1, [^\n]*\n(\tro[rl]\t\$r0\n)?\tmov\t\$r1, \[\$r0} } } */

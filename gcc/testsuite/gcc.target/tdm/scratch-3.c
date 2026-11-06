/* Stores through the same byte pointer in $r0, on one source line,
   rotate it once: $r0 stays rotated from one insn to the next, and
   isn't rotated back at the end, where it is dead.  */
/* { dg-do compile } */
/* { dg-skip-if "p is reloaded" { *-*-* } { "-O0" } { "" } } */
/* { dg-options "-O2" } */

struct s { int a, b, c, d; };

void
copy (struct s *p, int a, int b, int c, int d)
{
  p->a = a; p->b = b; p->c = c; p->d = d;
}

/* { dg-final { scan-assembler-times "ror\t\\\$r0" 1 } } */
/* { dg-final { scan-assembler-not "rol\t\\\$r0" } } */

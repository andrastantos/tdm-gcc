/* Where $r1 is dead after an insn, its output uses $r1 without saving
   it or swapping it back; where $r0 is dead after an access through it,
   it isn't rotated back.  */
/* { dg-do compile } */
/* { dg-options "-O2" } */

int g, h;

void
move (void)
{
  g = h;
}

void
store (int *p, int v)
{
  *p = v;
}

/* { dg-final { scan-assembler-not "swap\t\\\$r1" } } */
/* { dg-final { scan-assembler-not "\\\[\\\$sp-1\\\]" } } */
/* { dg-final { scan-assembler-not "rol\t\\\$r0" } } */

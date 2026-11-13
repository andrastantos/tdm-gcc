/* Functions that save several callee-saved registers call libgcc's
   __tdm_save_N in the prologue and jump to __tdm_restore_N in the
   epilogue, except when $r1 holds part of the return value.  One that
   saves two registers (a and b) stores them inline and restores them
   out of line.  */
/* { dg-do compile } */
/* { dg-skip-if "values stay on the stack" { *-*-* } { "-O0" } { "" } } */
/* { dg-options "-Os" } */

int id (int);

int
many (int x)
{
  int a = id (x + 1), b = id (x + 2), c = id (x + 3), d = id (x + 4);
  return id (a * 3 + b * 5 + c * 7 + d) + a + b + c + d;
}

long
many_long (int x)
{
  int a = id (x + 1), b = id (x + 2), c = id (x + 3), d = id (x + 4);
  return (long) (id (a * 3 + b * 5 + c * 7 + d) + a + b + c + d) << 3;
}

int
two (int x)
{
  int a = id (x), b = id (a + 1);
  return id (a + b) + a + b;
}

/* { dg-final { scan-assembler-times "\\.word\t__tdm_save_\[0-9\]+" 2 } } */
/* { dg-final { scan-assembler-times "\\.word\t__tdm_restore_\[0-9\]+" 2 } } */
/* { dg-final { scan-assembler-times "\\.word\t__tdm_restore_2" 1 } } */
/* { dg-final { scan-assembler-not "__tdm_save_2" } } */

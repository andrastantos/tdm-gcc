/* A long run of straight-line code with no barrier in reach: the wide
   constants go into pools in the middle of the code, with a jump over
   each, and none stays inline.  */
/* { dg-do compile } */
/* { dg-options "-O2" } */

volatile int v;

void
straight (void)
{
  int a = 0x1001;
#define S v = a; a += 0x0111;
  S S S S S S S S S S S S S S S S S S S S
  S S S S S S S S S S S S S S S S S S S S
}

/* { dg-final { scan-assembler {\tmov\t\$pc, \$pc\+[0-9]+\n\.Llit[0-9]+:\n} } } */
/* { dg-final { scan-assembler-not {\[\$pc\+2\]\n\tmov\t\$pc, \$pc\+2\n} } } */

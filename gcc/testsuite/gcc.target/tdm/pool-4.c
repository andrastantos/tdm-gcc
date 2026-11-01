/* Uses of the same wide constant in reach of one pool entry share it:
   the value is in the pool once.  */
/* { dg-do compile } */
/* { dg-options "-O0" } */

volatile int v;

void
same (void)
{
  v = 0x1234;
  v = 0x1234;
  v = 0x1234;
  v = 0x1234;
}

/* { dg-final { scan-assembler-times {\.word\t4660\n} 1 } } */
/* { dg-final { scan-assembler-times {\[\$pc\+\(\.Llit[0-9]+-\.\)\]} 8 } } */

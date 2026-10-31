/* Wide constants go into a literal pool after a barrier and are loaded
   with [$pc+(label-.)], not jumped over inline.  */
/* { dg-do compile } */
/* { dg-options "-O2" } */

extern int g[100];
extern void f (int);

int
use (int x)
{
  int s = 0;
  for (int i = 0; i < 50; i++)
    {
      s += x * 1234;
      if (s > 4000)
	f (g[i] + 0x5555);
    }
  return s;
}

/* { dg-final { scan-assembler {\[\$pc\+\(\.Llit[0-9]+-\.\)\]} } } */
/* { dg-final { scan-assembler {\.Llit[0-9]+:\n\t\.word\t1234\n} } } */
/* { dg-final { scan-assembler-not {mov\t\$pc, \$pc\+2\n} } } */

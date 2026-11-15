/* An insn that may throw ends its basic block: the pass that marks
   $r0 and $r1 as scratch must not put its clobbers after it.  */
/* { dg-do compile } */
/* { dg-options "-Os -fnon-call-exceptions" } */

_Complex long *c;
int *p, *q;

void
init (void)
{
  *c = 1.0;
}

int
copy (int n)
{
  int s = 0;
  for (int i = 0; i < n; i++)
    {
      q[i] = p[i];
      s += p[i] / n;
    }
  return s;
}

/* Far branches load their target from a literal pool: a conditional
   branch back over a long loop body, and jumps over long blocks.  Return
   0, or the number of the first failed check.  */
/* { dg-do run } */
/* { dg-options "-O2 -save-temps" } */

#define NOIPA __attribute__ ((noipa))

static int fail;
static void check (int n, int got, int want) { if (got != want && !fail) fail = n; }

volatile int v;

/* A loop whose body is too long for a short branch back.  */
NOIPA int
loop (int n)
{
  int s = 0;
  for (int i = 0; i < n; i++)
    {
#define B v = i; s += v ^ 0x0505; s -= v & 0x1111;
      B B B B B B B B B B
    }
  return s;
}

/* Far forward branches around long blocks.  */
NOIPA int
pick (int x)
{
  int r;
  if (x == 1)
    {
#define C v = 0x0123; v = 0x0234; v = 0x0345;
      C C C C C C
      r = 10;
    }
  else if (x == 2)
    {
      C C C C C C
      r = 20;
    }
  else
    r = 30;
  return r + v;
}

int
main (void)
{
  int want = 0;
  for (int i = 0; i < 7; i++)
    for (int k = 0; k < 10; k++)
      want += (i ^ 0x0505) - (i & 0x1111);
  check (1, loop (7), want);
  check (2, pick (1), 10 + 0x0345);
  check (3, pick (2), 20 + 0x0345);
  v = 5;
  check (4, pick (3), 35);
  return fail;
}

/* { dg-final { scan-assembler {\tif_[a-z]+\t[^\n]*\n\tmov\t\$pc, \[\$pc\+\(\.Llit[0-9]+-\.\)\]} } } */
/* { dg-final { scan-assembler-not {\tmov\t\$pc, \$pc\+3\n\tmov\t\$pc, \[\$pc\+1\]} } } */

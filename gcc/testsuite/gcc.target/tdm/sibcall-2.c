/* Calls that can't be sibling calls, although TDM accepts any call as
   one: they must still work as ordinary calls.  Return 0, or the number
   of the first failed check.  */
/* { dg-do run } */
/* { dg-options "-O2" } */

#define NOIPA __attribute__ ((noipa))

static int fail;
static void check (int n, int got, int want) { if (got != want && !fail) fail = n; }

volatile int v;

NOIPA int sub3 (int a, int b, int c) { return a - 2 * b + 3 * c; }
NOIPA int twice (int x) { return 2 * x + v; }

/* The callee needs more stack arguments than the caller received.  */
NOIPA int more_args (int x)
{
  int a = twice (x), b = twice (a), c = twice (b);
  return sub3 (a, b, c);
}

/* After a dynamic allocation, the only use of a frame pointer.  */
NOIPA int sum (const int *p, int n)
{
  int s = 0;
  for (int i = 0; i < n; i++)
    s += p[i];
  return s;
}
NOIPA int dynamic (int n)
{
  int a[n];
  for (int i = 0; i < n; i++)
    a[i] = i + v;
  v = sum (a, n);
  return twice (v);
}

/* To a nested function, whose static chain points into the caller's
   frame.  */
NOIPA int outer (int x)
{
  int k = x * 3;
  NOIPA int inner (int y) { return y + k; }
  k += v;
  return inner (x);
}

int
main (void)
{
  v = 0;
  check (1, more_args (1), 2 - 8 + 24);
  check (2, dynamic (5), 30);
  v = 0;
  check (3, outer (4), 16);
  return fail;
}

/* Scratch registers: $r1 or $r0 live after an insn whose output uses
   them must keep their values.  Return 0, or the number of the first
   failed check.  */
/* { dg-do run } */
/* { dg-options "-O2" } */

#define NOIPA __attribute__ ((noipa))

static int fail;
static void check (int n, long got, long want) { if (got != want && !fail) fail = n; }

volatile int a, b;
int arr[4];

/* A 32-bit value lives in $r0 and $r1 across moves between memory
   words.  */
NOIPA long
keep_long (long v)
{
  long t = v * 3 + 1;
  a = b;
  arr[2] = b;
  return t;
}

/* A pointer in $r0 is used again after a store through it.  */
NOIPA int
reuse_ptr (int *p, int v)
{
  p[0] = v;
  p[1] = a;
  p[2] = p[0] + p[1];
  return p[2];
}

/* A byte pointer: octets through $r0, both halves.  */
NOIPA int
octets (char *p, int n)
{
  int s = 0;
  for (int i = 0; i < n; i++)
    {
      p[i] = (char) (i + b);
      s += p[i];
    }
  return s;
}

int
main (void)
{
  char buf[7];
  b = 5;
  check (1, keep_long (100000L), 300001L);
  check (2, a, 5);
  check (3, arr[2], 5);
  check (4, reuse_ptr (arr, 7), 12);
  check (5, arr[0] + arr[1], 12);
  check (6, octets (buf, 7), 5 + 6 + 7 + 8 + 9 + 10 + 11);
  check (7, buf[6], 11);
  return fail;
}

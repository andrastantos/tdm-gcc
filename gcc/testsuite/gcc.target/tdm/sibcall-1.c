/* Sibling calls: the caller's epilogue leaves the return address in
   [$sp] and the callee's stack arguments over the caller's, then jumps
   to the callee.  See sibcall-2.c for calls that can't be sibling
   calls.  Return 0, or the number of the first failed check.  */
/* { dg-do run } */
/* { dg-options "-O2" } */

#define NOIPA __attribute__ ((noipa))

static int fail;
static void check (int n, long got, long want) { if (got != want && !fail) fail = n; }

volatile int v;

/* Mutual recursion far deeper than the stack: a call takes two words
   (the return address and the callee's free word), and the default
   layout has fewer than 50000 for the stack.  */
NOIPA int odd (unsigned int n);
NOIPA int even (unsigned int n) { return n == 0 ? 1 : odd (n - 1); }
NOIPA int odd (unsigned int n) { return n == 0 ? 0 : even (n - 1); }

/* Stack arguments, passed over the caller's own.  */
NOIPA int sub3 (int a, int b, int c) { return a - 2 * b + 3 * c; }
NOIPA int rot3 (int a, int b, int c) { return sub3 (c, a, b); }

/* A 32-bit argument and result: on the stack, and in $r0:$r1.  */
NOIPA long addl (long a, long b) { return a + b + v; }
NOIPA long swapl (long a, long b) { return addl (b << 1, a); }

/* An indirect call, through $r1.  */
typedef int fn (int);
NOIPA int twice (int x) { return 2 * x + v; }
static fn *volatile table[] = { twice };
NOIPA int indirect (int x) { return table[0] (x + 1); }

/* Registers saved by the caller, restored before the jump, and stack
   arguments in place of the caller's.  */
NOIPA int saved (int x, int y, int z)
{
  int a = twice (x), b = twice (a), c = twice (b);
  return sub3 (a + y, b + z, c);
}

/* A large frame, with stack slots out of reach of [$sp+N].  */
NOIPA int big (int n)
{
  volatile int a[100];
  for (int i = 0; i < 100; i++)
    a[i] = i * n;
  return twice (a[99] + a[n]);
}

int
main (void)
{
  v = 0;
  check (1, even (25000), 1);
  check (2, odd (25001), 1);
  check (3, rot3 (1, 2, 3), 3 - 2 + 6);
  check (4, swapl (0x12345L, 0x10000L), 0x20000L + 0x12345L);
  check (5, indirect (20), 42);
  check (6, saved (1, 10, 20), 12 - 2 * 24 + 3 * 8);
  check (7, big (3), 2 * (99 * 3 + 9));
  return fail;
}

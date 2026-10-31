/* Wide constants in literal pools, at -O0 and with optimization: uses
   before and after their pool, uses too far from any pool (they stay
   inline), symbol addresses, and an asm statement between a use and the
   end of the function.  Return 0, or the number of the first failed
   check.  */
/* { dg-do run } */
/* { dg-options "-O2" } */

#define NOIPA __attribute__ ((noipa))

static int fail;
static void check (int n, int got, int want) { if (got != want && !fail) fail = n; }

int arr[4] = { 10, 20, 30, 40 };
volatile int v;

/* Uses on both sides of the pool after the early return.  */
NOIPA int
both (int x)
{
  if (x == 0)
    return 0x1234;
  if (x == 1)
    return 0x2345;
  return x + 0x3456;
}

/* A long straight run: the constants at its start are out of reach of
   the pool at the end.  */
NOIPA int
straight (void)
{
  int a = 0x1001;
#define S v = a; a += 0x0111;
  S S S S S S S S S S S S S S S S S S S S
  S S S S S S S S S S S S S S S S S S S S
  return a;
}

/* Symbol addresses (byte pointers) and a function address.  */
NOIPA int *
elem (int i)
{
  return i ? &arr[3] : &arr[1];
}

NOIPA int (*fnptr (void)) (int)
{
  return both;
}

/* An asm statement: no pool across it.  */
NOIPA int
with_asm (int x)
{
  int y = x + 0x1234;
  __asm__ volatile ("" : "+r" (y));
  return y + 0x2345;
}

int
main (void)
{
  check (1, both (0), 0x1234);
  check (2, both (1), 0x2345);
  check (3, both (2), 0x3458);
  check (4, straight (), 0x1001 + 40 * 0x0111);
  check (5, *elem (0), 20);
  check (6, *elem (1), 40);
  check (7, fnptr () (5), 5 + 0x3456);
  check (8, with_asm (1), 1 + 0x1234 + 0x2345);
  return fail;
}

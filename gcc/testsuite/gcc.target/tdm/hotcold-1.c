/* Branches between the hot and the cold part of a function are far: the
   cold part goes to .text.unlikely, which the linker puts before the
   rest of the code.  Return 0, or the number of the first failed
   check.  */
/* { dg-do run } */
/* { dg-options "-O2 -freorder-blocks-and-partition -save-temps" } */

#define NOIPA __attribute__ ((noipa))

static int fail;
static void check (int n, int got, int want) { if (got != want && !fail) fail = n; }

volatile int v, w;

/* Puts distance between .text.unlikely and the hot part of foo.  */
NOIPA void
filler (void)
{
#define A w = w * 3 + 1;
#define B A A A A A A A A A A
  B B B B B B B B B B B B B B B B B B B B
}

void bar (void) __attribute__ ((noipa, cold));
void bar (void) { v += 100; }

/* The call to bar and what follows it go to foo.cold: a conditional
   branch goes there, and a jump comes back.  */
NOIPA int
foo (int x, int n)
{
  int s = 0;
  for (int i = 0; i < n; i++)
    {
      if (__builtin_expect (x == i, 0))
	{
	  bar ();
	  s -= v;
	  continue;
	}
      s += v;
    }
  return s;
}

int
main (void)
{
  v = 1;
  filler ();
  check (1, foo (-1, 5), 5);
  check (2, foo (2, 5), 1 + 1 - 101 + 101 + 101);
  return fail;
}

/* { dg-final { scan-assembler {foo\.cold:} } } */

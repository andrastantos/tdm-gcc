/* Out of line saves and restores of the callee-saved registers: with
   the block after the free word (no outgoing arguments) and at the top
   of the frame, a value returned in $r0 and $r1, a frame pointer and a
   sibling call.  Each function keeps values in callee-saved registers
   across calls; probe checks that $mr19 to $mr31 survive them.  Return
   0, or the number of the first failed check.  */
/* { dg-do run } */

#define NOIPA __attribute__ ((noipa))

static int fail;
static void check (int n, int got, int want) { if (got != want && !fail) fail = n; }

NOIPA int id (int x) { return x; }
NOIPA int id3 (int x, int y, int z) { return x + y - z; }
NOIPA void sink (volatile void *p) { (void) p; }

#define BODY(CALL)						\
  int a = CALL (x + 1), b = CALL (x + 2), c = CALL (x + 3);	\
  int d = CALL (x + 4), e = CALL (x + 5), f = CALL (x + 6);	\
  int g = CALL (a ^ b), h = CALL (c ^ d);			\
  int s = CALL (a * 3 + b * 5 + c * 7 + d * 11 + e * 13	\
		+ f * 17 + g * 19 + h * 23);

#define ID1(X) id (X)
#define ID3(X) id3 (X, x, x)

/* No outgoing arguments: the block follows the free word.  */
NOIPA int
bottom (int x)
{
  BODY (ID1)
  return s + a + b + c + d + e + f + g + h;
}

/* Outgoing arguments: the block is at the top of the frame.  */
NOIPA int
top (int x)
{
  BODY (ID3)
  return s + a + b + c + d + e + f + g + h;
}

/* A long comes back in $r0 and $r1: restored inline.  */
NOIPA long
twoword (int x)
{
  BODY (ID1)
  return (long) (s + a + b + c + d + e + f + g + h) << 3;
}

/* A frame pointer, and locals between the saved registers.  */
NOIPA int
fp (int x, int n)
{
  volatile int before = 100;
  int v[n];
  BODY (ID1)
  for (int i = 0; i < n; i++)
    v[i] = a + i;
  sink (v);
  return before + v[n - 1] + s + a + b + c + d + e + f + g + h;
}

/* A sibling call at the end.  */
NOIPA int
tail (int x)
{
  BODY (ID1)
  return id (s + a + b + c + d + e + f + g + h);
}

static int want (int x)
{
  int a = x + 1, b = x + 2, c = x + 3, d = x + 4, e = x + 5, f = x + 6;
  int g = a ^ b, h = c ^ d;
  int s = (a * 3 + b * 5 + c * 7 + d * 11 + e * 13 + f * 17 + g * 19
	   + h * 23);
  return s + a + b + c + d + e + f + g + h;
}

static int arg;
static int result;
NOIPA void run_bottom (void) { result = bottom (arg); }
NOIPA void run_top (void) { result = top (arg); }
NOIPA void run_twoword (void) { result = (int) (twoword (arg) >> 3); }
NOIPA void run_fp (void) { result = fp (arg, 5) - 100 - (arg + 1 + 4); }
NOIPA void run_tail (void) { result = tail (arg); }

/* Call FN with sentinels in $mr19 to $mr31; return how they changed
   (0 if they didn't).  */
NOIPA int
probe (void (*fn) (void))
{
  int r;

  fn ();			/* Give probe a frame.  */
  __asm__ volatile (
#define SET(N) "mov\t$r1, " #N "\n\tmov\t$mr" #N ", $r1\n\t"
    SET (19) SET (20) SET (21) SET (22) SET (23) SET (24) SET (25)
    SET (26) SET (27) SET (28) SET (29) SET (30) SET (31)
    "mov\t$r1, %1\n\t"
    "mov\t[$sp], $r1\n\t"
    "swap\t$pc, [$sp]\n\t"
    "mov\t$r1, 0\n\t"
#define TEST(N) "mov\t$r0, $mr" #N "\n\txor\t$r0, " #N "\n\t"	\
		"mov\t[$sp-1], $r0\n\tor\t$r1, [$sp-1]\n\t"
    TEST (19) TEST (20) TEST (21) TEST (22) TEST (23) TEST (24) TEST (25)
    TEST (26) TEST (27) TEST (28) TEST (29) TEST (30) TEST (31)
    : "=&t" (r)
    : "j" (fn)
    : "$r0", "$mr7", "$mr8", "$mr9", "$mr10", "$mr11", "$mr12", "$mr13",
      "$mr14", "$mr15", "$mr16", "$mr17", "$mr18", "$mr19", "$mr20",
      "$mr21", "$mr22", "$mr23", "$mr24", "$mr25", "$mr26", "$mr27",
      "$mr28", "$mr29", "$mr30", "$mr31", "memory");
  return r;
}

int
main (void)
{
  static void (*const fns[]) (void)
    = { run_bottom, run_top, run_twoword, run_fp, run_tail };

  for (int i = 0; i < 5; i++)
    for (arg = 0; arg < 40; arg += 13)
      {
	check (10 * i + 1, probe (fns[i]), 0);
	check (10 * i + 2, result, want (arg));
      }
  return fail;
}

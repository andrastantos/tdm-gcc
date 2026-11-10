/* 32-bit addition, subtraction and negation, word by word with the
   carry from a predicate instruction: carries and borrows at the
   edges, constants of every size, memory operands and operands that
   overlap.  The reference adds the halves as 16-bit numbers.  Return
   0, or the number of the first failed check.  */
/* { dg-do run } */

#define NOIPA __attribute__ ((noipa))

typedef unsigned long u32;
typedef unsigned short u16;

static int fail;
static void check (int n, u32 got, u32 want) { if (got != want && !fail) fail = n; }

static u32
ref_add (u32 a, u32 b)
{
  u16 alo = a, ahi = a >> 16, blo = b, bhi = b >> 16;
  u16 lo = alo + blo;
  u16 hi = ahi + bhi + (lo < alo);
  return (u32) hi << 16 | lo;
}

static u32
ref_neg (u32 a)
{
  u16 lo = -(u16) a, hi = ~(u16) (a >> 16) + ((u16) a == 0);
  return (u32) hi << 16 | lo;
}

static u32 ref_sub (u32 a, u32 b) { return ref_add (a, ref_neg (b)); }

NOIPA u32 add (u32 a, u32 b) { return a + b; }
NOIPA u32 sub (u32 a, u32 b) { return a - b; }
NOIPA u32 neg (u32 a) { return -a; }
NOIPA long sadd (long a, long b) { return a + b; }
NOIPA u32 add_5 (u32 a) { return a + 5; }
NOIPA u32 add_m3 (u32 a) { return a - 3; }
NOIPA u32 add_1000 (u32 a) { return a + 1000; }
NOIPA u32 add_10000 (u32 a) { return a + 0x10000; }
NOIPA u32 add_12345678 (u32 a) { return a + 0x12345678; }
NOIPA u32 sub_7 (u32 a) { return a - 7; }
NOIPA u32 sub_from_7 (u32 a) { return 7 - a; }
NOIPA u32 rsub_big (u32 a) { return 0x89abcdef - a; }
NOIPA u32 twice (u32 a) { return a + a; }
NOIPA u32 zero (u32 a) { return a - a; }

u32 g, h;
NOIPA void g_add_h (void) { g += h; }
NOIPA void g_sub_h (void) { g -= h; }
NOIPA void g_neg (void) { g = -g; }
NOIPA void g_add_g (void) { g += g; }
NOIPA void ptr_add (u32 *p, u32 *q) { *p += *q; }
NOIPA void ptr_sub (u32 *p, u32 *q) { p[1] = p[0] - *q; }

/* Values live across calls, in callee-saved memory registers.  */
NOIPA u32 id (u32 x) { return x; }
NOIPA u32
chain (u32 a, u32 b)
{
  u32 x = id (a) + id (b);
  u32 y = id (x) - a;
  return y + x - id (b);
}

static const u32 vals[] = {
  0, 1, 2, 0x7fff, 0x8000, 0xffff, 0x10000, 0x10001, 0x1ffff,
  0x7fffffff, 0x80000000, 0x80000001, 0xfffeffff, 0xffff0000,
  0xffff0001, 0xfffffffe, 0xffffffff, 0x12345678, 0x9abcdef0, 0xdead0001
};
#define N (sizeof vals / sizeof vals[0])

int
main (void)
{
  for (unsigned int i = 0; i < N; i++)
    {
      u32 a = vals[i];
      for (unsigned int j = 0; j < N; j++)
	{
	  u32 b = vals[j];
	  check (1, add (a, b), ref_add (a, b));
	  check (2, sub (a, b), ref_sub (a, b));
	  check (3, (u32) sadd (a, b), ref_add (a, b));
	  g = a; h = b; g_add_h (); check (4, g, ref_add (a, b));
	  g = a; h = b; g_sub_h (); check (5, g, ref_sub (a, b));
	  u32 m[2] = { a, 0 }, n = b;
	  ptr_add (&m[0], &n); check (6, m[0], ref_add (a, b));
	  m[0] = a; ptr_sub (m, &n); check (7, m[1], ref_sub (a, b));
	  check (8, chain (a, b), ref_add (a, b));
	}
      check (10, neg (a), ref_neg (a));
      g = a; g_neg (); check (11, g, ref_neg (a));
      g = a; g_add_g (); check (12, g, ref_add (a, a));
      check (13, add_5 (a), ref_add (a, 5));
      check (14, add_m3 (a), ref_add (a, -3ul));
      check (15, add_1000 (a), ref_add (a, 1000));
      check (16, add_10000 (a), ref_add (a, 0x10000));
      check (17, add_12345678 (a), ref_add (a, 0x12345678));
      check (18, sub_7 (a), ref_sub (a, 7));
      check (19, sub_from_7 (a), ref_sub (7, a));
      check (20, rsub_big (a), ref_sub (0x89abcdef, a));
      check (21, twice (a), ref_add (a, a));
      check (22, zero (a), 0);
    }
  return fail;
}

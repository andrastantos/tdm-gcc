/* 32-bit shifts by every constant amount, inline or through libgcc's
   __ashlsi3, __lshrsi3 and __ashrsi3, against shifting the 16-bit halves
   one bit at a time.  Return 0, or the number of the first failed
   check (32 * kind + amount).  The reference and the checks of one
   value are functions of their own: inlined and unrolled at -O3 they
   would make the program bigger than the room below the simulator
   test stack.  */
/* { dg-do run } */

#define NOIPA __attribute__ ((noipa))

typedef unsigned long u32;
typedef long s32;
typedef unsigned short u16;

static int fail;
static void check (int n, u32 got, u32 want) { if (got != want && !fail) fail = n; }

/* KIND 0: left, 1: logical right, 2: arithmetic right.  */
NOIPA static u32
ref (int kind, u32 x, int n)
{
  u16 lo = x, hi = x >> 16;
  for (int i = 0; i < n; i++)
    if (kind == 0)
      {
	hi = (u16) (hi << 1) | (lo >> 15);
	lo = (u16) (lo << 1);
      }
    else
      {
	lo = (lo >> 1) | (u16) (hi << 15);
	hi = (hi >> 1) | (kind == 2 ? hi & 0x8000 : 0);
      }
  return (u32) hi << 16 | lo;
}

#define SH(N)								\
  NOIPA u32 shl##N (u32 a) { return a << N; }				\
  NOIPA u32 shr##N (u32 a) { return a >> N; }				\
  NOIPA s32 sar##N (s32 a) { return a >> N; }
#define T(N)								\
  check (N, shl##N (x), ref (0, x, N));					\
  check (32 + N, shr##N (x), ref (1, x, N));				\
  check (64 + N, (u32) sar##N ((s32) x), ref (2, x, N));

#define ALL(M) M (1) M (2) M (3) M (4) M (5) M (6) M (7) M (8) M (9)	\
  M (10) M (11) M (12) M (13) M (14) M (15) M (16) M (17) M (18) M (19)	\
  M (20) M (21) M (22) M (23) M (24) M (25) M (26) M (27) M (28) M (29)	\
  M (30) M (31)

ALL (SH)

/* A shift whose value stays live across it, in memory registers.  */
NOIPA u32 id (u32 x) { return x; }
NOIPA u32 keep (u32 a) { u32 b = id (a); return (b << 5) + (b >> 9) + b; }

NOIPA static void
test (u32 x)
{
  ALL (T)
  check (100, keep (x), ref (0, x, 5) + ref (1, x, 9) + x);
}

static const u32 vals[] = {
  0, 1, 0x8000, 0xffff, 0x10000, 0x12345678, 0x80000000, 0x9abcdef0,
  0xffffffff, 0x7fffffff, 0xdead0001
};

int
main (void)
{
  for (unsigned int i = 0; i < sizeof vals / sizeof vals[0]; i++)
    test (vals[i]);
  return fail;
}

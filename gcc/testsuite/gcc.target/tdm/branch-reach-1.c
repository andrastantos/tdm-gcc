/* Short jumps and branches reach from 32 words before the jump
   instruction to 31 after it.  Forward and backward branches over 1 to
   24 stores sweep across both limits: every one must assemble as
   either a short or a far branch, and run.  */
/* { dg-do run } */

#define NOIPA __attribute__ ((noipa))

volatile int v;
int w[4];

#define P1(n) w[(n) & 3] = (n);
#define P2(n) P1 (n) P1 (n + 1)
#define P4(n) P2 (n) P2 (n + 2)
#define P8(n) P4 (n) P4 (n + 4)
#define P16(n) P8 (n) P8 (n + 8)

/* A forward conditional branch over PAD, and a jump over the else
   part.  */
#define FWD(name, pad)				\
  NOIPA int					\
  name (int x, int y)				\
  {						\
    if (x != y)					\
      {						\
	pad					\
	v = 1;					\
      }						\
    else					\
      {						\
	pad					\
	v = 2;					\
      }						\
    return v + x;				\
  }

/* A backward conditional branch over PAD.  */
#define BWD(name, pad)				\
  NOIPA int					\
  name (int x, int y)				\
  {						\
    do						\
      {						\
	pad					\
	v = x;					\
	x++;					\
      }						\
    while (x != y);				\
    return v;					\
  }

#define BOTH(n, pad) FWD (f##n, pad) BWD (b##n, pad)

BOTH (1, P1 (0))
BOTH (2, P2 (0))
BOTH (3, P2 (0) P1 (2))
BOTH (4, P4 (0))
BOTH (5, P4 (0) P1 (4))
BOTH (6, P4 (0) P2 (4))
BOTH (7, P4 (0) P2 (4) P1 (6))
BOTH (8, P8 (0))
BOTH (9, P8 (0) P1 (8))
BOTH (10, P8 (0) P2 (8))
BOTH (11, P8 (0) P2 (8) P1 (10))
BOTH (12, P8 (0) P4 (8))
BOTH (13, P8 (0) P4 (8) P1 (12))
BOTH (14, P8 (0) P4 (8) P2 (12))
BOTH (15, P8 (0) P4 (8) P2 (12) P1 (14))
BOTH (16, P16 (0))
BOTH (17, P16 (0) P1 (16))
BOTH (18, P16 (0) P2 (16))
BOTH (19, P16 (0) P2 (16) P1 (18))
BOTH (20, P16 (0) P4 (16))
BOTH (21, P16 (0) P4 (16) P1 (20))
BOTH (22, P16 (0) P4 (16) P2 (20))
BOTH (23, P16 (0) P4 (16) P2 (20) P1 (22))
BOTH (24, P16 (0) P8 (16))

typedef int (*fn) (int, int);

fn fwd[] = { f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14,
	     f15, f16, f17, f18, f19, f20, f21, f22, f23, f24 };
fn bwd[] = { b1, b2, b3, b4, b5, b6, b7, b8, b9, b10, b11, b12, b13, b14,
	     b15, b16, b17, b18, b19, b20, b21, b22, b23, b24 };

int
main (void)
{
  for (int i = 0; i < 24; i++)
    {
      if (fwd[i] (3, 4) != 4 || fwd[i] (5, 5) != 7)
	__builtin_abort ();
      if (bwd[i] (2, 5) != 4 || bwd[i] (7, 8) != 7)
	__builtin_abort ();
    }
  return 0;
}

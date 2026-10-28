/* Stack slots beyond [$sp+31], and copies between memory addressed
   through two pointers.  Return 0, or the number of the first failed
   check.  */
/* { dg-do run } */
/* Keep memcpy a loop.  */
/* { dg-additional-options "-fno-tree-loop-distribute-patterns" } */

#define NOIPA __attribute__ ((noipa))

static int fail;
static void check (int n, int got, int want) { if (got != want && !fail) fail = n; }

struct s { int a, b, c; };
struct big { int w[6]; };

NOIPA void cp (struct s *d, struct s *p) { d->b = p->c; }
NOIPA void ad (int *d, int *p) { d[1] += p[2]; }
NOIPA void xo (int *d, int *p) { d[0] ^= p[1]; }

NOIPA struct big
mkbig (int x)
{
  struct big r;
  for (int i = 0; i < 6; i++)
    r.w[i] = x + i;
  return r;
}

NOIPA void sink (volatile int *p) { (void) p; }

/* -Os copies the structs with memcpy; there is no libc.  */
NOIPA void *
memcpy (void *d, const void *s, __SIZE_TYPE__ n)
{
  char *dc = d;
  const char *sc = s;
  while (n--)
    *dc++ = *sc++;
  return d;
}

/* Locals past the reach of [$sp+31], used in every kind of operand.  */
NOIPA int
far (int x, int y, char c)
{
  volatile int pad[40];
  volatile int v = x;
  volatile int u = y;
  volatile unsigned char ch[3];
  volatile signed char sc = -5;
  int r;

  sink (pad);
  ch[0] = c;
  ch[1] = c + 1;
  ch[2] = c + 2;
  v += u;			/* far += far */
  check (1, v, x + y);
  u -= 3;			/* far -= imm */
  check (2, u, y - 3);
  v = -v;			/* neg far */
  check (3, v, -(x + y));
  v = ~v;
  check (4, v, x + y - 1);
  v <<= 2;			/* shift far */
  check (5, v, (x + y - 1) << 2);
  u = (unsigned) u >> 1;
  check (6, u, (unsigned) (y - 3) >> 1);
  check (7, ch[1], (unsigned char) (c + 1));
  check (8, ch[2], (unsigned char) (c + 2));
  check (9, sc, -5);
  r = v + u + ch[0];
  return r;
}

/* A far slot combined with a value in $r0 (the first argument).  */
NOIPA int
far_r0 (int x)
{
  volatile int pad[40];
  volatile int v = 7;

  sink (pad);
  x += v;
  x ^= v;
  v = x;
  return v - 1;
}

/* Far slots and pointers in one statement.  */
NOIPA int
far_ptr (int *p, char *q)
{
  volatile int pad[40];
  volatile int v = 11;
  volatile char c = 'q';

  sink (pad);
  p[1] = v;
  v = p[2];
  q[3] = c;
  c = q[1];
  return v + c;
}

int
main (void)
{
  struct s a = { 1, 2, 3 }, b = { 4, 5, 6 };
  int arr[3] = { 12, 20, 30 };
  int arr2[3] = { 1, 2, 3 };
  char str[6] = "abcde";
  struct big g;

  cp (&a, &b);
  check (20, a.b, 6);
  ad (arr, arr2);
  check (21, arr[1], 23);
  xo (arr, arr2);
  check (22, arr[0], 12 ^ 2);

  g = mkbig (100);
  check (23, g.w[0], 100);
  check (24, g.w[5], 105);

  check (25, far (1000, 50, 'A'), ((1050 - 1) << 2) + (47 >> 1) + 'A');
  check (26, far_r0 (5), ((5 + 7) ^ 7) - 1);
  arr[2] = 77;
  check (27, far_ptr (arr, str), 77 + 'b');
  check (28, arr[1], 11);
  check (29, str[3], 'q');
  return fail;
}

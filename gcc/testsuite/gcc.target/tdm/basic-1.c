/* Basic code generation: arithmetic, loops, compares, 32-bit add, word
   pointers, calls through a function pointer, char and initialized
   data.  Return 0, or the number of the first failed check.  */
/* { dg-do run } */

#define NOIPA __attribute__ ((noipa))

int g = 5;
int arr[4] = { 1, 2, 3, 4 };
int *gp = &arr[2];
const char msg[] = "hello";
struct s { char c; int i; } st = { 'x', 300 };

NOIPA int add (int a, int b) { return a + b; }
NOIPA int neg (int a) { return -a; }

NOIPA int sum (int n)
{
  int s = 0;
  for (int i = 0; i < n; i++)
    s += arr[i & 3];
  return s;
}

NOIPA unsigned umax (unsigned a, unsigned b) { return a > b ? a : b; }

NOIPA int call3 (int x) { return add (x, 7) + add (g, x) + neg (x); }

NOIPA int shifts (int x) { return (x << 3) ^ ((unsigned) x >> 2) ^ (x >> 5); }

NOIPA long ladd (long a, long b) { return a + b; }

NOIPA int deref (int *p) { return p[1] + *gp; }
NOIPA void store (int *p, int v) { p[2] = v; }

NOIPA int fp (int (*f) (int, int)) { return f (1, 2); }
NOIPA char lc (char c) { char x = c; return x + 1; }

static int fail;
static void check (int n, long got, long want) { if (got != want && !fail) fail = n; }

int main (void)
{
  int loc[4] = { 10, 20, 30, 40 };
  check (1, add (3, 4), 7);
  check (2, neg (5), -5);
  check (3, sum (6), 1 + 2 + 3 + 4 + 1 + 2);
  check (4, umax (3, 0xfff0), 0xfff0);
  check (5, umax (0xfff0, 3), 0xfff0);
  check (6, call3 (10), 10 + 7 + 5);
  check (7, shifts (-300), (-300 << 3) ^ ((unsigned) -300 >> 2) ^ (-300 >> 5));
  check (8, ladd (0x1234ffffL, 0x00010001L), 0x12360000L);
  check (9, deref (loc), 20 + 3);
  store (loc, 99);
  check (10, loc[2], 99);
  check (11, fp (add), 3);
  check (12, lc (41), 42);
  check (13, st.i, 300);
  check (14, st.c, 'x');
  return fail;
}

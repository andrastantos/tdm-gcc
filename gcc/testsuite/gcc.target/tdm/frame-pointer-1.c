/* Functions with a frame pointer: variable-sized arrays, alloca and
   __builtin_setjmp/__builtin_longjmp.  Return 0, or the number of the
   first failed check.  */
/* { dg-do run } */

#define NOIPA __attribute__ ((noipa))

static int fail;
static void check (int n, int got, int want) { if (got != want && !fail) fail = n; }

NOIPA void sink (volatile void *p) { (void) p; }

/* A VLA, locals around it and callee saved registers.  */
NOIPA int
vla (int n, int k)
{
  volatile int before = 100;
  int a[n];
  volatile char c[n];
  volatile int after = 200;
  int sum = 0;

  for (int i = 0; i < n; i++)
    {
      a[i] = i + k;
      c[i] = 'a' + i;
    }
  sink (a);
  for (int i = 0; i < n; i++)
    sum += a[i] + c[i];
  return sum + before + after;
}

/* VLAs in a loop: each iteration frees the previous one.  */
NOIPA int
vla_loop (int n)
{
  int total = 0;
  for (int i = 1; i <= n; i++)
    {
      int v[i];
      for (int j = 0; j < i; j++)
	v[j] = j;
      sink (v);
      total += v[i - 1];
    }
  return total;
}

NOIPA int
use_alloca (int n)
{
  volatile int x = 7;
  int *p = __builtin_alloca (n * sizeof (int));
  int *q = __builtin_alloca (n * sizeof (int));

  for (int i = 0; i < n; i++)
    {
      p[i] = i;
      q[i] = 2 * i;
    }
  sink (p);
  sink (q);
  return p[n - 1] + q[n - 1] + x;
}

/* A large frame with a VLA: far frame pointer offsets.  */
NOIPA int
big_vla (int n)
{
  volatile int pad[40];
  int v[n];
  sink (pad);
  pad[39] = 5;
  for (int i = 0; i < n; i++)
    v[i] = i + pad[39];
  sink (v);
  return v[n - 1] + pad[39];
}

static void *buf[5];
static volatile int depth;

NOIPA void
jump_out (int n)
{
  volatile int local[3];
  local[0] = n;
  sink (local);
  if (n == 0)
    __builtin_longjmp (buf, 1);
  depth++;
  jump_out (n - 1);
}

NOIPA int
catch_jump (int n)
{
  volatile int mark = 42;
  int vla_[n];

  vla_[0] = 9;
  sink (vla_);
  if (__builtin_setjmp (buf))
    return mark + vla_[0] + depth;
  jump_out (n);
  return -1;
}

int
main (void)
{
  check (1, vla (4, 3), (3 + 4 + 5 + 6) + ('a' * 4 + 6) + 300);
  check (2, vla_loop (5), 0 + 1 + 2 + 3 + 4);
  check (3, use_alloca (5), 4 + 8 + 7);
  check (4, big_vla (3), 2 + 5 + 5);
  check (5, catch_jump (3), 42 + 9 + 3);
  check (6, vla (2, 1), (1 + 2) + ('a' * 2 + 1) + 300);
  return fail;
}

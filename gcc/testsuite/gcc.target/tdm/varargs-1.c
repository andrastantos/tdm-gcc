/* The named arguments of a variadic function are passed as in any other
   call: the first in $r0 if it fits, the rest on the stack, followed by
   the unnamed ones.  Return 0, or the number of the first failed
   check.  */
/* { dg-do run } */
/* { dg-options "-O2" } */

#include <stdarg.h>

#define NOIPA __attribute__ ((noipa))

static int fail;
static void check (int n, long long got, long long want) { if (got != want && !fail) fail = n; }

/* One named argument, in $r0.  */
NOIPA int sum (int n, ...)
{
  va_list ap;
  int s = 0;
  va_start (ap, n);
  for (int i = 0; i < n; i++)
    s += va_arg (ap, int);
  va_end (ap);
  return s;
}

/* A pointer, and unnamed arguments of several sizes.  */
NOIPA long long mixed (const char *fmt, ...)
{
  va_list ap;
  long long s = 0;
  va_start (ap, fmt);
  for (; *fmt; fmt++)
    switch (*fmt)
      {
      case 'i': s += va_arg (ap, int); break;
      case 'l': s += va_arg (ap, long); break;
      case 'L': s += va_arg (ap, long long); break;
      }
  va_end (ap);
  return s;
}

/* A first named argument wider than a word: on the stack, with the
   rest.  */
NOIPA long wide (long base, ...)
{
  va_list ap;
  va_start (ap, base);
  long r = base + va_arg (ap, int);
  va_end (ap);
  return r;
}

/* Two named arguments: the second on the stack.  */
NOIPA int two (int a, int b, ...)
{
  va_list ap;
  va_start (ap, b);
  int r = a - b + va_arg (ap, int);
  va_end (ap);
  return r;
}

/* A va_list passed on.  */
NOIPA int vsum (int n, va_list ap)
{
  int s = 0;
  while (n--)
    s += va_arg (ap, int);
  return s;
}
NOIPA int fwd (int n, ...)
{
  va_list ap;
  va_start (ap, n);
  int s = vsum (n, ap);
  va_end (ap);
  return s;
}

/* A variadic function called through a pointer.  */
static int (*volatile psum) (int, ...) = sum;

int
main (void)
{
  check (1, sum (0), 0);
  check (2, sum (3, 1, 2, 3), 6);
  check (3, mixed ("ilL", 1, 0x20000L, 0x300000000LL), 0x300020001LL);
  check (4, wide (0x10000L, 5), 0x10005L);
  check (5, two (10, 3, 4), 11);
  check (6, fwd (4, 1, 2, 3, 4), 10);
  check (7, psum (2, 20, 22), 42);
  return fail;
}

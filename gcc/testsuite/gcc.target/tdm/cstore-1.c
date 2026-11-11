/* Comparisons as values (cstore), 16 and 32 bits, signed and
   unsigned, every condition, against a branch.  The empty asm keeps
   if-conversion from turning the reference into a cstore too.  Return
   0, or the number of the first failed check.  */
/* { dg-do run } */

#define NOIPA __attribute__ ((noipa))

static int fail;
static void check (int n, int got, int want) { if (got != want && !fail) fail = n; }

#define BR(A, OP, B) ({ int r_ = 0; if ((A) OP (B)) { __asm__ volatile (""); r_ = 1; } r_; })

#define FNS(T, P)							\
  NOIPA int P##lt (T a, T b) { return a < b; }				\
  NOIPA int P##le (T a, T b) { return a <= b; }				\
  NOIPA int P##gt (T a, T b) { return a > b; }				\
  NOIPA int P##ge (T a, T b) { return a >= b; }				\
  NOIPA int P##eq (T a, T b) { return a == b; }				\
  NOIPA int P##ne (T a, T b) { return a != b; }				\
  NOIPA int P##lt5 (T a) { return a < 5; }				\
  NOIPA int P##gt5 (T a) { return a > 5; }				\
  NOIPA int P##le1000 (T a) { return a <= 1000; }			\
  NOIPA int P##gtm1 (T a) { return a > (T) -1; }			\
  NOIPA int P##ne0 (T a) { return a != 0; }				\
  NOIPA void P##count (int *p, T a, T b) { *p += (a > b); }

FNS (int, i)
FNS (unsigned int, u)
FNS (long, l)
FNS (unsigned long, ul)

#define CHECKS(T, P, BASE)						\
  for (unsigned int i = 0; i < sizeof P##v / sizeof P##v[0]; i++)	\
    for (unsigned int j = 0; j < sizeof P##v / sizeof P##v[0]; j++)	\
      {									\
	T a = P##v[i], b = P##v[j];					\
	int c = 7;							\
	check (BASE + 1, P##lt (a, b), BR (a, <, b));			\
	check (BASE + 2, P##le (a, b), BR (a, <=, b));			\
	check (BASE + 3, P##gt (a, b), BR (a, >, b));			\
	check (BASE + 4, P##ge (a, b), BR (a, >=, b));			\
	check (BASE + 5, P##eq (a, b), BR (a, ==, b));			\
	check (BASE + 6, P##ne (a, b), BR (a, !=, b));			\
	P##count (&c, a, b);						\
	check (BASE + 7, c, 7 + BR (a, >, b));				\
	check (BASE + 8, P##lt5 (a), BR (a, <, (T) 5));		\
	check (BASE + 9, P##gt5 (a), BR (a, >, (T) 5));		\
	check (BASE + 10, P##le1000 (a), BR (a, <=, (T) 1000));	\
	check (BASE + 11, P##gtm1 (a), BR (a, >, (T) -1));		\
	check (BASE + 12, P##ne0 (a), BR (a, !=, (T) 0));		\
      }

static const int iv[] = { 0, 1, -1, 5, 6, 1000, 1001, 32767, -32768, -2 };
static const unsigned int uv[] = { 0, 1, 5, 6, 1000, 1001, 0x7fff, 0x8000, 0xfffe, 0xffff };
static const long lv[] = {
  0, 1, -1, 5, 1000, 1001, 0x10000, 0x10005, -0x10000, 0x7fffffff,
  -0x7fffffff - 1, 0xffff, -0xffff, 0x12345678, -0x12345678
};
static const unsigned long ulv[] = {
  0, 1, 5, 1000, 1001, 0xffff, 0x10000, 0x10005, 0x7fffffff,
  0x80000000, 0xffff0000, 0xfffffffe, 0xffffffff, 0x12345678
};

int
main (void)
{
  CHECKS (int, i, 0)
  CHECKS (unsigned int, u, 20)
  CHECKS (long, l, 40)
  CHECKS (unsigned long, ul, 60)
  return fail;
}

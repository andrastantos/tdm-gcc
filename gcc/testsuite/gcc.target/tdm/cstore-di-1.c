/* 64-bit == and != as values: the OR of the XORs of the words, compared
   with 0, not (-x | x) >> 63 through __lshrdi3.  */
/* { dg-do run } */
/* { dg-additional-options "-save-temps" } */

#define NOIPA __attribute__ ((noipa))

NOIPA int ne0 (long long a) { return a != 0; }
NOIPA int eq0 (long long a) { return a == 0; }
NOIPA int ne (long long a, long long b) { return a != b; }
NOIPA int eq (unsigned long long a, unsigned long long b) { return a == b; }

static const long long v[] = {
  0, 1, -1, 1LL << 16, 1LL << 32, 1LL << 48, 1LL << 63, 0x123456789abcdefLL
};

int
main (void)
{
  int n = sizeof v / sizeof v[0];
  for (int i = 0; i < n; i++)
    {
      if (ne0 (v[i]) != (v[i] != 0) || eq0 (v[i]) != (v[i] == 0))
	__builtin_abort ();
      for (int j = 0; j < n; j++)
	if (ne (v[i], v[j]) != (i != j) || eq (v[i], v[j]) != (i == j))
	  __builtin_abort ();
    }
  return 0;
}

/* { dg-final { scan-assembler-not "__lshrdi3" } } */

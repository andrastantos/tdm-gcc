/* The byte address of a stack slot within 32 words of $sp, in $r0, is
   "mov $r0, $sp+N; add $r0, $r0" (and "add $r0, 1" for a high octet),
   not a copy of $sp added to itself and an offset.  */
/* { dg-do run } */
/* { dg-additional-options "-save-temps" } */
/* { dg-skip-if "made by peephole2" { *-*-* } { "-O0" "-O1" } { "" } } */

#define NOIPA __attribute__ ((noipa))

NOIPA void
set (int *p, int v)
{
  *p = v;
}

NOIPA void
setc (char *p, char v)
{
  *p = v;
}

NOIPA int
sum (const int *p, int n)
{
  int s = 0;
  for (int i = 0; i < n; i++)
    s += p[i];
  return s;
}

/* Slots at even and odd octets, near and (C, at more than 15 words)
   beyond what "add $r0, N" reaches.  */
NOIPA int
slots (int x)
{
  int a, b, c;
  int pad[20];
  char s[6];

  for (int i = 0; i < 20; i++)
    set (&pad[i], x + i);
  set (&a, 100);
  set (&b, 200);
  set (&c, 300);
  setc (&s[0], 1);
  setc (&s[1], 2);
  setc (&s[3], 4);
  setc (&s[5], 6);
  return a + b + c + sum (pad, 20) + s[0] + s[1] * 3 + s[3] * 5 + s[5] * 7;
}

int
main (void)
{
  if (slots (10) != 600 + 200 + 190 + 1 + 6 + 20 + 42)
    __builtin_abort ();
  return 0;
}

/* { dg-final { scan-assembler {\tmov\t\$r0, \$sp(\+[0-9]+)?\n\tadd\t\$r0, \$r0\n} } } */
/* { dg-final { scan-assembler {\tadd\t\$r0, \$r0\n\tadd\t\$r0, 1\n} } } */
/* { dg-final { scan-assembler-not {\tmov\t\$r0, \$sp\n\tadd\t\$r0, \$sp\n} } } */

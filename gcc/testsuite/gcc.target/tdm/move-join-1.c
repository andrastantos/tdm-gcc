/* When the output of an insn ends with a move between $r1 and a memory
   register, a move between the same two that starts the output of the
   next insn goes.  Here: the OR of the words of G, kept in $mr7.  */
/* { dg-do run } */
/* { dg-additional-options "-save-temps" } */

unsigned long long g;

__attribute__ ((noipa)) int
nonzero (void)
{
  return g != 0;
}

int
main (void)
{
  if (nonzero ())
    __builtin_abort ();
  g = 1ULL << 50;
  if (!nonzero ())
    __builtin_abort ();
  return 0;
}

/* { dg-final { scan-assembler-not {\tmov\t(\$mr[0-9]+), \$r1\n\tmov\t\$r1, \1\n} } } */
/* { dg-final { scan-assembler-not {\tmov\t\$r1, (\$mr[0-9]+)\n\tmov\t\1, \$r1\n} } } */

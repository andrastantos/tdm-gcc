/* 32-bit addition, subtraction and negation take the carry from a
   predicate instruction, without branches or a store-flag sequence.  */
/* { dg-do compile } */
/* { dg-options "-Os" } */

long add (long a, long b) { return a + b; }
long sub (long a, long b) { return a - b; }
long neg (long a) { return -a; }

/* { dg-final { scan-assembler-times "if_ltu\t" 2 } } */
/* { dg-final { scan-assembler-times "if_eq\t" 1 } } */
/* { dg-final { scan-assembler-times "add\t\\\$r\[01\], 1\n" 2 } } */
/* { dg-final { scan-assembler-times "sub\t\\\$r\[01\], 1\n" 1 } } */
/* { dg-final { scan-assembler-not "mov\t\\\$pc, \\\$pc\\+" } } */

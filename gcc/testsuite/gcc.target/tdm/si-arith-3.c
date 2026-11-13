/* 32-bit subtraction does the high word and the borrow before the low
   word, and the borrow and carry compare with stack arguments in place:
   none of these needs a memory register.  */
/* { dg-do compile } */
/* At -O0 and -O1 the arguments are copied to registers first.  */
/* { dg-skip-if "" { *-*-* } { "-O0" "-O1" } { "" } } */

long sub (long a, long b) { return a - b; }
long addc (long a) { return a + 100000; }

/* { dg-final { scan-assembler-not "\\\$mr" } } */

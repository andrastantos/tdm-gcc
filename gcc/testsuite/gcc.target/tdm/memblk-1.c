/* Block copies and clears of octets of unknown half call memcpy and
   memset instead of moving one octet at a time; word aligned ones stay
   inline.  */
/* { dg-do compile } */
/* { dg-options "-O2" } */

struct c6 { char c[6]; };
struct w8 { int a[8]; };

void cp6 (struct c6 *d, struct c6 *s) { *d = *s; }
void cl5 (char *d) { __builtin_memset (d, 0, 5); }
void cpw (struct w8 *d, struct w8 *s) { *d = *s; }
void clw (struct w8 *d) { *d = (struct w8) { 0 }; }

/* { dg-final { scan-assembler-times "\\.word\tmemcpy" 1 } } */
/* { dg-final { scan-assembler-times "\\.word\tmemset" 1 } } */
/* { dg-final { scan-assembler-not "__tdm_(load|store)8" } } */

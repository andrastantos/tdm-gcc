/* libgcc's multiplication routines add the multiplicand for each set
   bit.  The conditional add mustn't turn into a multiplication by
   (y & 1): that would call the routine itself, forever.  */
/* { dg-do compile } */
/* { dg-final { scan-assembler-not "\\.word\\t__mulsi3" } } */
/* { dg-final { scan-assembler-not "\\.word\\t__mulhi3" } } */

long
__mulsi3 (long a, long b)
{
  unsigned long x = a, y = b, r = 0;

  while (y)
    {
      if (y & 1)
	r += x;
      x <<= 1;
      y >>= 1;
    }
  return r;
}

int
__mulhi3 (int a, int b)
{
  unsigned int x = a, y = b, r = 0;

  while (y)
    {
      if (y & 1)
	r += x;
      x <<= 1;
      y >>= 1;
    }
  return r;
}

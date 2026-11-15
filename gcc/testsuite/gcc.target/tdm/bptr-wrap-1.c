/* The address of an object with an offset that wraps around below zero
   links, and wraps around.  */
/* { dg-do run } */
/* { dg-options "-O2" } */

unsigned char b[4] = { 1, 2, 3, 4 };

__attribute__ ((noipa)) unsigned char *
bias (void)
{
  return b - 30000;
}

__attribute__ ((noipa)) int
get (unsigned char *p, unsigned int i)
{
  return p[i];
}

int
main (void)
{
  if (get (bias (), 30002) != 3)
    __builtin_abort ();
  return 0;
}

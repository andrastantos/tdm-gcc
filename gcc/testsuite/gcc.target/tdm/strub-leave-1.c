/* Stack scrubbing isn't expanded inline at -O3: the loop would clear
   the scratch word [$sp-1] while its own pointer is saved there, and
   stop after one word.  */
/* { dg-do run } */
/* { dg-options "-O3 -fstrub=strict" } */
/* { dg-require-effective-target strub } */

#include "../../c-c++-common/torture/strub-run1.c"

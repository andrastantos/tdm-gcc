/* Octets whose half of the word is only known at run time go through
   the helpers __tdm_load8 and __tdm_store8 in libgcc: at odd and even
   addresses, in a leaf function (which then gets a frame for the
   helper's return address), with the value in $r1 or elsewhere, and
   with $r1 live across a store.  Return 0, or the number of the first
   failed check.  */
/* { dg-do run } */
/* { dg-options "-Os -save-temps" } */

#define NOIPA __attribute__ ((noipa))

static int fail;
static void check (int n, int got, int want) { if (got != want && !fail) fail = n; }

NOIPA char get (const char *p, int i) { return p[i]; }
NOIPA void put (char *p, int i, char v) { p[i] = v; }
NOIPA void copy (char *d, const char *s, int n) { while (n--) *d++ = *s++; }
NOIPA int sum (const char *p, int n)
{
  int s = 0;
  for (int i = 0; i < n; i++)
    s += p[i];
  return s;
}
/* A store whose value stays live in a register afterwards.  */
NOIPA int put_both (char *p, char *q, char v) { *p = v; *q = v; return v + 1; }

int
main (void)
{
  char buf[8] = "abcdefg";
  char out[8] = { 0 };

  check (1, get (buf, 0), 'a');
  check (2, get (buf, 3), 'd');
  check (3, get (buf + 1, 1), 'c');
  put (buf, 1, 'B');
  put (buf, 4, 'E');
  check (4, buf[0], 'a');
  check (5, buf[1], 'B');
  check (6, buf[2], 'c');
  check (7, buf[4], 'E');
  check (8, buf[5], 'f');
  copy (out + 1, buf + 2, 3);
  check (9, out[0], 0);
  check (10, out[1], 'c');
  check (11, out[2], 'd');
  check (12, out[3], 'E');
  check (13, out[4], 0);
  check (14, sum (buf, 7), 'a' + 'B' + 'c' + 'd' + 'E' + 'f' + 'g');
  check (15, put_both (buf + 6, out + 5, 'z'), 'z' + 1);
  check (16, buf[6], 'z');
  check (17, out[5], 'z');
  check (18, buf[5], 'f');
  check (19, out[6], 0);
  return fail;
}

/* { dg-final { scan-assembler {\.word\t__tdm_load8} } } */
/* { dg-final { scan-assembler {\.word\t__tdm_store8} } } */

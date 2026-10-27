/* Octet access through pointers.  Return 0, or the number of the
   first failed check.  */
/* { dg-do run } */
/* Keep len and cpy as loops: there is no libc to call.  */
/* { dg-additional-options "-fno-tree-loop-distribute-patterns" } */

#define NOIPA __attribute__ ((noipa))

static int fail;
static void check (int n, int got, int want) { if (got != want && !fail) fail = n; }

char buf[8] = "abcdefg";
const char *msg = "hello, world";
struct s { char c; char d; int i; char e; } sv = { 'x', 'y', 300, 'z' };
signed char sc[4] = { -1, 2, -128, 127 };

NOIPA char get (const char *p, int i) { return p[i]; }
NOIPA char get1 (const char *p) { return p[1]; }
NOIPA char get3 (const char *p) { return p[3]; }
NOIPA char get40 (const char *p) { return p[40]; }
NOIPA void put (char *p, int i, char v) { p[i] = v; }
NOIPA void put1 (char *p, char v) { p[1] = v; }
NOIPA void put3 (char *p, char v) { p[3] = v; }
NOIPA int sget (const signed char *p, int i) { return p[i]; }
NOIPA int len (const char *p) { int n = 0; while (*p++) n++; return n; }
NOIPA void cpy (char *d, const char *s) { while ((*d++ = *s++)); }
NOIPA char sd (struct s *p) { return p->d; }
NOIPA char se (struct s *p) { return p->e; }
NOIPA void setd (struct s *p, char v) { p->d = v; }
NOIPA int sum (const char *p, int n)
{ int t = 0; for (int i = 0; i < n; i++) t += p[i]; return t; }

int main (void)
{
  char loc[50];
  for (int i = 0; i < 8; i++)
    check (1 + i, get (buf, i), "abcdefg"[i]);
  check (10, get1 (buf), 'b');
  check (11, get3 (buf), 'd');
  check (12, get1 (buf + 1), 'c');
  check (13, get3 (buf + 1), 'e');
  check (14, len (msg), 12);
  check (15, get (msg, 7), 'w');
  put (buf, 2, 'C');
  check (16, buf[1] + buf[2] * 256 + buf[3], 'b' + 'C' * 256 + 'd');
  put (buf, 3, 'D');
  check (17, buf[2] + buf[3] * 256 + buf[4], 'C' + 'D' * 256 + 'e');
  put1 (buf + 1, 'X');
  check (18, buf[1] + buf[2] * 256 + buf[3], 'b' + 'X' * 256 + 'D');
  put3 (buf, 'Y');
  check (19, buf[2] + buf[3] * 256 + buf[4], 'X' + 'Y' * 256 + 'e');
  put (buf, 7, 0xff);
  check (20, get (buf, 7), 0xff);
  check (21, sget (sc, 0), -1);
  check (22, sget (sc, 1), 2);
  check (23, sget (sc, 2), -128);
  check (24, sget (sc, 3), 127);
  cpy (loc, msg);
  check (25, len (loc), 12);
  check (26, get (loc, 11), 'd');
  cpy (loc + 1, "odd");
  check (27, loc[0] * 256 + loc[1], 'h' * 256 + 'o');
  check (28, len (loc + 1), 3);
  check (29, sd (&sv), 'y');
  check (30, se (&sv), 'z');
  setd (&sv, 'Q');
  check (31, sv.c * 256 + sv.d, 'x' * 256 + 'Q');
  check (32, sv.i, 300);
  for (int i = 0; i < 50; i++)
    loc[i] = i;
  check (33, get40 (loc), 40);
  check (34, get40 (loc + 1), 41);
  check (35, sum (loc + 1, 5), 1 + 2 + 3 + 4 + 5);
  check (36, sum (loc, 50), 49 * 50 / 2);
  return fail;
}

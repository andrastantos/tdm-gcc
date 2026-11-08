/* Octet accesses whose half of the word is known: fields of structs
   reached through a pointer, at odd and even offsets; elements of arrays
   of structs of even size.  Accesses whose half isn't known (a char
   pointer, structs of odd size, a pointer to an odd octet) test it at
   run time.  Return 0, or the number of the first failed check.  */
/* { dg-do run } */
/* { dg-options "-Os -save-temps" } */

#define NOIPA __attribute__ ((noipa))

static int fail;
static void check (int n, int got, int want) { if (got != want && !fail) fail = n; }

struct s { int i; char a, b, c; };	/* 6 octets: a at 2, b at 3, c at 4.  */
struct t { char x, y, z; };		/* 3 octets, not word aligned.  */
struct u { int i; struct s in; char d; };

NOIPA char getb (struct s *p) { return p->b; }
NOIPA char geta (struct s *p) { return p->a; }
NOIPA void setb (struct s *p, char v) { p->b = v; }
NOIPA void seta (struct s *p, char v) { p->a = v; }
NOIPA char nested (struct u *p) { return p->in.b + p->d; }
NOIPA char elem (struct s *p, int i) { return p[i].b; }
NOIPA void set_elem (struct s *p, int i, char v) { p[i].c = v; }
NOIPA char odd_struct (struct t *p, int i) { return p[i].y; }
NOIPA char through_char (char *q, int i) { return q[i]; }

int
main (void)
{
  struct s v[3] = { { 1, 'a', 'b', 'c' }, { 2, 'd', 'e', 'f' },
		    { 3, 'g', 'h', 'i' } };
  struct t w[3] = { { 'j', 'k', 'l' }, { 'm', 'n', 'o' }, { 'p', 'q', 'r' } };
  struct u x = { 4, { 5, 's', 't', 'u' }, 1 };

  check (1, getb (&v[0]), 'b');
  check (2, geta (&v[1]), 'd');
  setb (&v[2], 'H');
  check (3, v[2].b, 'H');
  check (4, v[2].a, 'g');
  check (5, v[2].c, 'i');
  seta (&v[0], 'A');
  check (6, v[0].a, 'A');
  check (7, v[0].b, 'b');
  check (8, nested (&x), 't' + 1);
  check (9, elem (v, 1), 'e');
  set_elem (v, 2, 'I');
  check (10, v[2].c, 'I');
  check (11, v[2].b, 'H');
  check (12, odd_struct (w, 1), 'n');
  check (13, odd_struct (w, 2), 'q');
  /* A pointer to an odd octet: its parity isn't the struct's.  */
  check (14, through_char (&v[1].b, 1), 'f');
  check (15, through_char (&v[1].a, 1), 'e');
  check (16, through_char (&w[0].y, 2), 'm');
  return fail;
}

/* The fields of struct s through a pointer need no run-time test: no
   "if_ges $r0, 0" from the label of the function to its .size.  */
/* { dg-final { scan-assembler {\ngetb:\n(?:(?!\tif_ges\t\$r0, 0\n)[^\n]*\n)*?\t\.size\tgetb,} } } */
/* { dg-final { scan-assembler {\nsetb:\n(?:(?!\tif_ges\t\$r0, 0\n)[^\n]*\n)*?\t\.size\tsetb,} } } */
/* { dg-final { scan-assembler {\nnested:\n(?:(?!\tif_ges\t\$r0, 0\n)[^\n]*\n)*?\t\.size\tnested,} } } */
/* And those of the odd-sized struct t still do, in __tdm_load8.  */
/* { dg-final { scan-assembler {\nodd_struct:\n(?:[^\n]*\n)*?\t\.word\t__tdm_load8\n(?:[^\n]*\n)*?\t\.size\todd_struct,} } } */

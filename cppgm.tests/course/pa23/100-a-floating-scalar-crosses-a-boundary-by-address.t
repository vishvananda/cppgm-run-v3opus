// 5.2.2p4 and 3.9.1p8: the course ABI carries a class object as the bytes it
// occupies where those bytes stand for the object and the object is narrow
// enough for registers - but the registers a class holding a floating scalar
// would be carried in are the floating ones, and no boundary of this milestone
// names those.  So such a class crosses by address however narrow it is, and a
// class wider than two words crosses as its bytes because the ABI already left
// it in the caller's storage.
//
// The question is asked of the whole class rather than of eightbytes, so a
// float beside an int, a float inside a member, a float in a base and a float
// in an array all answer the same way.

struct words
{
  long a;
};

struct one_float
{
  double a;
};

struct mixed
{
  int a;
  float b;
};

struct nested
{
  struct inner
  {
    float f;
  } m;
};

struct based : one_float
{
};

struct wide
{
  double a;
  double b;
  char c;
};

union either
{
  double a;
  long b;
};

union integral
{
  int a;
  long b;
};

struct bytes
{
  char a[16];
};

struct floats
{
  double a[2];
};

words take_words(words v) { return v; }
one_float take_one_float(one_float v) { return v; }
mixed take_mixed(mixed v) { return v; }
nested take_nested(nested v) { return v; }
based take_based(based v) { return v; }
wide take_wide(wide v) { return v; }
either take_either(either v) { return v; }
integral take_integral(integral v) { return v; }
bytes take_bytes(bytes v) { return v; }
floats take_floats(floats v) { return v; }

template<class T>
T round_trip(T v)
{
  return v;
}

int main()
{
  words w;
  w.a = 7;
  one_float f;
  f.a = 1.5;
  mixed m;
  m.a = 3;
  m.b = 0.5f;
  nested n;
  n.m.f = 2.5f;
  based b;
  b.a = 3.5;
  wide d;
  d.a = 4.5;
  d.b = 5.5;
  d.c = 'x';
  either e;
  e.a = 6.5;
  integral i;
  i.b = 9;
  bytes y;
  y.a[0] = 'z';
  floats g;
  g.a[0] = 7.5;
  g.a[1] = 8.5;

  int settled = 0;
  settled += take_words(w).a == 7 ? 0 : 1;
  settled += take_one_float(f).a == 1.5 ? 0 : 1;
  settled += take_mixed(m).b == 0.5f ? 0 : 1;
  settled += take_nested(n).m.f == 2.5f ? 0 : 1;
  settled += take_based(b).a == 3.5 ? 0 : 1;
  settled += take_wide(d).b == 5.5 ? 0 : 1;
  settled += take_either(e).a == 6.5 ? 0 : 1;
  settled += take_integral(i).b == 9 ? 0 : 1;
  settled += take_bytes(y).a[0] == 'z' ? 0 : 1;
  settled += take_floats(g).a[1] == 8.5 ? 0 : 1;
  settled += round_trip(f).a == 1.5 ? 0 : 1;
  settled += round_trip(w).a == 7 ? 0 : 1;
  return settled;
}

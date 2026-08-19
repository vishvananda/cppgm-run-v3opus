// 8.3.5p4 with 14.8.2.1p1: an argument the ellipsis takes is matched to no
// parameter, so it is a P/A pair of nothing - and the places the declarator did
// write still deduce, so a call that wrote more arguments than the head has
// places names the template all the same.

template <class U>
int first(U u, ...)
{
  return int(u);
}

template <class U, class V>
int second(U, V v, ...)
{
  return int(v);
}

struct four
{
  operator int() const { return 4; }
};

int main()
{
  return first(2) == 2 && first(3, 9, 8) == 3 && second(1, four()) == 4 ? 0 : 1;
}

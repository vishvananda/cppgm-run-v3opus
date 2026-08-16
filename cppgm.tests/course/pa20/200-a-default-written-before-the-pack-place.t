// VALIDATION: compile-pass
// 14.1p9 and 14.1p11: a place written before a class template's pack may
// carry a default template-argument of its own, so an argument list that
// writes nothing at all is filled by the defaults up to the pack - and the
// pack itself is bound to no arguments, which is a run of none and not an
// argument the list is short of.

template<int N = 5, class... T>
struct counted
{
  static const int held = N + (int)sizeof...(T);
};

template<class T = char, class... U>
struct typed
{
  static const int held = 1 + (int)sizeof...(U);
};

int written = counted<2, int, char>::held;
int defaulted = counted<>::held;
int stopped = counted<7>::held;
int named = typed<>::held;

int main()
{
  return written + defaulted + stopped + named;
}

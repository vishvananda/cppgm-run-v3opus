// N3485 focus: 14.5.1 [temp.class] p1 with 5.19 [expr.const] p2 - what a
// specialization of a variable template *is* is the constant its initializer
// evaluates to, so there is nothing to hold until that reading is over.  A
// naming of the same argument list reached from inside the reading is therefore
// asking the reading for its own answer, which no constant expression has: a
// class template needs no such rule, because its specialization is held before
// its body is read and a naming inside that body finds it incomplete.
template<int N>
constexpr int settled = N;

template<int N>
constexpr int circular = circular<N> + settled<N>;

int main()
{
  return circular<1>;
}

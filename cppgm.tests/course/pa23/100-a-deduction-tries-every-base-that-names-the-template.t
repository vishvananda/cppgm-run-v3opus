// VALIDATION: run-pass
// 14.8.2.1p3: the A a P/A pair deduces may be a base class of what the call
// passed, and 10p1 makes a base of a base a base - so the whole tree below the
// argument is walked.  A class that already names P's template is no answer on
// its own: `impl<0, int, char>` derives from `impl<1, char>`, so a pattern its
// own arguments refuse is one a base of it still deduces, and the walk goes on
// past every naming the pattern does not match.

template<unsigned long I, class... Ts>
struct impl {};

template<unsigned long I, class Head, class... Tail>
struct impl<I, Head, Tail...> : impl<I + 1, Tail...> {};

template<class... Ts>
struct tuple : impl<0, Ts...> {};

template<unsigned long I, class Head, class... Tail>
int at(impl<I, Head, Tail...> &)
{
  return static_cast<int>(sizeof...(Tail));
}

template<class A, class B>
struct pair {};

struct mixed : pair<int, int>, pair<long, char> {};

template<class A>
int second(pair<A, char> &)
{
  return 2;
}

int main()
{
  tuple<int, char, long> t;
  mixed m;
  return at<0>(t) == 2 && at<1>(t) == 1 && at<2>(t) == 0 && second(m) == 2
    ? 0 : 1;
}

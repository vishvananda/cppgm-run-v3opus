// N3485 focus: 14.5.3 [temp.variadic] p4 with 14.8.2.1 [temp.deduct.call] p1 -
// the pattern of a pack expansion written in a parameter-declaration-clause is
// read once per element of the run, in a region binding the packs it names to
// that element.  A pattern that is itself a class template specialization is the
// shape that tells that reading from the other one: substituting the whole run
// *into* the pattern names one specialization `wrap<A...>` over every element at
// once, which nothing can take back apart, where reading the pattern per element
// names `wrap<int>` at the first place and `wrap<char>` at the second.
//
// Each function is named only in an unevaluated operand, so what the program
// observes is the run each deduction settled and the specialization it chose -
// which is the fact the reading is asked for.
template<class T>
struct wrap {
  typedef T type;
  T v;
};

template<class... A>
struct held {
  static const int n = sizeof...(A);
};

// 14.8.2.1p1 over 14.5.3p4's trailing pattern: every argument the fixed places
// did not take is a P/A pair of its own against this pattern, and the run is
// what the pack's own place took in each of them.
template<class... A>
held<A...> by_value(wrap<A>... p) { return held<A...>(); }

template<class... A>
held<A...> by_pointer(wrap<A>*... p) { return held<A...>(); }

template<class... A>
held<A...> by_reference(const wrap<A>&... p) { return held<A...>(); }

// 14.6.2p1 with 14.8.2.5p5: a pattern that names a member of the specialization
// rather than the specialization is a non-deduced context, so the list that
// fills its places is written - and each element is still what the prefix is
// settled against.
template<class... A>
held<A...> by_member(typename wrap<A>::type... p) { return held<A...>(); }

// 14.5.3p4 in a member of a class template, where the pattern names the class's
// own run and the member's head declares a second one.
template<class... A>
struct outer {
  template<class... B>
  static held<A..., B...> both(wrap<A>... p, wrap<B>... q)
  { return held<A..., B...>(); }
};

int main()
{
  wrap<int> i;
  wrap<char> c;

  // The run the call deduced, counted where the specialization records it.
  static_assert(decltype(by_value(i, c))::n == 2, "two elements deduced");
  static_assert(decltype(by_value(i))::n == 1, "a run of one");
  static_assert(decltype(by_value())::n == 0, "a run of none");
  static_assert(decltype(by_pointer(&i, &c))::n == 2, "a pointer pattern");
  static_assert(decltype(by_reference(i, c))::n == 2, "a reference pattern");

  // 14.8.1p2: the same places filled by a written argument list rather than by a
  // deduction, which reads the very same pattern once per element.
  static_assert(decltype(by_member<int, char>(1, 'a'))::n == 2,
                "a member pattern");
  static_assert(decltype(by_value<int, char>(i, c))::n == 2, "an explicit list");
  static_assert(decltype(outer<int, char>::both(i, c, wrap<long>()))::n == 3,
                "two heads, two runs");

  // What the places each took: a call whose arguments are the elements in the
  // other order is a different specialization, because the pattern read at the
  // first place named `wrap<char>` there and `wrap<int>` at the second.
  static_assert(sizeof(decltype(by_value(i, c))) == sizeof(held<int, char>),
                "the run is the deduced order");
  static_assert(decltype(by_value(c, i))::n == 2, "the other order deduces too");

  int room[decltype(by_value(i, c, wrap<long>()))::n];
  return sizeof(room) / sizeof(room[0]) == 3 ? 0 : 1;
}

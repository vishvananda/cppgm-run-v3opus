// VALIDATION: compile-pass
// N3485 7.1.6.2p1 with 14.2 and 5.2.3: a nested-name-specifier may begin with a
// decltype-specifier, and every component written after it is looked up in the
// region the type of that expression names.  That region answers the same two
// questions a prefix written as a name answers and no fewer: a component may be
// a template-id, whose arguments are what make a declaration of the template the
// lookup found; and a *name* the lookup ends at may be a type, in which case the
// `(` after it opens 5.2.3's explicit type conversion rather than a call.
//
// Both were written at the door a prefix spelled as a name goes through and at
// neither of the two a decltype-specifier goes through, so a template-id after
// one was "no declaration is in scope" and a conversion through one was "does
// not name an object or a function".

struct maker
{
  template<class T>
  struct box
  {
    typedef T type;
    static const int width = sizeof(T);
  };

  typedef short plain;

  static maker make();
};

template<class T>
struct wrapping
{
  static T made();
};

typedef decltype(maker::make())::template box<int> named_box;
typedef decltype(maker::make())::template box<char>::type named_element;
typedef decltype(wrapping<maker>::made())::template box<long> through_a_template;

template<class T>
struct asked_in_a_template
{
  typedef typename decltype(wrapping<T>::made())::template box<int>::type type;

  enum
  {
    width = decltype(wrapping<T>::made())::template box<double>::width
  };
};

named_element narrowed = 7;

int main()
{
  const int by_typedef = named_box::width + through_a_template::width;
  const int converted = decltype(maker::make())::plain() +
                        decltype(maker::make())::template box<int>::type(5) +
                        decltype(maker::make())::plain(3);
  asked_in_a_template<maker>::type from_a_template = 1;
  return by_typedef == 12 && converted == 8 && narrowed == 7 &&
                 from_a_template == 1 &&
                 asked_in_a_template<maker>::width == 8
             ? 0
             : 1;
}

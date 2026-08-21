// VALIDATION: compile-pass
// N3485 14.6p8 with 3.4.3.1p1: a template definition is read where it stands,
// and a name it writes that no template parameter stands in the way of is one
// that reading answers on its own.  Such a name may reach a class template
// specialization the unit has not instantiated yet, and a lookup made *in* that
// class is a context that requires it completely defined - so the reading has
// to instantiate it in earnest rather than answer "nothing is owed here".
//
// 14.8.2p8 is what makes the difference visible: a detector deduces `U` from
// its first argument and then substitutes it into a default argument's type,
// where `typename U::fusion_tag` and `U::count` are looked up in whatever class
// the argument named.  A class left declared answers neither, so the detector
// found no member, the deduction was refused, and the candidate dropped to the
// `...` fallback for a class that does declare one.

template<class T>
struct type_wrapper;

struct no_tag
{
  char pad[2];
};

template<class T>
struct holder
{
  typedef int fusion_tag;
  static const int count = 3;
  template<class U>
  struct rebind
  {
    typedef U type;
  };
  struct inner
  {
    typedef char fusion_tag;
    static const int count = 4;
  };
};

template<class T>
struct bare
{
  typedef T value_type;
};

template<class T>
using aliased = holder<T>;

template<class U>
char detect_type(type_wrapper<U> const volatile *,
                 type_wrapper<typename U::fusion_tag> * = 0);

template<class U>
char detect_value(type_wrapper<U> const volatile *, int (*)[U::count] = 0);

template<class U>
char detect_template(type_wrapper<U> const volatile *,
                     typename U::template rebind<int>::type * = 0);

no_tag detect_type(...);
no_tag detect_value(...);
no_tag detect_template(...);

template<class T>
struct has_fusion_tag
{
  static const int value =
      sizeof(detect_type(static_cast<type_wrapper<T> *>(0)));
};

template<class T>
struct has_count
{
  static const int value =
      sizeof(detect_value(static_cast<type_wrapper<T> *>(0)));
};

template<class T>
struct has_rebind
{
  static const int value =
      sizeof(detect_template(static_cast<type_wrapper<T> *>(0)));
};

// Every one of these names is written inside a template definition and depends
// on no parameter of it, so each is answered where the definition stands.
template<class First>
struct asked_in_a_class
{
  enum
  {
    spelled = has_fusion_tag<holder<int> >::value,
    valued = has_count<holder<int> >::value,
    listed = has_rebind<holder<int> >::value,
    through_alias = has_fusion_tag<aliased<long> >::value,
    nested = has_fusion_tag<holder<char>::inner>::value,
    missing = has_fusion_tag<bare<int> >::value
  };
};

template<class First>
struct outer_of
{
  template<class Second>
  struct asked_one_class_in
  {
    enum
    {
      spelled = has_count<holder<short> >::value,
      missing = has_count<bare<short> >::value
    };
  };
};

template<class First>
int asked_in_a_function()
{
  return has_fusion_tag<holder<double> >::value +
         has_rebind<holder<double> >::value;
}

int main()
{
  const int found = asked_in_a_class<void>::spelled +
                    asked_in_a_class<void>::valued +
                    asked_in_a_class<void>::listed +
                    asked_in_a_class<void>::through_alias +
                    asked_in_a_class<void>::nested;
  const int refused = asked_in_a_class<void>::missing +
                      outer_of<void>::asked_one_class_in<void>::missing;
  const int one_class_in = outer_of<void>::asked_one_class_in<void>::spelled;
  return found == 5 && refused == 4 && one_class_in == 1 &&
                 asked_in_a_function<void>() == 2
             ? 0
             : 1;
}

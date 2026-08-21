// VALIDATION: compile-pass
// N3485 14.8.1p2 with 7.1.6.2p1: a template-id whose name is a *function*
// template names the specializations its argument list makes, which are
// declarations no region's chain holds - so a lookup of the whole spelling
// finds nothing and the set has to be built from the template the name reached.
//
// A name written after a decltype-specifier is looked up in the region that
// type names, and it makes every ask a name written after a name prefix makes:
// the ordinary lookup, the class-or-alias template-id, and this one.  The third
// was written at the door a prefix spelled as a name goes through and at
// neither of the two a decltype-specifier goes through, so `A::template
// pick<int>(3)` was a call and `decltype(A::make())::template pick<int>(3)` was
// "no declaration is in scope" - at the call, at `&` and at the value alike.
//
// 13.3 then chooses among the set exactly as it does for the name spelling,
// 9.3.1p3's implied object reaches a non-static member through it, and 5.19
// reads the same call where a constant expression is what the program wrote.

struct holder
{
  static holder make();

  template<class T>
  static T pick(T value)
  {
    return value;
  }

  template<class T>
  static int rank(T *)
  {
    return 2;
  }

  template<class T>
  static int rank(T)
  {
    return 1;
  }

  template<class T>
  static constexpr T folded(T value)
  {
    return value;
  }

  int held;

  template<class T>
  T added(T value)
  {
    return value + held;
  }

  int through_the_object()
  {
    return decltype(holder::make())::template added<int>(4);
  }
};

struct outer
{
  struct nested
  {
    template<class T>
    static T pick(T value)
    {
      return value;
    }
  };

  static nested made();
};

template<class T>
struct wrapping
{
  static T made();
};

template<class U>
struct asked_in_a_class_template
{
  static int run()
  {
    return decltype(wrapping<holder>::made())::template pick<int>(3);
  }
};

template<class U>
int asked_in_a_function_template(U)
{
  return decltype(holder::make())::template pick<int>(5);
}

int spelled_out()
{
  int written = 0;
  return decltype(holder::make())::template pick<int>(6) +
         decltype(outer::made())::template pick<int>(7) +
         decltype(holder::make())::template rank<int>(&written) +
         decltype(holder::make())::template rank<int>(written);
}

int bound[decltype(holder::make())::template folded<int>(3)];

int main()
{
  holder object;
  object.held = 3;
  static_assert(decltype(holder::make())::template folded<int>(9) == 9,
                "5.19 reads the same call");
  return spelled_out() == 16 && asked_in_a_class_template<char>::run() == 3 &&
                 asked_in_a_function_template('c') == 5 &&
                 object.through_the_object() == 7 &&
                 sizeof(bound) / sizeof(int) == 3
             ? 0
             : 1;
}

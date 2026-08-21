// VALIDATION: compile-pass
// 7.1.6.4p6: a declarator written `auto` derives its type from an invented
// template parameter, and what that parameter stands for is deduced from the
// initializer standing beside the declarator by 14.8.2.1's rules for a call.
// So every adjustment a call makes is made here: an array argument is a
// pointer, a function is a pointer to itself, a top-level cv-qualifier of the
// argument is dropped where the place takes a value and kept where a reference
// binds one, and `auto &&` over an lvalue collapses to an lvalue reference.
// The cv-qualifiers written beside `auto` are the declaration's own and go back
// on afterwards, and 7.1.6.4p7 makes every declarator of one declaration deduce
// the same type.

template<class T>
struct is_int
{
  static const int answer = 0;
};

template<>
struct is_int<int>
{
  static const int answer = 1;
};

template<class T>
struct names
{
  typedef T type;
  static const int size = sizeof(T);
};

int seven()
{
  return 7;
}

struct holder
{
  int value;

  int read() const
  {
    return value;
  }
};

static const int constant_five = 5;

int main()
{
  // 14.8.2.1p2: a place taking a value drops the argument's own qualifier.
  auto by_value = constant_five;
  by_value = 6;
  if (by_value != 6)
  {
    return 1;
  }
  if (is_int<names<decltype(by_value)>::type>::answer != 1)
  {
    return 2;
  }

  // 7.1.6.4p1: the qualifiers written beside `auto` are the declaration's.
  const auto still_five = constant_five;
  if (still_five != 5)
  {
    return 3;
  }

  // 14.8.2.1p2: a reference place keeps what the argument was qualified with,
  // and binds the object rather than a copy of it.
  int counter = 1;
  auto& named = counter;
  named = 4;
  if (counter != 4)
  {
    return 4;
  }
  const auto& watched = counter;
  if (watched != 4)
  {
    return 5;
  }

  // 14.8.2.1p3: an rvalue reference over the bare place collapses to an lvalue
  // reference where the initializer is an lvalue, and stays one where it is not.
  auto&& forwarded = counter;
  forwarded = 9;
  if (counter != 9)
  {
    return 6;
  }
  auto&& taken = seven();
  if (taken != 7)
  {
    return 7;
  }

  // 4.2 and 4.3: an array decays to a pointer and a function to a pointer to
  // itself, exactly as they would at a call.
  int numbers[3] = { 10, 20, 30 };
  auto walking = numbers;
  if (walking[2] != 30)
  {
    return 8;
  }
  if (names<decltype(walking)>::size != sizeof(int*))
  {
    return 9;
  }
  auto& whole = numbers;
  if (names<decltype(whole)>::size != 3 * sizeof(int))
  {
    return 10;
  }
  auto called = seven;
  if (called() != 7)
  {
    return 11;
  }

  // 8.3.1p1: the ptr-operators of the declarator derive from the place.
  auto* addressed = &counter;
  const auto* watching = &counter;
  if (*addressed != 9 || *watching != 9)
  {
    return 12;
  }

  // 7.1.6.4p7: two declarators of one declaration, each deducing `int`.
  auto first = 1, *second = &counter;
  if (first != 1 || *second != 9)
  {
    return 13;
  }

  // 8.5p16: the parenthesized form is the same deduction over the one
  // expression it holds.
  auto built(seven());
  if (built != 7)
  {
    return 14;
  }

  // A place deduced from what a member function handed back.
  holder object;
  object.value = 12;
  auto held = object.read();
  if (held != 12)
  {
    return 15;
  }
  auto& reached = object;
  if (reached.read() != 12)
  {
    return 16;
  }

  return 0;
}

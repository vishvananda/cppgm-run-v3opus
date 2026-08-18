// 14.6.4.1p1 and 14.6.4.2p1: a definition a template wrote is read again where
// an argument list arrives, which may be anywhere below it in the unit - and by
// then the namespaces its names are looked up in have gone on being declared
// into.  3.4.1's half of what such a reading finds is what stood where the
// definition was written; 3.4.2's half is answered at the instantiation context
// too.  So a body carries the bound it was written under, and each of the five
// kinds of definition below carries its own: the template's pattern, a body
// `template<>` wrote out for one argument list, a definition written outside
// the class, the type-id of an alias template, and the constant expression a
// fold reads.

int reached(long)
{
  return 1;
}

namespace lib
{

struct arg
{
};

} // namespace lib

template<class T>
int through(T value)
{
  // Read again at the call below, by which time `reached(int)` has been
  // declared - which is no candidate of this reading.
  return reached(value);
}

template<class T>
struct held
{
  int go(T value)
  {
    return reached(value);
  }

  int out_of_class(T value);

  // 11.3p1: the friend this body declares is declared by nothing else, so a
  // second reading of this same body has to find it.
  friend int peek(const held &)
  {
    return 10;
  }
};

template<class T>
int recursive(T count)
{
  return count == 0 ? 0 : recursive(count - 1) + 1;
}

constexpr int folded(long)
{
  return 100;
}

template<class T>
struct gate
{
  static const int value = folded(T(0));
};

// Everything below is declared after every pattern above.

int reached(int)
{
  return 2;
}

constexpr int folded(int)
{
  return 200;
}

// 3.4.2: a function declared into an associated namespace after the pattern is
// still one the call reaches, because that half of the clause is answered where
// the arguments arrive.
namespace lib
{

int reached(arg)
{
  return 1000;
}

} // namespace lib

// 14.5.1.3p1: this definition stands here, so what its own names reach is what
// stands here - which includes `reached(int)`.
template<class T>
int held<T>::out_of_class(T value)
{
  return reached(value);
}

// 14.7.3p1: and a body written out for one argument list is read where the
// program wrote it, which is below everything above.
template<>
int through<char>(char value)
{
  return reached(static_cast<int>(value)) * 3;
}

int main()
{
  int settled = 0;
  held<int> one;
  settled += through(1) == 1 ? 0 : 1;
  settled += one.go(1) == 1 ? 0 : 1;
  settled += peek(one) == 10 ? 0 : 1;
  settled += recursive(3) == 3 ? 0 : 1;
  settled += gate<int>::value == 100 ? 0 : 1;
  settled += through(lib::arg()) == 1000 ? 0 : 1;
  settled += one.out_of_class(1) == 2 ? 0 : 1;
  settled += through('a') == 6 ? 0 : 1;
  return settled;
}

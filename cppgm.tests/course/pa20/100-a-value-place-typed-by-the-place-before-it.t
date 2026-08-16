// VALIDATION: compile-pass
// 14.1p4: the type a non-type template parameter declares may name the places
// written before it, so the argument at that place is converted to the type
// those arguments make of it - at a function template's head as at a class
// template's, and through 14.6.2p1's dependent qualified name as well as
// through the place itself.

template<unsigned long Bits>
struct uint_for
{
  typedef unsigned int fast;
};

template<class T, T Value>
struct held
{
  static const T value = Value;
};

template<class T, T Value>
T from_place()
{
  return Value;
}

template<unsigned long Bits, typename uint_for<Bits>::fast Poly>
unsigned int from_qualified_place()
{
  return Poly;
}

template<unsigned long Bits, typename uint_for<Bits>::fast Poly>
struct kind
{
  static const unsigned int poly = Poly;
};

template<unsigned long Bits, typename uint_for<Bits>::fast Poly>
unsigned int through_a_class()
{
  kind<Bits, Poly> one;
  (void)&one;
  return kind<Bits, Poly>::poly;
}

template<class T, T First, T Second>
T of_two_places()
{
  return First + Second;
}

template<class T, T Value = 7>
T defaulted_place()
{
  return Value;
}

int main()
{
  return held<int, 5>::value == 5 &&
         from_place<unsigned int, 7>() == 7u &&
         from_qualified_place<32, 7>() == 7u &&
         through_a_class<32, 9>() == 9u &&
         of_two_places<int, 4, 5>() == 9 &&
         defaulted_place<unsigned int>() == 7u
    ? 0 : 1;
}

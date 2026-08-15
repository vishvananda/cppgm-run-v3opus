// VALIDATION: compile-pass
// 2.14.8p3: a set that declares no operator taking the value of a literal and
// none taking its digits may still declare a literal operator template, which
// is called with the characters the program wrote as its argument list.  And
// where one does take the value, the argument is the literal 2.14.2 made, so
// 5.2.2p4's conversion brings it to the parameter as it would any argument.

typedef decltype(sizeof(0)) size_type;

template<char... Chars>
struct first_of
{
  static const int value = 0;
};

template<char Head, char... Rest>
struct first_of<Head, Rest...>
{
  static const int value = Head;
};

template<char... Chars>
int operator ""_spelled()
{
  return sizeof...(Chars) * 1000 + first_of<Chars...>::value;
}

int operator ""_mixed(const char *, size_type)
{
  return 100;
}

template<char... Chars>
int operator ""_mixed()
{
  return sizeof...(Chars);
}

int operator ""_cooked(unsigned long long value)
{
  return static_cast<int>(value) + 7;
}

int operator ""_wide(long double value)
{
  return static_cast<int>(value);
}

int main()
{
  return 12_spelled == 2049 && 0xFF_spelled == 4048 && 1.5_spelled == 3049 &&
         12_spelled == 12_spelled && 1_spelled == 1049 &&
         3_mixed + "ab"_mixed == 101 && 35_cooked == 42 && 1.5_wide == 1
    ? 0 : 1;
}

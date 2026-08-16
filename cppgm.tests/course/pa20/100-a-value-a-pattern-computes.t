// VALIDATION: compile-pass
// 14.3.2p1 refuses a *type* written where a value belongs, and what it is asked
// of is the argument each element of a run comes to.  A pattern that is the
// pack's own name writes those elements at the places; one that computes
// something out of them writes 5.3.3p1's value instead - so a definition read
// before any argument list settles the run has to tell the two apart by the
// pattern rather than by the pack it names.

template<unsigned long...>
struct sizes
{
};

template<class S>
struct first_size;

template<unsigned long Head, unsigned long... Rest>
struct first_size<sizes<Head, Rest...> >
{
  static const unsigned long value = Head;
};

template<class... T>
struct widths
{
  typedef sizes<sizeof(T)...> made;
};

template<int... N>
struct shifted
{
  typedef sizes<(unsigned long)(N + 1)...> made;
};

int main()
{
  return first_size<widths<int, char>::made>::value == 4 &&
         first_size<shifted<7, 8>::made>::value == 8
    ? 0 : 1;
}

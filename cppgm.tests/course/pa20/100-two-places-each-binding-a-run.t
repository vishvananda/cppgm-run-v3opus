// VALIDATION: compile-pass
// 14.1p11 and 14.4p1: a function template's head may declare more than one
// place that binds a run, because 14.8.2 deduces the places one at a time.
// One flat argument list cannot then say where the first run ended - the same
// three types are two different specializations depending on the split - so a
// run that is not the last place stands as one entry of the list.

template<class...>
struct types
{
};

typedef unsigned long size_type;

template<size_type...>
struct indexes
{
};

int add(int left, int right)
{
  return left + right;
}

template<class... Args, class... Rest>
int split(types<Args...>, Rest...)
{
  return (int)(sizeof...(Args) * 10 + sizeof...(Rest));
}

template<class... Args, size_type... Which>
int both(types<Args...>, indexes<Which...>)
{
  return (int)(sizeof...(Args) * 10 + sizeof...(Which));
}

template<class One, class... Rest>
int one_of(Rest...)
{
  return (int)(sizeof(One) * 100 + sizeof...(Rest));
}

// 14.5.3p5: a pack named inside the pattern of an inner expansion is expanded
// by that one, so it says nothing about how long the outer run is.
template<class... Args, class... Rest>
int nested(types<Args...>, Rest... rest)
{
  return add(one_of<Args>(rest...)...);
}

template<class... Args, class Last>
int before(types<Args...>, Last)
{
  return (int)sizeof...(Args);
}

int main()
{
  return split(types<char, short>(), 4) == 21 &&
         split(types<char>(), (short)1, 4L) == 12 &&
         both(types<char, int>(), indexes<1, 2, 3>()) == 23 &&
         split(types<>()) == 0 &&
         before(types<char, short, int>(), 4) == 3 &&
         nested(types<char, short>(), 4) == 302
    ? 0 : 1;
}

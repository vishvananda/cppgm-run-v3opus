// 14.6.2p2 with 14.3.2p5: a value an argument list has yet to settle is the
// member the prefix names once it has one - the specialization the arguments
// chose, an enumerator, a value converted to the place it fills - and a class
// declaring no such member is 14.8.2p8's substitution failure.

template<class T>
struct trait
{
  static const int value = 7;
};

template<>
struct trait<char>
{
  static const int value = 4;
};

template<class T>
struct tagged
{
  enum
  {
    value = 2
  };
};

template<int N>
struct held
{
  static const int carried = N;
};

template<char N>
struct narrow
{
  static const int carried = (int)(unsigned char)N;
};

// The specialization the arguments chose, not the primary.
template<class T>
int chosen()
{
  return held<trait<T>::value>::carried;
}

// An enumerator reached through the same door as a static data member.
template<class T>
int counted()
{
  return held<tagged<T>::value>::carried;
}

// 14.3.2p5's conversion to the place, which is the char the value truncates to.
template<class T>
int truncated()
{
  return narrow<(char)(trait<T>::value + 296)>::carried;
}

// One naming written at two places of different types.
template<class T>
int twice()
{
  return held<trait<T>::value>::carried + narrow<(char)trait<T>::value>::carried;
}

// 14.8.2p8: a class with no such member drops the candidate rather than
// refusing the program.
template<int N>
struct wrapper
{
  typedef int type;
};

template<class T>
typename wrapper<T::value>::type reached(T *)
{
  return 1;
}

int reached(...)
{
  return 2;
}

struct carries
{
  static const int value = 5;
};

struct carries_nothing
{
};

int main()
{
  carries one;
  carries_nothing none;
  return chosen<char>() + counted<int>() + truncated<char>() + twice<char>() +
      reached(&one) + reached(&none) - 4 - 2 - 44 - 8 - 3;
}

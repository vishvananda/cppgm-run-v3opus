// VALIDATION: compile-pass
// N3485 focus: 14.5.2 [temp.mem], 14.5.1.3 [temp.mem.class],
// 14.6.1 [temp.local]
// 14.5.2p3 writes one template-parameter-clause per class the member is nested
// in, and the declarator-id says which of them each clause parameterises: every
// component the region has already bound names a class the reading settled, and
// the first it has not is the template the definition is a member of.  So the
// second head declares the places of `inner` and the third those of the member
// of `inner` it defines.

template<class T>
struct outer
{
  template<class U>
  struct inner;

  template<class U>
  struct pair_of;
};

template<class T>
template<class U>
struct outer<T>::inner
{
  static const int width = sizeof(T) + sizeof(U);
  int scaled(int by);
  template<class V>
  struct deeper;
};

template<class T>
template<class U>
struct outer<T>::pair_of
{
  U held;
};

template<class T>
template<class U>
int outer<T>::inner<U>::scaled(int by)
{
  return by * static_cast<int>(sizeof(U));
}

template<class T>
template<class U>
template<class V>
struct outer<T>::inner<U>::deeper
{
  static const int total = sizeof(T) + sizeof(U) + sizeof(V);
};

int main()
{
  outer<int>::inner<char> reached;
  outer<int>::pair_of<long> beside;
  beside.held = 4;
  return outer<int>::inner<char>::width == 5 &&
                 outer<int>::inner<char>::deeper<long>::total == 13 &&
                 reached.scaled(6) == 6 && beside.held == 4
             ? 0
             : 1;
}

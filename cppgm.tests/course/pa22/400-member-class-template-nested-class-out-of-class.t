// VALIDATION: compile-pass
// N3485 focus: 14.5.2 [temp.mem], 14.5.1.3 [temp.mem.func], 3.4.3 [basic.lookup.qual]
// 14.5.2p3 says which head parameterises which class by how far the region has
// already bound, and a class nested inside a *member* class template is the
// tier where the declarator-id's own middle component names this head's places:
// `adaptor<T>::range<M>::iterator` is a class the region can reach only once
// `M` stands for what the second head declared.

template<class T>
struct adaptor
{
  template<int M>
  struct range;
};

template<class T>
template<int M>
struct adaptor<T>::range
{
  struct iterator;

  iterator begin() const;

  T value;
};

template<class T>
template<int M>
struct adaptor<T>::range<M>::iterator
{
  T value;

  int mark() const;
};

template<class T>
template<int M>
int adaptor<T>::range<M>::iterator::mark() const
{
  return M + int(sizeof(T));
}

template<class T>
template<int M>
typename adaptor<T>::template range<M>::iterator adaptor<T>::range<M>::begin() const
{
  iterator made;
  made.value = value;
  return made;
}

template<class T>
struct outer
{
  template<class U>
  struct inner
  {
    struct leaf;
  };
};

template<class T>
template<class U>
struct outer<T>::inner<U>::leaf
{
  U slot;
};

int main()
{
  adaptor<int>::range<7> held;
  held.value = 5;
  outer<long>::inner<char>::leaf tip;
  tip.slot = 'a';
  return held.begin().mark() == 11 && held.begin().value == 5 &&
         tip.slot == 'a' ? 0 : 1;
}

// VALIDATION: compile-pass
// N3485 focus: 14.5.2 [temp.mem], 14.5.6.1 [temp.over.link]
// Several overloads of one member-template name, each defined outside the
// class template.  Two declarations of one function template write types that
// differ, because each head declared places of its own, so 14.5.6.1's
// equivalence is what matches each definition to the declaration it defines.

template<class T>
struct dispatch
{
  struct first { };
  struct second { };

  template<class I>
  int pick(first*, I x);

  template<class I>
  int pick(second*, I x);
};

template<class T>
template<class I>
int dispatch<T>::pick(first*, I x)
{
  return int(x);
}

template<class T>
template<class I>
int dispatch<T>::pick(second*, I x)
{
  return int(x) + 10;
}

int main()
{
  dispatch<int> d;
  return d.pick((dispatch<int>::first*)0, 1) == 1 &&
         d.pick((dispatch<int>::second*)0, 1) == 11 ? 0 : 1;
}

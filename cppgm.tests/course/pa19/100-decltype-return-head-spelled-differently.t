// N3485 focus: 14.4 [temp.type] and 14.5.6.1 [temp.over.link] each declaration
// of one template spells the places its head declared as it likes, and a
// template-parameter is equivalent to the one at the same position - so a
// decltype-specifier written over that parameter names one type in both
// declarations however each of them spelled it.

template<class T>
auto make(T seed) -> decltype(T() + seed);

template<class U>
auto make(U seed) -> decltype(U() + seed)
{
  return U() + seed;
}

int main()
{
  return make(42) - 42;
}

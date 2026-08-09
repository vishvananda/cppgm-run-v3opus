// N3485 focus: 3.3.7 [basic.scope.proto] a place a parameter-clause declares
// after a decltype-specifier is one that specifier could not name - its
// potential scope begins at its own declarator-id - so 14.7.1's second reading
// of the specifier rebuilds only what stood when it was written, and a place
// whose own type is a decltype-specifier does not stand for itself.

template<class T>
auto differ(T left, decltype(left - left) right) -> decltype(left - right)
{
  return left - right;
}

int main()
{
  return differ(50, 8) - 42;
}

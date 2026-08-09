// N3485 focus: 14.7.2 [temp.explicit], 14.8.2.2 [temp.deduct.funcaddr] an
// explicit instantiation that writes no template-argument-list names the
// specialization its own declared type deduces.

template<class T>
T doubled(T value)
{
  return value + value;
}

template long doubled(long);

int main()
{
  return 0;
}

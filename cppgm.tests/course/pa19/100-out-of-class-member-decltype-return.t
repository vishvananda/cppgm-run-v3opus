// N3485 focus: 14.5.1.3 [temp.mem.func] an out-of-class member definition is
// read against the class its declaration was read against, so a
// decltype-specifier written in both names one type and the two declare one
// member rather than two that differ in their return type.

template<class T>
struct holder
{
  T held;
  auto doubled() -> decltype(held + held);
};

template<class T>
auto holder<T>::doubled() -> decltype(held + held)
{
  return held + held;
}

int main()
{
  holder<int> box;
  box.held = 21;
  return box.doubled() - 42;
}

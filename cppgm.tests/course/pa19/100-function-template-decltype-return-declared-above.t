// N3485 focus: 14.4 [temp.type] two declarations of one function template whose
// return type is a decltype-specifier over its own places declare one template,
// so the definition written below a use is the definition that use asks for and
// the object file writes it.

template<class T>
auto added(T left, T right) -> decltype(left + right);

int main()
{
  return added(20, 22) - 42;
}

template<class T>
auto added(T left, T right) -> decltype(left + right)
{
  return left + right;
}

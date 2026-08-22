// 3.10p1 with 4.2p1: there is no prvalue of array type, so an argument a
// constant expression arrived at a value for is still the object it names where
// that object is an array - every reader that takes a value out of one converts
// it to the address of its first element first.  13.3.3.1.4p1's reference
// parameter binds that object, and 14.8.2.1p2 deduces against the array itself
// rather than against the pointer 4.2p1 would have made of it.
constexpr int values[2] = { 8, 9 };

constexpr int first(int const (&given)[2]) { return given[0]; }

template<class T>
constexpr int measured(T const & given) { return sizeof(given); }

template<class T, int N>
constexpr int counted(T const (&given)[N]) { return N; }

int main() {
  static_assert(first(values) == 8, "the reference binds the array");
  static_assert(measured(values) == 8, "T deduces the array and not a pointer");
  static_assert(counted(values) == 2, "the bound is deduced from the object");
  return first(values) - 8;
}

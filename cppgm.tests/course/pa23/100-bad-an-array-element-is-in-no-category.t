// VALIDATION: compile-fail
// 14.3.2p3: the address of an array element is in none of the categories
// 14.3.2p1 lists, however much the array's own name reaches such a place.

int digits[4] = { 0, 1, 2, 3 };

template<int *Pointer>
struct through_pointer
{
  static int read() { return *Pointer; }
};

int main()
{
  return through_pointer<&digits[2]>::read();
}

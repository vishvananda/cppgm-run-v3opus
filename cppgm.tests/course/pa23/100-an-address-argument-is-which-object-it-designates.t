// VALIDATION: compile-pass
// 14.3.2p1: an argument at a place of pointer or lvalue-reference type is
// 5.19p2's address constant, so what tells two specializations apart is which
// object each argument designates and no number at all.

int left = 3;
int right = 5;

template<int *Pointer>
struct through_pointer
{
  static int read() { return *Pointer; }
  static void write(int value) { *Pointer = value; }
};

template<int &Reference>
struct through_reference
{
  static int read() { return Reference; }
};

int main()
{
  through_pointer<&left>::write(7);
  return through_pointer<&left>::read() == 7 &&
         through_pointer<&right>::read() == 5 &&
         through_reference<left>::read() == 7 &&
         through_reference<right>::read() == 5 ? 0 : 1;
}

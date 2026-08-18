// VALIDATION: compile-fail
// 14.3.2p1 asks for an object with external or internal linkage, and 3.5p8
// gives a name a block declares none - which a function-local `static` has as
// much as an automatic object does.

template<int *Pointer>
struct through_pointer
{
  static int read() { return *Pointer; }
};

int main()
{
  static int local_value = 4;
  return through_pointer<&local_value>::read();
}

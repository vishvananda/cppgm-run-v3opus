// N3485 5.3.5p3 [expr.delete]: a `delete` of a pointer to a base with a
// virtual destructor runs the destructor of the object's own class and gives
// the storage back through the deallocation function 5.3.5p9 chose in *that*
// class, which only the class's own deleting entry knows.

struct Base
{
  virtual ~Base() {}
};

struct Derived : Base
{
  static void operator delete(void *) noexcept;
};

int main()
{
  Base *object = new Derived();
  delete object;
  return 0;
}

// N3485 11.2 [class.access.base] and 10 [class.derived] over the type-info
// record of a polymorphic class: a base that is neither public nor at the
// start of the object is described by the ABI's multiple-inheritance record,
// which writes the byte the base begins at and its access.

struct Held
{
  int value;

  Held() : value(3) {}
  int get() const { return value; }
};

struct Wrapper : private Held
{
  virtual int read() const { return get(); }
};

int main()
{
  Wrapper object;
  return object.read() - 3;
}

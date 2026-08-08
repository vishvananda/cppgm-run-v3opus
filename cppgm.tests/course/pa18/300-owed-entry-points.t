// 12.1, 12.4 and the ABI: which entry points the object file owes for the
// special members of the classes a polymorphic object is built out of.
//
// The classes this unit's own source wrote definitions for are the one the
// vpointer starts at and the non-polymorphic classes under it: a class whose
// base already dispatches adds none, because every unit that can create a
// complete object of it can define its members for itself.  The deleting entry
// is a fact of the definition rather than of the table - 10.4p2's pure slots
// name the runtime's own function and ask for nothing - so a destructor no
// other unit may define owes it wherever this unit holds the body.
struct plain
{
  plain() {}
  ~plain() {}
  int held;
};

struct root : plain
{
  root() {}
  virtual int value() const { return 1; }
  virtual ~root() {}
};

struct between : root
{
  between() {}
  ~between() {}
};

struct leaf : between
{
  int value() const { return 2; }
};

struct pure_end
{
  virtual ~pure_end() = 0;
  virtual int value() const { return 4; }
};

pure_end::~pure_end() {}

struct ends_it : pure_end
{
  int value() const { return 8; }
};

int main()
{
  leaf of_a_chain;
  ends_it of_a_pure_destructor;
  root * polymorphic = &of_a_chain;
  pure_end * ended = &of_a_pure_destructor;
  return polymorphic->value() == 2 && ended->value() == 8 ? 0 : 1;
}

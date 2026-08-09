// 9.3p2, 3.2p4 and 2.2p1: a member function this unit's own source defined
// outside its class is a definition the program wrote here, so the object file
// holds it however little of the unit names it.  A body written in the class -
// and one an included file wrote - is a definition every unit that needs one
// writes for itself, and 3.2p3 leaves that to the use that asks.
struct Held {
  int value;

  Held(int v) : value(v) {}
  int doubled();
  static int origin();
  operator int();
};

inline int Held::doubled() { return value + value; }

inline int Held::origin() { return 0; }

inline Held::operator int() { return value; }

int main()
{
  Held one(3);
  return one.value - 3;
}

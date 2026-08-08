// N3485 focus: 12.8p28 [class.copy], 8.3.6p9 [dcl.fct.default]
// The step 12.8p28's transfer of an array member is, is written once and run for
// each element - so an object that step creates is that element's own.  Here the
// element's own assignment operator takes its parameter by value, which makes
// every element's step build an argument object of its own: reading the object
// the first element built for the second is one copy the program never wrote.
struct Value
{
  Value() : state(1) {}
  Value(const Value & other) : state(other.state + 1) {}
  Value & operator=(Value other) { state = other.state + 3; return *this; }
  ~Value() {}
  int state;
};

struct Owner
{
  Value values[3];
};

Owner target;
Owner source;

int main()
{
  target = source;
  return target.values[0].state == 5 && target.values[1].state == 5 &&
         target.values[2].state == 5 ? 0 : 1;
}

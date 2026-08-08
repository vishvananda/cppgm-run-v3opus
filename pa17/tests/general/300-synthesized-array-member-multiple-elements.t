// N3485 focus: 12.8p15, 12.8p28 [class.copy]
// The four value-transfer members the standard defines carry an array member
// one element at a time, and what each step reads is the element of the source
// at the same index - so a member of more than one element is not the first
// element carried over and over.
struct Value
{
  Value() : state(1) {}
  Value(const Value & other) : state(other.state + 1) {}
  Value(Value && other) : state(other.state + 2) { other.state = 0; }
  Value & operator=(const Value & other) { state = other.state + 3; return *this; }
  Value & operator=(Value && other) { state = other.state + 4; other.state = 0; return *this; }
  int state;
};

struct Owner
{
  Value values[3];
};

int main()
{
  Owner source;
  Owner copied(source);
  Owner moved(static_cast<Owner &&>(source));
  Owner copy_assigned;
  copy_assigned = copied;
  Owner move_assigned;
  move_assigned = static_cast<Owner &&>(copied);
  return source.values[2].state == 0 && moved.values[2].state == 3 &&
         copied.values[2].state == 0 && copy_assigned.values[2].state == 5 &&
         move_assigned.values[2].state == 6 ? 0 : 1;
}

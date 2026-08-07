// VALIDATION: compile-pass
// 8.5.1p2 copy-initializes a member of class type from the clause that reached
// it, and 8.5.1p7 value-initializes one no clause reached - which for a class
// with a default constructor is a call of that constructor.

struct Counted {
  int value;
  Counted() : value(9) {}
};

struct Converted {
  int value;
  Converted(int v) : value(v + 1) {}
};

struct Holder {
  int head;
  Converted middle;
  Counted tail;
};

int main()
{
  Holder holder{4, 5};
  return holder.head == 4 && holder.middle.value == 6 &&
    holder.tail.value == 9 ? 0 : 1;
}

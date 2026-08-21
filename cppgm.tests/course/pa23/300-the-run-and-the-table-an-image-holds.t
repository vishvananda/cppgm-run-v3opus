// VALIDATION: compile-pass
// N3485 focus: 3.6.2 [basic.start.init], 12.6 [class.init], 10.3 [class.virtual]
//
// The two subobject walks 3.6.2p2's constant initialization reaches past one
// object of one class.
//
// 12.6p1 makes an array of class type one construction per element, and the
// elements of one whose constructor 12.1p5 makes constexpr are all values the
// analysis holds - so the image is the list it arrived at, laid out against
// 8.3.4p6's stride, and no element is built before the program runs.
//
// 12.1p11 and 10.3p1 are the other: a constructor of a class that dispatches
// writes the pointer to that class's table before it runs a mem-initializer, so
// the first bytes of an object the image holds carry the table's address and the
// members stand after it.  The pointer is the *complete* object's - each base's
// own constructor writes its table over the same bytes while it runs, and the
// last one to run is the class the declaration named.

struct element
{
  int first = 1;
  int second = 2;
};

element a_run[3];
element from_an_empty_list[2] = {};

struct dispatches
{
  virtual int which() const
  {
    return 1;
  }

  int held = 2;
};

struct derives : dispatches
{
  int which() const
  {
    return 3;
  }

  int also = 4;
};

dispatches the_base_table;
derives the_derived_table;

int main()
{
  const dispatches * reached = &the_derived_table;
  return a_run[2].second == 2 && from_an_empty_list[1].first == 1 &&
         the_base_table.which() == 1 && the_base_table.held == 2 &&
         reached->which() == 3 && the_derived_table.also == 4
    ? 0
    : 1;
}

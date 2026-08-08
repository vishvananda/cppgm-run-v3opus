// 3.6.2p2 and 12.1p11: the definition the standard gives the default
// constructor of a class that holds nothing but what 10.3p1 gave it writes one
// thing - the vpointer - so the image of a namespace-scope object of that class
// holds it and the program runs nothing before it.  An object with a byte the
// vpointer does not cover, an array of them, and a class whose own constructor
// the program wrote are each initialized before the program runs instead.
struct root
{
  virtual int value() const { return 1; }
  virtual ~root() {}
};

struct derived : root
{
  int value() const { return 2; }
};

struct with_member : root
{
  int value() const { return 3; }
  int held;
};

struct with_constructor : root
{
  with_constructor() {}
  int value() const { return 4; }
};

root image_holds_the_vpointer;
derived the_derived_table;
with_member built_before_the_program;
with_constructor built_by_what_the_program_wrote;
root a_run_of_them[2];

int main()
{
  root * of_root = &image_holds_the_vpointer;
  root * of_derived = &the_derived_table;
  root * of_member = &built_before_the_program;
  root * of_written = &built_by_what_the_program_wrote;
  root * of_element = &a_run_of_them[1];
  return of_root->value() == 1 && of_derived->value() == 2 &&
         of_member->value() == 3 && of_written->value() == 4 &&
         of_element->value() == 1
    ? 0
    : 1;
}

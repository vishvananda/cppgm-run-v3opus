// 12.2p1 and 15.4p14: a virtual call that hands back an object of class type in
// registers, made where an object's lifetime is already open.  12.2p1 gives the
// value a temporary of its own and the copy into it stands in the step the call
// belongs to, so the call needs no second place to stand - `load` and `store`
// have no lowered type to spell an object with.  The array below asks the other
// half: an element constructor the standard wrote throws what the members it
// invokes throw, which for a class of scalars is nothing at all, so its run
// opens no handler.
struct small_result
{
  int held;
};

struct source
{
  virtual small_result made() const
  {
    small_result out;
    out.held = 1;
    return out;
  }
  virtual ~source() {}
};

struct other_source : source
{
  small_result made() const
  {
    small_result out;
    out.held = 2;
    return out;
  }
};

struct element
{
  virtual int value() const { return 4; }
  virtual ~element() {}
  int held;
};

int through_a_pointer()
{
  other_source live;
  source * polymorphic = &live;
  small_result got = polymorphic->made();
  return got.held;
}

int over_a_run()
{
  element * run = new element[3];
  int total = run[0].value() + run[2].value();
  delete [] run;
  return total;
}

int main()
{
  return through_a_pointer() == 2 && over_a_run() == 8 ? 0 : 1;
}

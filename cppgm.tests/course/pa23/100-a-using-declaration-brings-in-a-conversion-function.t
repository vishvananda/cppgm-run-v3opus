// VALIDATION: run-pass
// 12.3.2p1 with 7.3.3p1: a conversion function is named by the type it converts
// to, and every region that bound one bound it under the one spelling that type
// has - so the target of a using-declaration, which reaches the reading as the
// flattening of the terminals that were written, has to be read as a type-id
// before the lookup, or `using base::operator int` asks for a name no class
// declared.  13.3.1p4's last sentence then makes what it published a member of
// *this* class for the object it is called on, which is what lets a private
// base's conversion be reached through the class that published it - and the
// step to the base subobject is the one 11.2p5 leaves the base-specifier's own
// access unasked about, so the question is asked before that step and not after
// it.

typedef int counted;

struct base
{
  counted value;
  operator counted() const { return value; }
  operator bool() const { return value != 0; }
  operator int const *() const { return &value; }
};

class holder : private base
{
public:
  holder() { value = 7; }
  using base::operator counted;
  using base::operator bool;
  using base::operator int const *;
};

int measure(int v)
{
  return v;
}

int main()
{
  holder h;
  return static_cast<int>(h) == 7 && measure(h) == 7 && (h ? 1 : 0) == 1 &&
      *static_cast<int const *>(h) == 7
    ? 0 : 1;
}

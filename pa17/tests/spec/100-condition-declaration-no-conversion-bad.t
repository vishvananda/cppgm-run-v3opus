// VALIDATION: compile-fail
// N3485 focus: 6.4p4 [stmt.select] contextual conversion of a condition
// Expected: a condition that declares an object of class type with no
// conversion to bool is refused, exactly as the expression spelling of the
// same condition is - the object's own storage is nothing to branch on.

struct Opaque
{
  Opaque();
  ~Opaque();
};

Opaque make();

int main()
{
  if (Opaque o = make())
  {
    return 1;
  }
  return 0;
}

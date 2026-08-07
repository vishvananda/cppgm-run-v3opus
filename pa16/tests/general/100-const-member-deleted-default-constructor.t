// VALIDATION: compile-fail
// 12.1p5: the default constructor the standard would give this class has
// nothing to initialize a member of const-qualified type with, so it is deleted
// and 8.5p6's default-initialization of an object of the class names it.

struct Frozen {
  const int locked;
  int loose;
};

int main()
{
  Frozen frozen;
  return frozen.loose;
}

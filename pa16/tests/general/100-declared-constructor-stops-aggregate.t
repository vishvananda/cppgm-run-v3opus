// VALIDATION: compile-pass
// 8.4.2p4 and 8.5.1p1: a constructor the class only declares is user-provided
// all the same, so the class is no aggregate and a braced initializer calls the
// constructor another unit defines rather than storing the members.

struct Late {
  int value;

  Late();
};

int main()
{
  Late late{};
  return late.value;
}

// 12.1: a complete object and a base class subobject each asked this unit for
// the constructor of a class it holds no body for, so the calls name both of
// the ABI's entry points - and a name a call writes is one this unit owes the
// program a declaration of, whether or not it holds the definition.
struct entry_base {
  int held;
  entry_base(int given);
};

struct entry_derived : entry_base {
  entry_derived(int given) : entry_base(given) {}
};

entry_base whole_object(1);
entry_derived through_base(2);

int main() { return 0; }

// 11p1 and 12.9p1: which of a base's constructors are inherited is a question
// about the complete class, so a constructor the class declares after the
// using-declaration is one of its own - with the access the access-specifier
// standing over it gives it rather than the one over the using-declaration.
struct later_declaration_base {
  int held;
  later_declaration_base(int given) : held(given) {}
  later_declaration_base(double given) : held((int)given + 1) {}
};

struct later_declaration_derived : later_declaration_base {
 private:
  using later_declaration_base::later_declaration_base;

 public:
  later_declaration_derived(int given) : later_declaration_base(given + 2) {}
};

int main() {
  later_declaration_derived derived(1);
  return derived.held - 3;
}

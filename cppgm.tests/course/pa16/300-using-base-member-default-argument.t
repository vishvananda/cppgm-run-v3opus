// 8.3.6p4 and 7.3.3p1: a default-argument belongs to the declaration that wrote
// it, which for a member a using-declaration brought into a class is the base's,
// so a call that omits the argument reads it from there rather than from a copy
// the using-declaration would have had to make.
struct default_member_base {
  int given(int written = 5) { return written; }
  static int taken(int written = 6) { return written; }
};

struct default_member_derived : private default_member_base {
 public:
  using default_member_base::given;
  using default_member_base::taken;
};

int main() {
  default_member_derived derived;
  return derived.given() + default_member_derived::taken() - 11;
}

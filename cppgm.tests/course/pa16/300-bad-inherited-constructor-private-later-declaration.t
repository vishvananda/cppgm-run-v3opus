// 11p1: the constructor this class declares itself stands under the
// access-specifier written over it, which a using-declaration standing earlier
// under another one does not change.
struct private_later_base {
  int held;
  private_later_base(int given) : held(given) {}
};

struct private_later_derived : private_later_base {
 public:
  using private_later_base::private_later_base;

 private:
  private_later_derived(int given) : private_later_base(given + 1) {}
};

int main() {
  private_later_derived derived(1);
  return derived.held - 2;
}

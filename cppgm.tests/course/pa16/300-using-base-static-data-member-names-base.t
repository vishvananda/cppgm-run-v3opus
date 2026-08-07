// 7.3.3p1: a using-declaration makes the class declare what the base declared,
// and the object that declaration names is the base's - one object with one
// name in the object file, not a second one of the derived class.
struct base_static_holder {
  static int value;
};

int base_static_holder::value = 1;

class derived_static_holder : private base_static_holder {
 public:
  using base_static_holder::value;
};

int main() { return derived_static_holder::value - 1; }

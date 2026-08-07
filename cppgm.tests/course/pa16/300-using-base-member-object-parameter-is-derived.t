// 13.3.3.1p4: a member function a using-declaration brought into a derived
// class has that class as the type of its implicit object parameter, so on a
// non-const object it beats the derived class's own const-qualified member.
struct base_object_parameter {
  int f() { return 1; }
};

struct derived_object_parameter : base_object_parameter {
  int f() const { return 2; }
  using base_object_parameter::f;
};

int main() {
  derived_object_parameter d;
  const derived_object_parameter &c = d;
  return d.f() + c.f() - 3;
}

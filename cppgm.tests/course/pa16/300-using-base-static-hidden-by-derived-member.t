// 7.3.3p14: what the class declared itself hides what the base declared with
// the same name and parameter-type-list.  9.3.1p3's object parameter is one
// this milestone put in a member function's type and no declarator wrote, so it
// is no part of that list - which is why a non-static declaration of the class
// hides a static one brought in from the base, and 9.4.1p2 does not have to let
// the two overload.
struct static_list_base {
  static int shared(int given) { return given; }
  static int only_here(int given) { return given + 1; }
};

struct static_list_derived : static_list_base {
  using static_list_base::shared;
  using static_list_base::only_here;
  int shared(int given) { return given + 10; }
  static int only_here(int given) { return given + 20; }
};

int main() {
  static_list_derived derived;
  return derived.shared(1) + static_list_derived::only_here(2) - 33;
}

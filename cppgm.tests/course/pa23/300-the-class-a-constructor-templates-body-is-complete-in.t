// The same clause where the class is a template and the member is one of 12's
// entry points: a constructor template and a conversion function template are
// function bodies written in a class body like any other.
template<class U>
struct wrapper {
  int held;

  template<class T>
  wrapper(T given) { held = int(given) + step; }

  template<class T>
  operator T() const { return T(held + limit::extra); }

  static const int step = 3;
  struct limit { static const int extra = 4; };
};

int main() {
  wrapper<int> made(0);
  return int(made) - 7;
}

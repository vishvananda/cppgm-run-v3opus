// 9.2p2: a class is regarded as complete within the function bodies of its own
// member specification, so a member function body reaches every member the
// class declares - including the ones written below it.  The reading is
// therefore made at the `}` that completes the class and not where the member
// specification meets the `{`: `pop<long>(value)` written above
// `template<class U> bool pop(U &)` is 14.2's template-id there and 5.9's two
// comparisons where the body stands, and the second reading is not a
// translation unit at all.
struct holder {
  bool pop(long& value) { return pop<long>(value); }

  template<class U, class Enabler = void>
  bool pop(U&) { return true; }

  int span() { width measured = 0; return measured; }

  typedef int width;

  int reach() { return depth; }

  static const int depth = 0;
};

int main() {
  holder held;
  long value = 0;
  return (held.pop(value) ? 0 : 1) + held.span() + held.reach();
}

// 9.2p2 and 14.6p8: a member template's body is a complete-class context, so
// the names it writes are looked up where the class-specifier closes and not
// where the member-declarator stands.  A declarator is not one of the clause's
// four contexts, so `count_type` is written inside the body and not before it.
struct holder {
  template<class T>
  static int settled() { return kept + tag_value + nested::held + named(); }

  template<class T>
  int carried(T given) const { count_type held = kept; return held + int(given); }

  static const int kept = 1;
  enum { tag_value = 2 };
  struct nested { static const int held = 4; };
  static int named() { return 8; }
  typedef int count_type;
};

int main() {
  holder object;
  return holder::settled<int>() + object.carried<int>(0) - 16;
}

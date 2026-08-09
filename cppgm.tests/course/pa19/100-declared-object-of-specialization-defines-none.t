// 3.9p5 and 3.1p2: the declared type of an object shall be complete where the
// object is *defined*, so an `extern` declaration with no initializer and
// 9.4.2p2's declaration a class writes of its static data member each name
// 14.7.1p1's specialization and ask it for nothing.
template<class T> struct holder {
  static const int factor = T::factor;
  int base;
};

struct box;
extern holder<box> never_defined;
struct wrap { static holder<box> member_declaration; };
struct box { static const int factor = 3; };

int main() {
  holder<box> a;
  a.base = 13;
  return a.base * holder<box>::factor == 39 ? 0 : 1;
}

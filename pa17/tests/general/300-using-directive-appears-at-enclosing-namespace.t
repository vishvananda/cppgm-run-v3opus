namespace outer { int i = 1; }
namespace holder {
  int i = 2;
  using namespace outer;
  int hidden() { return i; }
}
namespace deep {
  int i = 3;
  namespace inner {
    using namespace deep;
    int reached() { return i; }
  }
}
int main() { return holder::hidden() * 10 + deep::inner::reached(); }

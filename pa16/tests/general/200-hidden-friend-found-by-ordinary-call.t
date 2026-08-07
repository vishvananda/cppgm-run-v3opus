struct YC {
  int held;
  friend int reach(const YC& c) { return c.held; }
};

namespace nn {
struct YD { int held; };
int reach(const nn::YD& d) { return d.held; }
}

YC c = {4};
nn::YD d = {4};

int main() { return reach(c) - reach(d); }

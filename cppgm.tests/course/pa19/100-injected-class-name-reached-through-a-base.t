// 9p2 and 14.6.1p1: the injected-class-name is a member of the class it names,
// so a class derived from a specialization reaches the template-name as a
// type-name however far down the chain the derivation stands.
template<class T>
struct holder {
  T held;
};

struct direct : holder<int> {
  holder * self() { holder * reached = this; return reached; }
};

struct indirect : direct {
  holder * again() { holder * reached = this; return reached; }
};

int main()
{
  indirect built;
  built.held = 5;
  return built.self()->held == 5 && built.again()->held == 5 ? 0 : 1;
}

struct YBase {
  int held;
  int reach() { return 1; }
  int only() { return 10; }
};

struct YDerived : YBase {
  int reach() { return 2; }
};

YDerived d;

int main() {
  YDerived* p = &d;
  return d.YBase::reach() + d.YDerived::reach() + d.YBase::only() +
         p->YBase::reach() - 14;
}

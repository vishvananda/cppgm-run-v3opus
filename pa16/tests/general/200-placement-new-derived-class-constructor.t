typedef unsigned long ulong;

void* operator new(ulong, void* p) noexcept;

struct B { int b; };

struct D : B {
  int n;
  D() { n = 1; }
  D(int v) { n = v; }
};

char buf[32];

int main() {
  D* p = ::new((void*)buf) D(5);
  D* q = ::new((void*)buf) D();
  return p->n - q->n - 4;
}

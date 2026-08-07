typedef unsigned long ulong;

struct T {
  int n;
  static void* operator new(ulong, void* p) noexcept { return p; }
  T(int v) { n = v; }
};

void* operator new(ulong, void* p) noexcept { return p; }

char buf[16];

int main() {
  T* a = new((void*)buf) T(3);
  T* b = ::new((void*)buf) T(4);
  return a->n + b->n - 7;
}

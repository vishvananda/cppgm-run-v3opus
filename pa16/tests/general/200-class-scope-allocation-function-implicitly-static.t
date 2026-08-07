typedef unsigned long ulong;

void* operator new(ulong, void* p) noexcept { return p; }

struct Held {
  int n;
  void* operator new(ulong, void* p) noexcept { return p; }
  Held(int v) { n = v; }
};

struct Base {
  void* operator new(ulong, void* p) noexcept { return p; }
};

struct Derived : Base {
  int n;
  Derived(int v) { n = v; }
};

struct Apart {
  int n;
  void* operator new(ulong, void*) noexcept;
  Apart(int v) { n = v; }
};

void* Apart::operator new(ulong, void* p) noexcept { return p; }

char buf[32];

int main() {
  Held* a = new((void*)buf) Held(1);
  Derived* b = new((void*)buf) Derived(2);
  Apart* c = new((void*)buf) Apart(3);
  return a->n + b->n + c->n - 6;
}

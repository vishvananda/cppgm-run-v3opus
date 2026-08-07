typedef unsigned long ulong;

void* operator new(ulong, void* p) noexcept { return p; }

char buf[32];

int main() {
  int* a = ::new((void*)buf) int;
  int* b = ::new((void*)buf) int();
  int* c = ::new((void*)buf) int(7);
  int* d = ::new((void*)buf) int{9};
  return *b + *c + *d - 16 + (a == 0 ? 1 : 0);
}

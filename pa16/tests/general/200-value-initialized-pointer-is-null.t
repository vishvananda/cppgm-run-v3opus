typedef unsigned long ulong;

void* operator new(ulong, void* p) noexcept { return p; }

struct Slots {
  int* braced{};
  int* written{0};
  int* parens;
  Slots() : parens() {}
};

char buf[16];

int main() {
  Slots s;
  int** made = ::new((void*)buf) int*();
  int* local{};
  return (s.braced == 0 && s.written == 0 && s.parens == 0 && *made == 0 &&
          local == 0) ? 0 : 1;
}

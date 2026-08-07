namespace outer {
namespace {
namespace inner {
int counted;
struct Held { int value; void set() { value = 1; } };
Held held;
}
}
}

int main() {
  outer::inner::held.set();
  outer::inner::counted = outer::inner::held.value;
  return outer::inner::counted == 1 ? 0 : 1;
}

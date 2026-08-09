// 8.3.5p6: the parameter and return types of a function nobody is defining
// name a class the program may only have declared, and a cast's type-id, a
// `sizeof` over a pointer and a default argument name no complete type either.
template<class T> struct holder {
  static const int factor = T::factor;
  int base;
};

struct box;
void takes(holder<box>);
auto trailing() -> holder<box> *;
int probe(int, holder<box> * = 0);
int cast_only(void * p) { return (holder<box> *)p == 0 ? 1 : 0; }
int size_only() { return sizeof(holder<box> *) == sizeof(void *) ? 1 : 0; }
struct box { static const int factor = 3; };

int main() {
  holder<box> a;
  a.base = 13;
  return a.base * holder<box>::factor == 39 ? 0 : 1;
}

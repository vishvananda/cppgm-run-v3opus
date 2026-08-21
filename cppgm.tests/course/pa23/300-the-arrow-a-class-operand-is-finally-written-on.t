// N3485 focus: 13.5.6 [over.ref] the arrow a class operand is finally written on
typedef int scalar;

struct value {
  int held;
  int twice() const { return held * 2; }
};

struct inner {
  value *at;
  value *operator->() const { return at; }
};

struct outer {
  inner step;
  inner operator->() const { return step; }
};

struct scalar_holder {
  scalar *at;
  scalar *operator->() { return at; }
};

template<class T>
struct held_by {
  T *at;
  T *operator->() { return at; }
};

template<class T>
int read_through(held_by<T> from) {
  return from->held;
}

int main() {
  value one = {7};
  inner near = {&one};
  outer far = {near};
  if (near->held != 7 || far->held != 7) { return 1; }
  if (near->twice() != 14 || far->twice() != 14) { return 2; }
  far->held = 9;
  if (one.held != 9) { return 3; }
  int *reached = &far->held;
  if (*reached != 9) { return 4; }
  scalar counted = 3;
  scalar_holder holds = {&counted};
  holds->~scalar();
  held_by<value> generic = {&one};
  if (read_through(generic) != 9) { return 5; }
  return 0;
}

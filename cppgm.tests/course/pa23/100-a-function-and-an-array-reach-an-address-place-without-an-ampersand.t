// VALIDATION: compile-pass
// 14.3.2p1: the `&` may be omitted where the name refers to a function or an
// array, and 14.1p8 writes a place of array or function type as the pointer
// 4.2p1 and 4.3p1 hand back.

int digits[3] = { 4, 5, 6 };

int twice(int value);
int twice(int value) { return value * 2; }

template<int *Elements>
struct first
{
  static int read() { return Elements[0]; }
};

template<int Call(int)>
struct applied
{
  static int run(int value) { return Call(value); }
};

template<int (&Whole)[3]>
struct third
{
  static int read() { return Whole[2]; }
};

int main()
{
  return first<digits>::read() == 4 && applied<twice>::run(8) == 16 &&
         third<digits>::read() == 6 ? 0 : 1;
}

// 20.8.2p1 [func.require] with 5.2.2p1 [expr.call]: `INVOKE(f, t1, ..., tN)` is
// the call `f(t1, ..., tN)`, and where the operand standing for `f` *names* a
// function the call runs that declaration - the same line a callee the program
// wrote as a name is written on.  4.3's pointer is what such a name would be
// worth in an operand position that asks for a value, and a call asks for none:
// an operand that came to a function some other way names no declaration and is
// called through what it came to.
namespace outer
{
  int taking(int n) { return n; }
}

using outer::taking;

struct held
{
  static int stat(int n) { return n + 1; }
};

template<class T> T identity(T v) { return v; }

int with_default(int a, int b = 7) { return a + b; }

int through_a_pointer(int n) { return n + 2; }

int total()
{
  int (*pointed)(int) = through_a_pointer;
  return __builtin_invoke(taking, 1) + __builtin_invoke(held::stat, 2) +
    __builtin_invoke(identity<int>, 3) + __builtin_invoke(with_default, 4) +
    __builtin_invoke(pointed, 5);
}

int main()
{
  return total();
}

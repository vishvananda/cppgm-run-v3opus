// N3485 focus: 14.2 [temp.names], 14.8.1 [temp.arg.explicit], 13.4 [over.over]
// A template-id names one specialization of each declaration of the name its
// argument list fits, and a target type chooses among them.  Naming the same
// specialization again names the one declaration it already made.
namespace N { template<class T> void f(T); template<class T> void f(T, int); }
struct s {};
void take(void(*)(s));
void take2(void(*)(s, int));
void use() {
  take(static_cast<void(*)(s)>(&N::f<s>));
  take2(static_cast<void(*)(s, int)>(&N::f<s>));
  take(static_cast<void(*)(s)>(&N::f<s>));
}

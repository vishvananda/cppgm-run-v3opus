// A by-value class parameter of a *constructor* is 5.2.2p4's boundary exactly
// as an ordinary call's is: the caller names the storage the parameter stands
// in before the argument runs, so a call handing back an object of that class
// builds it there rather than being copied into it.
struct T { T(); T(const T&); };
T from(T t);
struct W { W(T d); };
struct Pair { Pair(T a, T b); };

void one() { W w(from(T())); }
void two() { Pair p(from(T()), T()); }
void named(T t) { W w(t); }

int main() { return 0; }

decltype(x)::Y1 y1;
struct C2 : decltype(x)::C1 { };
int f1() : decltype(x)::C1(1) { }

// N3485 focus: 14.8.2.1 [temp.deduct.call], 14.8.2.5 [temp.deduct.type]
// A call deduces each template parameter from the argument passed to it, both
// where the parameter is the whole of the argument's type and where it stands
// inside a pointer written around it.
template<class T, class U> void p(T, U);
template<class T> void q(T*);
void use() { int a; 0, p(1, 2u); 0, q(&a); }

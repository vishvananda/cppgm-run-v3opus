// VALIDATION: compile-fail
// N3485 focus: 14.6 [temp.res], 3.4.2 [basic.lookup.argdep]
//
// 3.4.2p2 is what leaves the callee of a call written in a template definition
// to the instantiation: the namespaces searched for it are the ones its
// arguments' types are associated with, and an argument list has yet to name
// those.  A fundamental type is associated with none, so a call written with
// no arguments - or with none but literals - reaches exactly what ordinary
// lookup does, and 14.6p8 settles its callee where the definition stands.

template<class T>
int settled(T v)
{
  return nowhere_at_all() + v;
}

int main()
{
  return 0;
}

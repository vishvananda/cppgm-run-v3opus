// 14.6.2.1p9: a nested class of the current instantiation is a dependent
// type, so a base-specifier written over one is 14.6.2p3's dependent base and
// the definition is read without an argument list saying what it is.
template<class D> struct adaptor { typedef D base_type; };

template<class Key>
struct ptree {
  struct iterator : adaptor<iterator> { int held; };
};

int main() { ptree<int>::iterator i; i.held = 5; return i.held == 5 ? 0 : 1; }

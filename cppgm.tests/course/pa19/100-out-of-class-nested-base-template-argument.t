// 3.4.1p8: the base-clause of a member class defined outside its class is
// read in the region its class-head-name reached, so a template argument
// there names a member type of the enclosing class.
template<class D> struct adaptor { typedef D base_type; };

struct outer { typedef int value_type; struct inner; };

struct outer::inner : adaptor<value_type> {
  typedef base_type held;
  held m;
};

int main() { outer::inner i; i.m = 3; return i.m == 3 ? 0 : 1; }

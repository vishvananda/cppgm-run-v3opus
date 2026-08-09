// 14.7.1p1: a class template specialization is instantiated where it is used
// in a context requiring a completely-defined type, and a typedef-name is not
// one - so the member the class writes below the typedef is the one the
// specialization has.
template<class Owner> struct traits { typedef typename Owner::later type; };

template<class T>
struct basic {
  typedef traits<basic> holder;
  typedef int later;
};

typedef basic<char>::holder::type result;

int main() { result value = 0; return value; }

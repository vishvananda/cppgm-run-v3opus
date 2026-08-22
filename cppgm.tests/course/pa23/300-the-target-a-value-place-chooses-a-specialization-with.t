// 13.4p1 at 14.1p4's value place: a template-id whose argument-list completes
// more than one declaration of the name is an overload set exactly as an
// overloaded name is, and the place's own type is one of the targets that
// chooses one of them.  The place is declared before the argument that reaches
// it, so the reading that looks the spelling up has the target in hand - which
// `&f<int>` written at an address constant has not.
template<class T> int pick(T given) { return int(given); }
template<class T> int pick(T given, int) { return int(given) + 8; }

struct holder {
  template<class T> static int pick(T given) { return int(given) + 16; }
  template<class T> static int pick(T given, int) { return int(given) + 32; }
};

template<int (*P)(int)>
struct through { static int get(int given) { return P(given); } };

int main() {
  return through<&pick<int> >::get(1) +
         through<holder::pick<int> >::get(2) - 19;
}

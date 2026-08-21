// 14.1p4 with 5.1.1p8 again, at the door 14.5.1.3p1 opens: an out-of-class
// member definition writes a head of its own, whose names stand for the places
// the class's head declared - and where that place is a pack, what the name is
// bound to is 14.5.3p1's settled run.  A run is one entry of the type table
// interned by its elements and says nothing of the place it came from, so the
// head is what has to be asked, exactly as it is at every other binding.  14.6p8
// reads such a definition where it stands, and every name it writes for a run
// of values is refused wherever that binding says the name is a type.
int sum() { return 0; }
int sum(int a) { return a; }
int sum(int a, int b) { return a + b; }

template<int... Ns>
struct constants {
  template<class... Rest> static int total(Rest... rest);
  static int span();
};

template<int... Ns>
template<class... Rest>
int constants<Ns...>::total(Rest... rest) { return sum(Ns...) + sizeof...(rest); }

template<int... Ns>
int constants<Ns...>::span() { return sizeof...(Ns); }

template<class T, int... Ns>
struct picked {
  template<class U> static int total(U);
};

template<int... Ns>
struct picked<char, Ns...> {
  template<class U> static int total(U);
};

template<int... Ns>
template<class U>
int picked<char, Ns...>::total(U) { return sum(Ns...); }

int main() {
  return constants<3, 4>::total(0) - 8 + constants<>::total() +
         constants<3, 4>::span() - 2 + picked<char, 5>::total(0) - 5;
}

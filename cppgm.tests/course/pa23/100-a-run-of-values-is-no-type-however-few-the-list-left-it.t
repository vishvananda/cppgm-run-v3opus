// 14.1p4 and 5.1.1p8: a name bound to a run of values names values and no
// type.  A settled run is one entry of the type table interned by its elements,
// so it carries nothing of the place it was bound at - which leaves the
// declaration the fact's only carrier, exactly as the length of a run of places
// is carried on the first of them.  14.6p8 reads a member template's definition
// where it stands, and every name it writes for a run of values is refused
// wherever that declaration says the name is a type.
int sum() { return 0; }
int sum(int a) { return a; }
int sum(int a, int b) { return a + b; }

template<int... Ns>
struct constants {
  template<class... Rest>
  static int total(Rest... rest) { return sum(Ns...); }
};

// 14.5.3p4 with 8.3.5p10: the clause the arguments settled to a run of no
// elements declared no place at all and still declares the pack, whose name an
// expansion in the body reads as the nothing it stands for.
template<class... Args>
struct clause {
  template<class... Rest>
  static int count(Args... args, Rest... rest) {
    return sizeof...(args) + sum(args...);
  }
};

int main() {
  return constants<3, 4>::total() - 7 + clause<>::count() +
         clause<int>::count(5) - 6;
}

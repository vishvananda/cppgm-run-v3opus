// VALIDATION: compile-pass
// N3485 focus: 14.7.2 [temp.explicit], 9.3 [class.mfct], 5.2.3 [expr.type.conv]
//
// 14.7.2p10 says an explicit instantiation declaration suppresses the implicit
// instantiation of the entity it names, and p11 says a definition of the same
// specialization in the same unit takes that back.  Neither is a question about
// the order the program wrote the two in: what the clause leaves to the other
// unit is 9.3p2's out-of-line copy, which is a fact of the *definition* and not
// of the declaration - so a member whose own definition stands below the
// `extern template` is left to the other unit exactly as one written above it
// is, and a member defined in the class body is left to this one either way.
//
// 5.2.3p3's array prvalue is beside them: 12.2p1 makes it an object, so it
// stands in storage of the function's wherever the reading finds it - at a
// discarding, at an initialization that takes 4.2's pointer from it, and under
// the comma whose result it is.

typedef int three[3];

int bump(int x)
{
  return x + 1;
}

template<class T>
struct held
{
  // 9.3p2: a definition written in the class body is one every unit that needs
  // one writes for itself, which is what 14.7.2p10's suppression does not
  // reach.
  int inside()
  {
    return 1;
  }
  int below();
  int above();
  struct nested
  {
    int deep();
  };
  static int count;
};

// 14.7.2p10 over a definition written *above* the declaration.
template<class T>
int held<T>::above()
{
  return 2;
}

extern template struct held<int>;

// 14.7.2p10 over a definition written *below* it: the declaration said nothing
// about which side of it the definition would stand on.
template<class T>
int held<T>::below()
{
  return 3;
}

template<class T>
int held<T>::nested::deep()
{
  return 4;
}

template<class T>
int held<T>::count = 5;

template<class T>
struct owed
{
  int one();
  int two();
};

template<class T>
int owed<T>::one()
{
  return 7;
}

extern template struct owed<int>;

template<class T>
int owed<T>::two()
{
  return 8;
}

// 14.7.2p11: this unit owes them again from here, however far above this the
// declaration stands and whichever side of it their definitions were written
// on.
template struct owed<int>;

template<class T>
struct kept
{
  int only();
};

// 7.1.2p1's `inline` written on a definition outside the class is still an
// out-of-line copy of it, which p10 leaves to the unit the declaration names.
template<class T>
inline int kept<T>::only()
{
  return 6;
}

extern template struct kept<int>;

int taken(const int* p)
{
  return p[2];
}

int main()
{
  held<int> one;
  int sum = one.inside() + one.above() + one.below() + held<int>::count;
  held<int>::nested two;
  owed<int> three_of;
  kept<int> four;
  sum = sum + two.deep() + three_of.one() + three_of.two() + four.only();
  // 5.2.3p3 with 12.2p1 at the three readings that find the object: the
  // discarding, the comma whose result it is, and the initialization that
  // takes 4.2's pointer from the array's name.
  (void)three{bump(0), bump(1), bump(2)};
  (void)(three{bump(3), bump(4), bump(5)}, three{bump(6), bump(7), bump(8)});
  const int* into = three{10, 20, 30};
  sum = sum + into[2] + taken(three{40, 50, 60});
  return sum;
}

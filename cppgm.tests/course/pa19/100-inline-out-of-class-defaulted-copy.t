// 8.4.2p2: `= default` written outside the class is a definition of the member
// the class declared, and what it defines is the definition the standard
// describes - so 12.8p12 reads the class again and finds the copy is the bytes.
// 3.2p4 still leaves the definition one this unit holds: the call 12.8p12 left
// out does not unwrite the definition the program wrote.
struct Held {
  int& bound;

  Held(int& r) : bound(r) {}
  Held(const Held&);
};

inline Held::Held(const Held&) = default;

int main()
{
  int value = 6;
  Held source(value);
  Held copy(source);
  return copy.bound - 6;
}

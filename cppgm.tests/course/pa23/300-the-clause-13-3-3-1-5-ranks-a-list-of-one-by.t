// 13.3.3.1.5p6: a braced-init-list of one clause reaches a parameter that is no
// class through the sequence required to convert *that clause* to it, so two
// candidates a list of one length would rank alike are told apart by what the
// clause is worth.  `{1}` is the identity for `int` and a floating-integral
// conversion for `double`; `{2.0}` is the identity for `double` and a
// conversion for `int`.  A list ranked by its length alone leaves both calls
// with no best declaration.
int pick(int given) { return given; }
int pick(double given) { return int(given) + 8; }

int chosen(int given) { return given + 16; }
int chosen(double given) { return int(given); }

int main() {
  return pick({1}) + chosen({2.0}) - 3;
}

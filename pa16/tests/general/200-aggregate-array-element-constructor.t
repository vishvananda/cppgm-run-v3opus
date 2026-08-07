// VALIDATION: compile-pass
// 13.3.1.7: an element of an array of class type is an object of its own, so
// an aggregate element is built by the constructor 8.5.1 gives the class from
// its members - including where a clause reached only some of them.

struct Pair {
  int first;
  int second;
};

int main()
{
  Pair pairs[3] = {{1, 2}, {3}, {5, 6}};
  return pairs[0].first == 1 && pairs[0].second == 2 &&
    pairs[1].first == 3 && pairs[1].second == 0 &&
    pairs[2].second == 6 ? 0 : 1;
}

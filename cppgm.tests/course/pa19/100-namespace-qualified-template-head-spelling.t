// N3485 focus: 9.4.2 [namespace.memdef] and 14.1 [temp.param] a
// template-declaration whose class-head-name is qualified defines the template
// the region that name reaches already declared, and 14.1p2 lets its own head
// spell the places in whatever order it likes - so the names the body writes
// stand for the arguments those places take and not for the ones the first
// declaration named.

namespace outer
{
namespace store
{
template<class First, class Second>
struct pair;
}

template<class Left, class Right>
struct store::pair
{
  Left first;
  Right second;
  int total() const { return first + second; }
};
}

int main()
{
  outer::store::pair<int, char> both;
  both.first = 40;
  both.second = 2;
  return both.total() - 42;
}

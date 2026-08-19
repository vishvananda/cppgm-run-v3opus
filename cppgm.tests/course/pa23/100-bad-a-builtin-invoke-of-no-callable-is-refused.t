// 20.8.2p1: the first operand of `INVOKE` is what the call is made of, so an
// operand whose class declares no function call operator names no call at all -
// which outside a deduction is a program that is refused.
template<class T> T&& declval();

struct not_callable
{
  int member;
};

typedef decltype(__builtin_invoke(declval<not_callable&>(), 1)) answer;

int main()
{
  return 0;
}

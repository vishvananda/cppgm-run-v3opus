// 7.1.5p1: a constexpr function this unit has only declared is one no constant
// expression may call, so the array bound below is not a constant expression.

constexpr int later();

int held[later()];

constexpr int later() { return 4; }

int main()
{
  return sizeof(held) == 16 ? 0 : 1;
}

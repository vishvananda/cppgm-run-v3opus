// 20.8.2p1 with 5.2.2p4: `INVOKE` is a call, so the arguments after the
// callable reach their parameters through the conversions 13.3 chose - a
// user-defined conversion among them - and the value the call comes to is the
// one that call would have had written directly.
struct counted
{
  int held;
  operator int() const { return held; }
};

int add_two(int value)
{
  return value + 2;
}

struct scaler
{
  int by;
  int operator()(int value) const { return value * by; }
};

int main()
{
  counted seven = {7};
  scaler triple = {3};
  int through_pointer = __builtin_invoke(&add_two, seven);
  int through_object = __builtin_invoke(triple, 4);
  return through_pointer == 9 && through_object == 12 ? 0 : 1;
}

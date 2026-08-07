struct Inner { };
struct Outer { Inner held; };

int main() {
  Inner one = {};
  Outer two = {};
  Inner three;
  return (&one == 0) || (&two == 0) || (&three == 0) ? 1 : 0;
}

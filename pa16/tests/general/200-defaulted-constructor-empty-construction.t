struct Quiet { Quiet() {} };
struct Loud { int value; Loud() { value = 4; } };

struct OverQuiet : Quiet { OverQuiet() = default; };
struct DeepQuiet : OverQuiet { DeepQuiet() = default; };
struct HoldsQuiet { Quiet held; };
struct HoldsLoud { Loud held; HoldsLoud() = default; };
struct WroteBody { Quiet held; WroteBody() {} };

int main() {
  DeepQuiet deep;
  HoldsQuiet quiet;
  HoldsLoud loud;
  WroteBody wrote;
  return loud.held.value == 4 ? 0 : 1;
}

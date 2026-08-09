// 1.4p8 and 5.3.6: `__alignof__` is the second spelling this implementation
// reserves for `alignof`, and 2.12 leaves both identifiers.
template<class T>
struct alignas(__alignof__(T)) HoldsAsT {
  char c;
};

HoldsAsT<double> held;

int main() {
  return __alignof__(HoldsAsT<double>) == __alignof(double) ? 0 : 1;
}

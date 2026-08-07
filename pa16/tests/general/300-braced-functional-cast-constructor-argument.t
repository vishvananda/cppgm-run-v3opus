struct T {
  int n;
  T(int v) { n = v; }
};

int take(const T& value) { return value.n; }

int main() {
  T local = T{4};
  return take(T{3}) + local.n - 7;
}

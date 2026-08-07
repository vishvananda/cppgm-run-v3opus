// 5.2.2p7: a constructor declared with an ellipsis is chosen by 13.3.1.3 like
// any other, and an argument no parameter names is passed as it stands under
// the default argument promotions - the same way a call the program wrote
// passes one.
struct ellipsis_holder {
  int held;
  ellipsis_holder(int given, ...) : held(given) {}
};

int main() {
  ellipsis_holder written(1, 2, 3);
  return written.held - 1;
}

struct Holder {
  static thread_local int slot;
};

int Holder::slot = 3;

int main() {
  return Holder::slot;
}

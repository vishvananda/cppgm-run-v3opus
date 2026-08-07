struct Slot {
  int held;
  ~Slot();
};

thread_local Slot cell = {5};

int main() {
  return cell.held - 5;
}

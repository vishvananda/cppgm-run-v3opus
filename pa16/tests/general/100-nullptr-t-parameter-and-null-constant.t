typedef decltype(nullptr) null_type;

null_type spelled = nullptr;
void* address = nullptr;

bool same(null_type a, null_type b) { return a == b; }

int main() {
  null_type from_zero = 0;
  void* reached = spelled;
  return same(nullptr, 0) && reached == 0 && from_zero == nullptr ? 0 : 1;
}

// N3485 focus: 13.5.6 [over.ref] an arrow no operator-> a call reaches answers
struct value { int held; };

struct holder {
  value *at;
private:
  value *operator->() { return at; }
};

int main() {
  value one = {7};
  holder from = {&one};
  return from->held;
}

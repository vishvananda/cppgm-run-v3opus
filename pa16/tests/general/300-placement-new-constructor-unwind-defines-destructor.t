int order;

struct Leaf {
  Leaf() { order = order + 1; }
  ~Leaf() { order = order + 10; }
};

struct Pair {
  Leaf first;
  Leaf second;
};

char storage[8];

void* operator new(unsigned long size, void* where) { return where; }

int main() {
  ::new (storage) Pair();
  return order == 2 ? 0 : 1;
}

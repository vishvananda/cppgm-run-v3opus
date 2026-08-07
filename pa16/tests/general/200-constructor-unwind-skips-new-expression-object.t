int order;
char storage[64];

void * operator new(unsigned long, void * p) { return p; }

struct Node {
  Node() { order = order * 3 + 1; }
  ~Node() { order = order * 3 + 2; }
};

struct Owner {
  Node * held;
  Node kept;
  Owner() : held(::new((void *)storage) Node), kept() {}
};

int main() {
  {
    Owner owner;
  }
  ((Node *)(void *)storage)->~Node();
  return order == 44 ? 0 : 1;
}

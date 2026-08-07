int order;

struct Held {
  ~Held() { order = order * 3 + 2; }
};

struct Box {
  Held held;
};

void destroy(Box * p)
{
  p->~Box();
}

int main() {
  char storage[8];
  Box * p = (Box *)storage;
  destroy(p);
  return order == 2 ? 0 : 1;
}

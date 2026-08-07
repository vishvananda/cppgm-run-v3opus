int order;

struct Held {
  ~Held() { order = order * 3 + 2; }
};

struct Box {
  Held held;

  inline
  ~Box();
};

Box::~Box()
{
  order = order * 3 + 1;
}

int main() {
  {
    Box box;
  }
  return order == 5 ? 0 : 1;
}

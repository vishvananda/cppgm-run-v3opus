// VALIDATION: compile-pass
struct node
{
  typedef node * link;
  int held;

  void clear() { held = 0; }
  static void clear(link which) { which->clear(); }
};

int main()
{
  node n;
  n.held = 3;
  node::clear(&n);
  return n.held;
}

// VALIDATION: compile-pass
// N3485 focus: 11.3 [class.friend], 14.5.4 [temp.friend]
// 14.5.4p1: a friend declaration whose declarator-id is a template-id names a
// specialization of a template some region already declares and declares
// nothing of its own - so what it does is grant, and which region declares the
// template is the only thing the qualified and the unqualified spellings differ
// by.

namespace tools
{
  template<class T> int peek(T);
}

template<class T> int reach(T);

struct vault
{
  vault(int v) : held(v) {}

  friend int tools::peek<vault>(vault);
  friend int reach<>(vault);

private:
  int held;
};

namespace tools
{
  template<class T> int peek(T t) { return t.held; }
}

template<class T> int reach(T t) { return t.held + 1; }

int main()
{
  vault v(7);
  return tools::peek<vault>(v) == 7 && reach(v) == 8 ? 0 : 1;
}

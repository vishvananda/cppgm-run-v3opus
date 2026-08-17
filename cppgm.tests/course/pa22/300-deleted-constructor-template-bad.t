// VALIDATION: compile-fail
// N3485 focus: 8.4.3 [dcl.fct.def.delete], 14.5.2 [temp.mem]

struct refuses
{
  int n;

  refuses()
    : n(0)
  {
  }

  template<class U>
  refuses(U) = delete;
};

int main()
{
  refuses made(3);
  return made.n;
}

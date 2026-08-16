// VALIDATION: compile-pass
// 13.4p1 and 14.8.2.2p1: `&f` where `f` names a function template is one of
// the places a target type chooses a declaration, and the declaration it
// chooses is the specialization that target deduces.  The line stands for the
// `&` the program wrote however late the choice is made, so a constructor's
// parameter, a declaration's initializer and an element of an array of
// pointers each reach the same one.

namespace chosen
{
template<class T>
void run(T)
{
}
}

struct adapter
{
  adapter(void (*taken)(int)) : held(taken) {}

  void (*held)(int);
};

int main()
{
  adapter one(&chosen::run);
  void (*two)(char) = &chosen::run;
  void (*table[2])(long) = {&chosen::run, &chosen::run};
  one.held(1);
  two('a');
  table[0](2);
  return 0;
}

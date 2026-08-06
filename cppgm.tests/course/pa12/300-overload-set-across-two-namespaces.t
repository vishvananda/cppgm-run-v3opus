// N3485 focus: 3.4p2 [basic.lookup], 7.3.4 [namespace.udir], 13.1 [over.load]
// A lookup a using-directive extended reaches the declarations of several
// namespaces at once.  Two declarations of one name in two of them are ill
// formed unless both are functions, and then the set the lookup found is the
// union of their declarations, which the arguments of a call and a target type
// choose from alike.
namespace narrow
{
  void pick(int);
  void pick(int, int);
}
namespace wide
{
  void pick(long);
}
namespace pointers
{
  void pick(char*);
}
using namespace narrow;
using namespace wide;
using namespace pointers;
void take(void (*chosen)(long));
void use()
{
  char* text = 0;
  pick(1);
  pick(1L);
  pick(text);
  pick(1, 2);
  take(pick);
}

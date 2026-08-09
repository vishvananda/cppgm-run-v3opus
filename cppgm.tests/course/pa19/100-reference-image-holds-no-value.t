// N3485 focus: 12.2 [class.temporary], 8.5.3 [dcl.init.ref] and 3.6.2
// [basic.start.init] - a reference's storage holds the address of an object, and
// where its initializer was a value there is no object there for the image to
// name.  3.10p1 makes a prvalue a value, so what the reference binds is a
// temporary the program gives storage to before it runs: the image holds
// nothing and the binding is an action.  A reference bound to an object the
// program already has is that same action over an address, and one to an object
// of static storage duration is the address itself.

int reached = 7;
int elsewhere[2] = { 8, 9 };

const int & bound_to_a_value = 5;
const int & bound_to_an_object = reached;
const int & bound_to_an_element = elsewhere[1];

template<class T>
struct held
{
  static T value;
};

template<class T>
T held<T>::value;

const int & bound_to_a_member = held<int>::value;

int main()
{
  return bound_to_a_value + bound_to_an_object + bound_to_an_element +
         bound_to_a_member - 21;
}

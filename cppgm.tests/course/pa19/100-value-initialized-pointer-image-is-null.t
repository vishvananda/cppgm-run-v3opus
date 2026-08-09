// N3485 focus: 8.5p7 [dcl.init] and 4.10p1 [conv.ptr] - the zero a
// value-initialization gives an object is a value of that object's own type, so
// a pointer holds the null pointer value and not the integer a null pointer
// constant is written as.  The image spells that value the way a body does, and
// an item inside a structured one says it by being storage; the integer the
// program itself wrote keeps its own spelling wherever it converts.

typedef int * pointer;

pointer value_initialized = pointer();
pointer written_as_zero = 0;
pointer written_as_nullptr = nullptr;

struct pair_of
{
  pointer first;
  int second;
};

pair_of subobject_written = { 0, 1 };
pair_of subobject_left = { };

int reached;
pair_of subobject_addressed = { &reached, 2 };

template<class T>
struct held
{
  static T value;
};

template<class T>
T held<T>::value;

int main()
{
  return (value_initialized == 0) + (written_as_zero == nullptr) +
         (written_as_nullptr == pointer()) + subobject_written.second +
         (subobject_left.first == 0) + (subobject_addressed.first == &reached) +
         (held<pair_of>::value.first == 0) - 7;
}

// N3485 focus: 5.2.3p2 [expr.type.conv], 8.5p7 [dcl.init] and 4.10p1
// [conv.ptr] - `T()` value-initializes what it makes, and the zero it holds is
// a value of the object's own type: a pointer takes the null pointer value
// rather than the integer a null pointer constant is written as.

typedef void * anything;

int reached(void * where)
{
  return where == nullptr ? 0 : 1;
}

template<class T>
int through(T where)
{
  return reached(where);
}

int main()
{
  anything held = anything();
  return reached(anything()) + reached(0) + reached(held) +
         through(anything());
}

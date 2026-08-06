// N3485 focus: 5.2.9 [expr.static.cast], 5.4 [expr.cast], 8.5.3 [dcl.init.ref]
// A cast to a reference type binds the operand itself where the two are
// reference-related, whatever wrote the operand's line, and binds a temporary
// holding a conversion of it where they are not.  The first is the operand's own
// line with what the cast made of it; the second is a value of its own.
int values[4];
int* pointer = values;
long wide;
int one = 1;
int two = 2;
void use()
{
  int& related_subscript = (int&)values[1];
  int& related_indirection = (int&)*pointer;
  const int& related_sum = (const int&)(one + two);
  int& related_assignment = (int&)(one = two);
  const int& related_condition = (const int&)(one > two ? one : two);
  const int& related_cast = (const int&)(int)one;
  const int& converted_object = (const int&)wide;
  const int& converted_value = (const int&)sizeof(int);
  0, related_subscript, related_indirection, related_sum, related_assignment;
  0, related_condition, related_cast, converted_object, converted_value;
}

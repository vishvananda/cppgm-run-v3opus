// VALIDATION: compile-pass
// 5.16p3: where the second and third operands of a conditional have different
// types and either is of class type, an attempt is made to convert each of them
// to the type of the other, and the conversion stands only where exactly one of
// the two directions can be made.  The clause has three bullets and this
// milestone had written one of them.  An operand that is an lvalue is reached
// by binding an lvalue reference to it *directly*, which a conversion function
// handing back a reference does - including one deduced from a conversion
// function template by 14.8.2.3.  Where neither direction binds a reference,
// the last bullet asks for an ordinary conversion to the type the other operand
// is worth as a prvalue - and where both operands are of class type and one
// derives from the other, that conversion is the slice and is written one way
// only, from the derived class to the base.

struct counted
{
  int value;
};

struct derived : counted
{
  int extra;
};

template<class T>
struct storage
{
  static T held;
};

template<class T>
T storage<T>::held;

// 12.3.2 and 14.8.2.3: a conversion function template deduced for a reference
// destination hands back an lvalue, which 5.16p3's first bullet binds.
struct reaches_reference
{
  template<class T>
  operator const T&() const
  {
    return storage<T>::held;
  }
};

// 12.3.2p1 handing back a value instead, which the last bullet converts.
struct reaches_value
{
  operator counted() const
  {
    counted made;
    made.value = 31;
    return made;
  }
};

derived make_derived()
{
  derived built;
  built.value = 41;
  built.extra = 42;
  return built;
}

int main()
{
  storage<counted>::held.value = 21;

  // The first bullet: the third operand is an lvalue, and the second reaches
  // an lvalue reference to its type through the conversion function template.
  const counted named = { 11 };
  reaches_reference reaching;
  if ((true ? reaching : named).value != 21)
  {
    return 1;
  }
  if ((false ? reaching : named).value != 11)
  {
    return 2;
  }

  // The last bullet: nothing binds a reference, so the operand is converted to
  // the type the other is worth as a prvalue and the result is that prvalue.
  counted plain = { 12 };
  reaches_value making;
  if ((true ? making : plain).value != 31)
  {
    return 3;
  }
  if ((false ? making : plain).value != 12)
  {
    return 4;
  }

  // The last bullet's first half: two related classes, where the conversion is
  // from the derived class to the base and the result is a prvalue of the base.
  counted base_object = { 13 };
  if ((true ? make_derived() : base_object).value != 41)
  {
    return 5;
  }
  if ((false ? make_derived() : base_object).value != 13)
  {
    return 6;
  }

  // 5.16p4 as it stood: two lvalues of one type are the lvalue one of them
  // names, and a derived operand beside a base one names its base subobject.
  derived left_object;
  left_object.value = 14;
  left_object.extra = 15;
  counted& picked = true ? left_object : base_object;
  picked.value = 16;
  if (left_object.value != 16 || base_object.value != 13)
  {
    return 7;
  }

  return 0;
}

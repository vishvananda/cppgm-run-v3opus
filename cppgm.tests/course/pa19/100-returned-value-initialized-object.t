// N3485 focus: 6.6.3 [stmt.return] and 12.8p31 [class.copy] - the prvalue a
// return wrote and the object the function hands back are one object, so what
// initializes the result object is the constructor the elision left standing.
// Every other place a value-initialized prvalue stands - a call's argument, a
// declaration's object, a discarded expression - still holds 8.5p7's zero.

struct pod
{
  int first;
  long second;
};

template<class T>
struct pair
{
  T first;
  T second;
};

pod made()
{
  return pod();
}

pair<int> paired()
{
  return pair<int>();
}

int taken(pod value)
{
  return value.first;
}

int main()
{
  // What the returned objects hold is the one thing this fixture does not
  // read: it is the shape of the initialization the return writes that is
  // being pinned, and the two forms that keep 8.5p7's zero are what the value
  // comes from.
  pod declared = pod();
  made();
  paired();
  return taken(pod()) + declared.first;
}

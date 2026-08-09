// N3485 focus: 3.6.3 [basic.start.term] and 3.6.2 [basic.start.init] - the end
// of the lifetime of an object with static storage duration is registered where
// the program starts, so a unit that owes the program that end owes it the entry
// the registration stands in - however little the initialization beside it came
// to.  14.7.1p6 is what leaves the initialization of an instantiated static data
// member of trivial type writing nothing at all.

struct closed
{
  int marker;
  ~closed();
};

closed::~closed()
{
  marker = 0;
}

template<class T>
struct held
{
  static T value;
};

template<class T>
T held<T>::value;

closed * reach()
{
  return &held<closed>::value;
}

int main()
{
  return reach()->marker;
}

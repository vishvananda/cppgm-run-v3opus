// VALIDATION: compile-pass
// N3485 focus: 14.5.5 [temp.class.spec], 14.5.1.3 [temp.mem.class],
// 14.6.1 [temp.local]
// 14.6.1p1 gives a class template's own definition a class to be read as - the
// one its name over its own parameters denotes - and 14.5.5p1 leaves one such
// class per body an argument list may be read from.  So an out-of-class member
// definition names the body it belongs to by the arguments its declarator-id
// writes, and the same member name written over the primary's places and over a
// pattern declares two members of two classes.

struct enabled {};

template<class Key, class Value>
struct pair
{
  Key first;
  Value second;
};

template<class T, class Tag>
struct map_base
{
  int get();
  struct held;
};

template<class Key, class Value>
struct map_base<pair<Key, Value>, enabled>
{
  typedef map_base<pair<Key, Value>, enabled> self;

  int get();
  struct held;
};

template<class T, class Tag>
int map_base<T, Tag>::get()
{
  return 1;
}

template<class Key, class Value>
int map_base<pair<Key, Value>, enabled>::get()
{
  self *reached = static_cast<self *>(this);
  return reached != 0 ? 7 : 2;
}

template<class T, class Tag>
struct map_base<T, Tag>::held
{
  int value() { return 11; }
};

template<class Key, class Value>
struct map_base<pair<Key, Value>, enabled>::held
{
  int value() { return 13; }
};

int main()
{
  map_base<pair<int, char>, enabled> chosen;
  map_base<int, int> primary;
  map_base<pair<int, char>, enabled>::held from_pattern;
  map_base<int, int>::held from_primary;
  return chosen.get() == 7 && primary.get() == 1 &&
                 from_pattern.value() == 13 && from_primary.value() == 11
             ? 0
             : 1;
}

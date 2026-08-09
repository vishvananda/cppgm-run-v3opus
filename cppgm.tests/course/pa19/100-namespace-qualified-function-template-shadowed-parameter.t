// N3485 focus: 3.4.1 [basic.lookup.unqual] and 14.1 [temp.param] the parameters
// a template head declared stand inside the region a qualified declarator-id
// reaches while the declarator and the body are read, so a typedef-name that
// region declares with the same spelling is the one that is hidden - and the
// definition still declares the template the region declared rather than a
// second function.

namespace holder
{
typedef char Item;

template<class Item>
Item keep(Item value);
}

template<class Item>
Item holder::keep(Item value)
{
  return value + 5;
}

int main()
{
  return holder::keep(37) - 42;
}

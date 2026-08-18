// 8.5.4p7 through 13.3.3.1p4: the clauses of a list-initialization are the
// constructor's arguments, and a narrowing conversion to a parameter is what
// makes the initialization ill formed - so it is what a substitution drops a
// candidate on.
struct takes_int
{
  takes_int(int);
};

template<class T, class = decltype(T{0.5})>
char narrowed(int);

template<class>
long narrowed(...);

template<class T, class = decltype(T{1})>
char whole(int);

template<class>
long whole(...);

int main()
{
  if(sizeof(narrowed<takes_int>(0)) != sizeof(long))
  {
    return 1;
  }
  if(sizeof(narrowed<int>(0)) != sizeof(long))
  {
    return 2;
  }
  if(sizeof(whole<takes_int>(0)) != sizeof(char))
  {
    return 3;
  }
  if(sizeof(whole<int>(0)) != sizeof(char))
  {
    return 4;
  }
  return 0;
}

// 5.7p1 and 5.2.6p1: a pointer operand of arithmetic points to a completely
// defined object type, so a substitution that reads `void *` or a pointer to a
// class the unit only declared drops the candidate rather than ending the
// program.
struct only_declared;

template<class T>
T&& value();

template<class T, class = decltype(value<T>() + 1)>
char adds(int);

template<class>
long adds(...);

template<class T, class = decltype(++value<T>())>
char steps(int);

template<class>
long steps(...);

int main()
{
  if(sizeof(adds<void *>(0)) != sizeof(long))
  {
    return 1;
  }
  if(sizeof(adds<only_declared *>(0)) != sizeof(long))
  {
    return 2;
  }
  if(sizeof(adds<int *>(0)) != sizeof(char))
  {
    return 3;
  }
  if(sizeof(steps<void *&>(0)) != sizeof(long))
  {
    return 4;
  }
  if(sizeof(steps<int *&>(0)) != sizeof(char))
  {
    return 5;
  }
  return 0;
}

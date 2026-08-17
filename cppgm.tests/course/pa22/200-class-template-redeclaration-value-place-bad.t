// VALIDATION: compile-fail
// N3485 focus: 14.5.6.1 [temp.over.link], 14.1 [temp.param]
// One head declares a type place and the other a value place, so the two
// declarations do not declare one template.

template<class T>
struct holder;

template<int N>
struct holder
{
};

int main()
{
  return 0;
}

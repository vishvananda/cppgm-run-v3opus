// 14.1p10 asks the defaults of every declaration of one template to be merged,
// which is a question about the places one head and the next both wrote - so a
// redeclaration that writes a different number of parameters declares no
// template this one is a redeclaration of.
template<class T, class U>
struct box
{
  typedef U type;
};

template<class T, class U, class V = int>
struct box;

int main()
{
  return 0;
}

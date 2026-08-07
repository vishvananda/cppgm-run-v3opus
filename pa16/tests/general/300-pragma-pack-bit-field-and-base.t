#pragma pack(push, 2)
struct Base {
  char tag;
  int value;
};

struct Derived : Base {
  char flag : 3;
  char rest : 4;
  long wide;
};
#pragma pack(pop)

int main()
{
  return sizeof(Base) == 6 && alignof(Base) == 2 &&
         sizeof(Derived) == 16 && alignof(Derived) == 2
    ? 0 : 1;
}

typedef unsigned long size_t;

int main()
{
  const char * joined = "a b" " c";
  return sizeof("a b" " c") == 7 && joined[1] == ' ' && joined[3] == ' ' &&
         joined[4] == 'c' && joined[5] == 0
    ? 0 : 1;
}

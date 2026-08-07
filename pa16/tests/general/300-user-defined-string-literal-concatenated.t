typedef unsigned long size_t;

int operator ""_len(const char * text, size_t size)
{
  return text[2] == ' ' ? (int)size : -1;
}

int main()
{
  return "ab" " cd"_len == 5 ? 0 : 1;
}

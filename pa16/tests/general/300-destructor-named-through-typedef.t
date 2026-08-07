struct Box {
  int held;
  ~Box() { held = 0; }
};

typedef Box Alias;

void wipe(Alias * p)
{
  p->~Alias();
  (*p).~Alias();
}

Box shared;

int main()
{
  wipe(&shared);
  return 0;
}

struct Holder {
  char quote;
  char row['"' - 30];

  Holder() : quote('"') {}

  char first() { return row[0]; }
};

int main()
{
  Holder h;
  h.row[0] = h.quote;
  return h.quote == 34 && sizeof(h.row) == 4 && h.first() == 34 ? 0 : 1;
}

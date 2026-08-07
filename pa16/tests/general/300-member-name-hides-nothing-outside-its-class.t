// VALIDATION: compile-pass
struct cell
{
  int car;
};

struct holder
{
  int cell;
  typedef int cell_alias;
};

struct pair
{
  pair(struct cell & first) : cell(first) {}
  struct cell & cell;
};

int main()
{
  cell value;
  value.car = 5;
  pair p(value);
  p.cell.car = 11;
  holder h;
  h.cell = 2;
  return value.car == 11 && h.cell == 2 ? 0 : 1;
}

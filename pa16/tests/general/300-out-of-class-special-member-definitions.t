// VALIDATION: compile-pass
struct outer
{
  typedef int entry;

  struct token
  {
    int held;
  };

  struct buffer
  {
    entry filled;

    buffer(token given);
    ~buffer();
  };
};

outer::buffer::buffer(token given) : filled((entry)given.held) { }

outer::buffer::~buffer() { filled = 0; }

int main()
{
  outer::token source;
  source.held = 4;
  outer::buffer holder(source);
  return holder.filled == 4 ? 0 : 1;
}

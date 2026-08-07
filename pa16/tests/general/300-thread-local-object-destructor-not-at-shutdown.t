int sink;

struct Held {
  int n;
  Held() : n(0) {}
  ~Held() { sink = n; }
};

Held program;
thread_local Held per_thread;

int main() {
  return program.n;
}

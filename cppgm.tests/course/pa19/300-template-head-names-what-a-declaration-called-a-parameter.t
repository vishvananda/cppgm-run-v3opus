template<class T>
int ignore(int amount);

template<class amount>
int ignore(int) {
  return 0;
}

int main() {
  return ignore<int>(2);
}

template<class T>
T zero_of(T) {
  return T();
}

template<class T>
T zero_of(T sample);

int main() {
  return zero_of(41);
}

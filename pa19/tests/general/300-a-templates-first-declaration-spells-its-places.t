template<class T>
T zero_of(T);

template<class T>
T zero_of(T sample);

template<class T>
T zero_of(T) {
  return T();
}

int main() {
  return zero_of(41);
}

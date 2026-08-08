struct value { int x; };

void take(value);

value* allocated(value a) { return new value(a); }
void written(value a) { take(value(a)); take(static_cast<value>(a)); }
void implied(value a) { value b = a; value c(a); take(a); take(b); take(c); }

int main() { return 0; }

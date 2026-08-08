struct empty {};
struct source { operator empty() const; };
struct heavy { heavy(); heavy(const heavy&); heavy(heavy&&); ~heavy(); };
struct maker { operator heavy() const; };

void sink(empty);
void hold(const heavy&);

empty carried_by_bytes(source s) { return empty(s); }
heavy carried_by_address(maker m) { return heavy(m); }
void as_argument(source s) { sink(empty(s)); }
void as_reference_argument(maker m) { hold(heavy(m)); }

int main() { return 0; }

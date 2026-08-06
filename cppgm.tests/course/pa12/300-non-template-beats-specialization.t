// N3485 focus: 13.3.3 [over.match.best]
// A function the program declared beats a specialization of a template whose
// conversions are no better, and loses to one whose conversions are better.
template<class T> void g(T);
void g(int);
void use() { 0, g(1); 0, g(1.0f); }

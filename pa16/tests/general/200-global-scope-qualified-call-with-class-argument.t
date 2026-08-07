struct YC { int held; };

int reach(const YC& c) { return c.held; }

YC c = {7};

int main() { return ::reach(c) - 7; }

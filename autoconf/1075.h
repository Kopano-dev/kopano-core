struct Foo { unsigned int x; };
struct Bar { struct Foo *__ptr; int __size; };
struct Baz { struct Bar *__ptr; int __size; };
int ns2__add(struct Bar y, int &z);

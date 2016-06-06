/*
This is a sample file documenting which C++2011 features we will
be using and which we will not.

g++ 4.3:
	decltype
g++ 4.4 (RHEL 6):
	cbegin
	rvalue refs with &&
	initializer lists
not in g++ 4.4
	lambda: [](){}
g++ 4.6
	nullptr
	range-based for (auto x : list)
g++ 4.7
	"final", "override" keywords
??
	std::unordered_map
*/
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <cstdio>
#include <ctime>
class B {
	public:
	B(){}
	B(const B &) = delete;
	virtual int foo(void);
};
class D /* final */ : public B {
	public:
	D(){}
	D(const D &) = default;
	int foo(void) /* override */;
}; 
static std::unique_ptr<std::string> foo(std::string &&z)
{
	auto p = std::unique_ptr<std::string>(new std::string);
	*p = z;
	return p;
}
int main(void)
{
	auto z = foo("bar");
	printf("%s %s\n", z->c_str(), typeid(z).name());
	std::string q{"f"};
	foo(std::move(q));
	std::list<int> list;
	list.push_back(0);
	auto j = list.cbegin();
	printf("%s\n", typeid(j).name());

	std::mutex mtx;
	std::unordered_map<unsigned int, time_t> testmap;
	testmap[1] = 2;
	if (testmap.size() != 1)
		abort();
	decltype(testmap) testmap2 = std::move(testmap);
	if (testmap.size() != 0 || testmap2.size() != 1)
		abort();
	return 0;
}

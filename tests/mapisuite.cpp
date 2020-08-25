#include <cstdlib>
#include <cppunit/CompilerOutputter.h>
#include <cppunit/extensions/TestFactoryRegistry.h>
#include <cppunit/ui/text/TestRunner.h>
#include <cppunit/extensions/HelperMacros.h>
#include <kopano/automapi.hpp>
#include <kopano/memory.hpp>
#include <kopano/CommonUtil.h>
#define TESTUSER "user1"
#define TESTPASS "xuser1"
#define TESTHOST nullptr

using namespace KC;

class basic_test : public CppUnit::TestFixture {
	CPPUNIT_TEST_SUITE(basic_test);
	CPPUNIT_TEST(test_1);
	CPPUNIT_TEST_SUITE_END();
	public:
	void setUp();
	void tearDown();
	void test_1();
};

CPPUNIT_TEST_SUITE_REGISTRATION(basic_test);

void basic_test::setUp()
{
}

void basic_test::tearDown()
{
}

void basic_test::test_1()
{
	AutoMAPI am;
	CPPUNIT_ASSERT_EQUAL(am.Initialize(), hrSuccess);

	object_ptr<IMAPISession> ses;
	auto ret = HrOpenECSession(&~ses, "0", "ubt", TESTUSER,
	           TESTPASS, TESTHOST, EC_PROFILE_FLAGS_NO_NOTIFICATIONS,
	           nullptr, nullptr);
	CPPUNIT_ASSERT_EQUAL(hrSuccess, ret);

	object_ptr<IMsgStore> store;
	ret = HrOpenDefaultStore(ses, &~store);
	CPPUNIT_ASSERT_EQUAL(hrSuccess, ret);

	memory_ptr<SPropValue> props;
	ret = HrGetOneProp(store, PR_ENTRYID, &~props);
	CPPUNIT_ASSERT_EQUAL(hrSuccess, ret);
}

int main(int argc, char **argv)
{
	using namespace CppUnit;
	auto suite = TestFactoryRegistry::getRegistry().makeTest();
	TextUi::TestRunner runner;
	runner.addTest(suite);
	runner.setOutputter(new CompilerOutputter(&runner.result(), std::cerr));
	return runner.run() ? EXIT_SUCCESS : EXIT_FAILURE;
}

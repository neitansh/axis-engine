#include "test.h"
#include "porting.h"
#include "gettext.h"

class TestGettext : public TestBase
{
public:
	TestGettext() {
		TestManager::registerTestModule(this);
	}

	const char *getName() { return "TestGettext"; }

	void runTests(ICrateDef *cratedef);

	void testFmtgettext();
};

static TestGettext g_test_instance;

void TestGettext::runTests(ICrateDef *cratedef)
{
	TEST(testFmtgettext);
}

// Make sure updatepo.sh does not pick up the strings
#define dummyname fmtgettext

void TestGettext::testFmtgettext()
{
	std::string buf = dummyname("sample text %d", 12);
	UASSERTEQ(std::string, buf, "sample text 12");

	std::string src, expect;
	src    = "You are about to join this server with the name \"%s\".\n";
	expect = "You are about to join this server with the name \"foo\".\n";
	for (int i = 0; i < 20; i++) {
		src.append("loooong text");
		expect.append("loooong text");
	}
	buf = dummyname(src.c_str(), "foo");
	UASSERTEQ(std::string, buf, expect);
}

// Luanti
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2025 sfan5

#include "test.h"

#include <sstream>

#include "server/blockmodifier.h"

class TestLBMManager : public TestBase
{
public:
	TestLBMManager() { TestManager::registerTestModule(this); }
	const char *getName() {	return "TestLBMManager"; }

	void runTests(ICrateDef *cratedef);

	void testNew(ICrateDef *cratedef);
	void testExisting(ICrateDef *cratedef);
	void testDiscard(ICrateDef *cratedef);
};

static TestLBMManager g_test_instance;

void TestLBMManager::runTests(ICrateDef *cratedef)
{
	TEST(testNew, cratedef);
	TEST(testExisting, cratedef);
	TEST(testDiscard, cratedef);
}

namespace {
	struct FakeLBM : LoadingBlockModifierDef {
		FakeLBM(const std::string &name, bool every_load) {
			this->name = name;
			this->run_at_every_load = every_load;
			trigger_contents.emplace_back("air");
		}
	};
}

void TestLBMManager::testNew(ICrateDef *cratedef)
{
	LBMManager mgr;

	mgr.addLBMDef(new FakeLBM(":foo:bar", false));
	mgr.addLBMDef(new FakeLBM("not:this", true));

	mgr.loadIntroductionTimes("", cratedef, 1234);

	auto str = mgr.createIntroductionTimesString();
	// name of first lbm should have been stripped
	// the second should not appear at all
	UASSERTEQ(auto, str, "foo:bar~1234;");
}

void TestLBMManager::testExisting(ICrateDef *cratedef)
{
	LBMManager mgr;

	mgr.addLBMDef(new FakeLBM("foo:bar", false));

	// colon should also be stripped when loading (due to old versions)
	mgr.loadIntroductionTimes(":foo:bar~22;", cratedef, 1234);

	auto str = mgr.createIntroductionTimesString();
	UASSERTEQ(auto, str, "foo:bar~22;");
}

void TestLBMManager::testDiscard(ICrateDef *cratedef)
{
	LBMManager mgr;

	// LBMs that no longer exist are dropped
	mgr.loadIntroductionTimes("some:thing~2;", cratedef, 10);

	auto str = mgr.createIntroductionTimesString();
	UASSERTEQ(auto, str, "");
}

// We should also test LBMManager::applyLBMs in the future.

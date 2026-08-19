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

	void runTests(IPlaceDef *placedef);

	void testNew(IPlaceDef *placedef);
	void testExisting(IPlaceDef *placedef);
	void testDiscard(IPlaceDef *placedef);
};

static TestLBMManager g_test_instance;

void TestLBMManager::runTests(IPlaceDef *placedef)
{
	TEST(testNew, placedef);
	TEST(testExisting, placedef);
	TEST(testDiscard, placedef);
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

void TestLBMManager::testNew(IPlaceDef *placedef)
{
	LBMManager mgr;

	mgr.addLBMDef(new FakeLBM(":foo:bar", false));
	mgr.addLBMDef(new FakeLBM("not:this", true));

	mgr.loadIntroductionTimes("", placedef, 1234);

	auto str = mgr.createIntroductionTimesString();
	// name of first lbm should have been stripped
	// the second should not appear at all
	UASSERTEQ(auto, str, "foo:bar~1234;");
}

void TestLBMManager::testExisting(IPlaceDef *placedef)
{
	LBMManager mgr;

	mgr.addLBMDef(new FakeLBM("foo:bar", false));

	// colon should also be stripped when loading (due to old versions)
	mgr.loadIntroductionTimes(":foo:bar~22;", placedef, 1234);

	auto str = mgr.createIntroductionTimesString();
	UASSERTEQ(auto, str, "foo:bar~22;");
}

void TestLBMManager::testDiscard(IPlaceDef *placedef)
{
	LBMManager mgr;

	// LBMs that no longer exist are dropped
	mgr.loadIntroductionTimes("some:thing~2;", placedef, 10);

	auto str = mgr.createIntroductionTimesString();
	UASSERTEQ(auto, str, "");
}

// We should also test LBMManager::applyLBMs in the future.

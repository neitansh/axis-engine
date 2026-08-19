// Luanti
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2013 celeron55, Perttu Ahola <celeron55@gmail.com>

#include "test.h"

#include "placedef.h"
#include "nodedef.h"
#include "content_mapnode.h"

class TestMapNode : public TestBase
{
public:
	TestMapNode() { TestManager::registerTestModule(this); }
	const char *getName() { return "TestMapNode"; }

	void runTests(IPlaceDef *placedef);

	void testNodeProperties(const NodeDefManager *nodedef);
};

static TestMapNode g_test_instance;

void TestMapNode::runTests(IPlaceDef *placedef)
{
	TEST(testNodeProperties, placedef->getNodeDefManager());
}

////////////////////////////////////////////////////////////////////////////////

void TestMapNode::testNodeProperties(const NodeDefManager *nodedef)
{
	MapNode n(CONTENT_AIR);

	ContentLightingFlags f = nodedef->getLightingFlags(n);
	UASSERT(n.getContent() == CONTENT_AIR);
	UASSERT(n.getLight(LIGHTBANK_DAY, f) == 0);
	UASSERT(n.getLight(LIGHTBANK_NIGHT, f) == 0);

	// Transparency
	n.setContent(CONTENT_AIR);
	UASSERT(nodedef->get(n).light_propagates == true);
}

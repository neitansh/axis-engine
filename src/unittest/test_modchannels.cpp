// Luanti
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2013 celeron55, Perttu Ahola <celeron55@gmail.com>

#include "test.h"

#include "cratedef.h"
#include "modchannels.h"

class TestModChannels : public TestBase
{
public:
	TestModChannels() { TestManager::registerTestModule(this); }
	const char *getName() { return "TestModChannels"; }

	void runTests(ICrateDef *cratedef);

	void testJoinChannel(ICrateDef *cratedef);
	void testLeaveChannel(ICrateDef *cratedef);
	void testSendMessageToChannel(ICrateDef *cratedef);
};

static TestModChannels g_test_instance;

void TestModChannels::runTests(ICrateDef *cratedef)
{
	TEST(testJoinChannel, cratedef);
	TEST(testLeaveChannel, cratedef);
	TEST(testSendMessageToChannel, cratedef);
}

void TestModChannels::testJoinChannel(ICrateDef *cratedef)
{
	// Test join
	UASSERT(cratedef->joinModChannel("test_join_channel"));
	// Test join (fail, already join)
	UASSERT(!cratedef->joinModChannel("test_join_channel"));
}

void TestModChannels::testLeaveChannel(ICrateDef *cratedef)
{
	// Test leave (not joined)
	UASSERT(!cratedef->leaveModChannel("test_leave_channel"));

	UASSERT(cratedef->joinModChannel("test_leave_channel"));

	// Test leave (joined)
	UASSERT(cratedef->leaveModChannel("test_leave_channel"));
}

void TestModChannels::testSendMessageToChannel(ICrateDef *cratedef)
{
	// Test sendmsg (not joined)
	UASSERT(!cratedef->sendModChannelMessage(
			"test_sendmsg_channel", "testmsgchannel"));

	UASSERT(cratedef->joinModChannel("test_sendmsg_channel"));

	// Test sendmsg (joined)
	UASSERT(cratedef->sendModChannelMessage("test_sendmsg_channel", "testmsgchannel"));
}

// Luanti
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2013 celeron55, Perttu Ahola <celeron55@gmail.com>

#include "test.h"

#include "placedef.h"
#include "modchannels.h"

class TestModChannels : public TestBase
{
public:
	TestModChannels() { TestManager::registerTestModule(this); }
	const char *getName() { return "TestModChannels"; }

	void runTests(IPlaceDef *placedef);

	void testJoinChannel(IPlaceDef *placedef);
	void testLeaveChannel(IPlaceDef *placedef);
	void testSendMessageToChannel(IPlaceDef *placedef);
};

static TestModChannels g_test_instance;

void TestModChannels::runTests(IPlaceDef *placedef)
{
	TEST(testJoinChannel, placedef);
	TEST(testLeaveChannel, placedef);
	TEST(testSendMessageToChannel, placedef);
}

void TestModChannels::testJoinChannel(IPlaceDef *placedef)
{
	// Test join
	UASSERT(placedef->joinModChannel("test_join_channel"));
	// Test join (fail, already join)
	UASSERT(!placedef->joinModChannel("test_join_channel"));
}

void TestModChannels::testLeaveChannel(IPlaceDef *placedef)
{
	// Test leave (not joined)
	UASSERT(!placedef->leaveModChannel("test_leave_channel"));

	UASSERT(placedef->joinModChannel("test_leave_channel"));

	// Test leave (joined)
	UASSERT(placedef->leaveModChannel("test_leave_channel"));
}

void TestModChannels::testSendMessageToChannel(IPlaceDef *placedef)
{
	// Test sendmsg (not joined)
	UASSERT(!placedef->sendModChannelMessage(
			"test_sendmsg_channel", "testmsgchannel"));

	UASSERT(placedef->joinModChannel("test_sendmsg_channel"));

	// Test sendmsg (joined)
	UASSERT(placedef->sendModChannelMessage("test_sendmsg_channel", "testmsgchannel"));
}

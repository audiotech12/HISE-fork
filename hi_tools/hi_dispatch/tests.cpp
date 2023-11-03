/*  ===========================================================================
*
*   This file is part of HISE.
*   Copyright 2016 Christoph Hart
*
*   HISE is free software: you can redistribute it and/or modify
*   it under the terms of the GNU General Public License as published by
*   the Free Software Foundation, either version 3 of the License, or
*   (at your option) any later version.
*
*   HISE is distributed in the hope that it will be useful,
*   but WITHOUT ANY WARRANTY; without even the implied warranty of
*   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*   GNU General Public License for more details.
*
*   You should have received a copy of the GNU General Public License
*   along with HISE.  If not, see <http://www.gnu.org/licenses/>.
*
*   Commercial licenses for using HISE in an closed source project are
*   available on request. Please visit the project's website to get more
*   information about commercial licensing:
*
*   http://www.hise.audio/
*
*   HISE is based on the JUCE library,
*   which must be separately licensed for closed source applications:
*
*   http://www.juce.com
*
*   ===========================================================================
*/

#include "JuceHeader.h"

namespace hise {
namespace dispatch {	
using namespace juce;

inline LoggerTest::LoggerTest():
	UnitTest("testing dispatch logger & queue", "dispatch")
{}

void LoggerTest::testLogger()
{
	beginTest("Testing logger");

	RootObject root(nullptr);
	beginTest("testing dispatch logger");
	Logger l(root, 0);

	root.setLogger(&l);

	l.printString("hello world");
	l.printString("nudel was geht");

	Random r;

	MyTestQueuable x(root);

	PerformanceCounter pc("start", 1);

	pc.start();

	for(int i = 0; i < 900; i++)
	{
		l.printRaw("24", 2);

		uint8 buffer[16];

		MyTestQueuable dangling(root);

		auto thisRandom = r.nextInt(16);

		if(i % 10 == 0)
			l.flush();

		for(int i = 0; i < r.nextInt(5); i++)
		{
			auto never = "never";
			l.log(&dangling, EventType::Warning, never, 5);
		}

		for(int i = 0; i < thisRandom; i++)
		{
			buffer[i] = (uint8)r.nextInt({23, 96});
		}

		l.log(&x, EventType::SlotChange, buffer, thisRandom);

	}
	l.flush();

	root.setLogger(nullptr);

	pc.stop();
}

void LoggerTest::testQueue()
{
	beginTest("Testing queue");

	RootObject root(nullptr);
	Logger l(root, 8192);
	root.setLogger(&l);

	Queue queue(root, 0);

	MyTestQueuable s1(root);
	MyTestQueuable s2(root);
	MyTestQueuable s3(root);

	uint8 buffer[16];
	for(int i = 0; i < 16; i++)
		buffer[i] = i;

	queue.push(&s1, EventType::SlotChange, buffer, 1);
	queue.push(&s1, EventType::SlotChange, buffer + 2, 2);
	queue.push(&s1, EventType::SlotChange, buffer + 3, 3);

	l.flush();

	int numIterations = 0;

	queue.flush([&](const Queue::FlushArgument&)
	{
		numIterations++;
		return true;
	});

	expectEquals(numIterations, 3);

	NeverExecuted n1(root, "never1");
	NeverExecuted n2(root, "never2");

	Random r;

	queue.push(&n1, EventType::SlotChange, buffer, 0);
	queue.push(&s1, EventType::SlotChange, buffer, 0);
	queue.push(&s2, EventType::SlotChange, buffer, 0);
	queue.push(&n2, EventType::SlotChange, buffer, 0);
	queue.push(&n1, EventType::SlotChange, buffer, 0);

	queue.removeFirstMatchInQueue(&n2);
	queue.removeAllMatches(&n1);

	auto numIterationsToExpect = 2;
	numIterations = 0;

	queue.flush([&](const Queue::FlushArgument& f)
	{
		expect(dynamic_cast<NeverExecuted*>(f.source) == nullptr, "never was executed");
		numIterations++;
		return true;
	});

	expectEquals(numIterations, numIterationsToExpect, "all nevers removed properly");

	numIterationsToExpect = 0;
	numIterations = 0;

	for(int i = 0; i < 100; i++)
	{
		if(r.nextBool())
		{
			queue.push((r.nextBool() ? &n1 : &n2), EventType::SlotChange, buffer, 0);
		}
		else
		{
			queue.push(&s1, EventType::SlotChange, buffer, 0);
			numIterationsToExpect++;
		}
	}

	queue.invalidateQueuable(&n1, DanglingBehaviour::CloseGap);
	queue.invalidateQueuable(&n2, DanglingBehaviour::Undefined);

	queue.flush([&](const Queue::FlushArgument& f)
	{
		expect(dynamic_cast<NeverExecuted*>(f.source) == nullptr, "never was executed");
		numIterations++;
		return true;
	});

	expectEquals(numIterations, numIterationsToExpect, "all nevers removed dynamically");
	root.setLogger(nullptr);
}

void LoggerTest::testQueueResume()
{
	beginTest("test resuming of queue");
	RootObject root(nullptr);
	Logger l(root, 8192);
	root.setLogger(&l);

	Queue testQueue(root, 40);

	OwnedArray<MyTestQueuable> tests;

	uint8 i = 0;
	for(i = 0; i < 40; i++)
	{
		tests.add(new MyTestQueuable(root));
		testQueue.push(tests.getLast(), EventType::SlotChange, (uint8*)&i, 1);
	}

	BigInteger b;
		
	testQueue.flush([&](const Queue::FlushArgument& f)
	{
		auto v = *f.data;
		b.setBit(v, true);
		return true;
	}, Queue::FlushType::KeepData);

	

	expectEquals(b.countNumberOfSetBits(), 40, "skipped a iteration");
	b.clear();

	// This tests that KeepData retains the elements
	testQueue.flush([&](const Queue::FlushArgument& f)
	{
		auto v = *f.data;
		b.setBit(v, true);
		return true;
	}, Queue::FlushType::KeepData);

	expectEquals(b.countNumberOfSetBits(), 40, "skipped a iteration");
	b.clear();

	// This tests the abortion if you return false
	testQueue.flush([&](const Queue::FlushArgument& f)
	{
		auto v = *f.data;

		if(v == 20)
			return false;

		b.setBit(v, true);
		return true;
	}, Queue::FlushType::KeepData);

	expectEquals(b.countNumberOfSetBits(), 20, "abort didn't work");
	b.clear();

	// Now we test the pause function of the root manager
	testQueue.flush([&](const Queue::FlushArgument& f)
	{
		auto v = *f.data;

		if(v == 19)
			root.setState(State::Paused);

		b.setBit(v, true);
		return true;
	}, Queue::FlushType::Flush);

	expectEquals(b.countNumberOfSetBits(), 20, "pause didn't work");
	b.clear();

	// Now we test that calling flush while the root object is paused doesn't exeucte anything.
	testQueue.flush([&](const Queue::FlushArgument& f)
	{
		auto v = *f.data;
		
		b.setBit(v, true);
		return true;
	}, Queue::FlushType::Flush);

	expectEquals(b.countNumberOfSetBits(), 0, "paused root object didn't work");
	b.clear();

	root.setState(State::Running);
	
	expectEquals(b.countNumberOfSetBits(), 20, "work after resume");
	b.clear();

	root.setLogger(nullptr);
}

void LoggerTest::testSourceManager()
{
	RootObject root(nullptr);
	Logger lg(root, 1024);
	root.setLogger(&lg);
	SourceManager sm(root, IDs::source::automation);

	struct MySource: public Source
	{
		MySource(SourceManager& sm):
		  Source(sm, "my_source"),
		  helloSlot(*this, 13)
		{
			helloSlot.setNumSlots(8);
		};

		void sendHelloMessage()
		{
			helloSlot.sendChangeMessage(0, sendNotificationSync);
			helloSlot.sendChangeMessage(2, sendNotificationSync);
		}

		SlotSender helloSlot;
	};

	MySource src(sm);

	struct MyListener: public Listener
	{
		MyListener(RootObject& r):
		  Listener(r)
		{
			
		}

		void slotChanged(const ListenerData& d) override
		{
			changed = true;
		}

		~MyListener()
		{
			
		}

		void reset()
		{
			changed = false;
		}

		bool changed = false;
	};

	MyListener l(root);

	l.addListenerToAllSources(sm, sendNotificationSync);

	src.sendHelloMessage();

	expect(l.changed, "message didn't work");

	l.removeListener(sm);

	l.reset();

	src.sendHelloMessage();

	expect(!l.changed, "remove didn't work");

	root.setLogger(nullptr);
	
#if 0
	uint8 slots[2];
	slots[0] = 1;
	slots[1] = 2;
	l.addListenerToSingleSource(&src, slots, 2, sendNotificationAsync);
	
	l.addListenerToSubset(sm, [&](uint8** data)
	{
		auto offset = Listener::EventParser::writeSourcePointer(&src, data);
		return offset;
	}, sendNotificationSync);
#endif

	
}

static LoggerTest loggerTest;

void LoggerTest::runTest()
{
	testQueue();
	testLogger();
    testQueueResume();
	testSourceManager();
}


void CharPtrTest::expectStringResult(const StringBuilder& b, const String& e)
{
    expectEquals(String(b.get(), b.length()), e);
    expectEquals((int)b.length(), e.length());
}

void CharPtrTest::testStringBuilder()
{
    beginTest("test StringBuilder << operators");

    String s;

    StringBuilder b;
    int n = 1;
    b << n;
    s << String(n);
    expectStringResult(b, s);

    auto r = ", ";
    b << r;
    s << r;
    expectStringResult(b, s);

    String j("juce::String, ");
    b << j;
    s << j;
    expectStringResult(b, s);

    CharPtr c("CharPtr, ");
    b << c;
    s << (StringRef)c;
    expectStringResult(b, s);

    HashedCharPtr h("HashedCharPtr, ");
    b << h;
    s << (StringRef)h;
    expectStringResult(b, s);

    StringBuilder b2;
    b2 << b;
    expectStringResult(b2, s);

    {
	    struct DummySource: public Queueable
		{
			DummySource(RootObject& r):
			  Queueable(r),
			  id("dummy_source")
			{}
			  
			const HashedCharPtr id;
			HashedCharPtr getDispatchId() const override { return id; }
		};

		RootObject r(nullptr);
		DummySource ds(r);
		Queue::FlushArgument f;
		f.source = &ds;
		f.eventType = EventType::SlotChange;
		uint8 buffer[4];
		memset(buffer, 1, 4);
		buffer[0] = 70;
		buffer[1] = 90;
		f.data = buffer;
		f.numBytes = 4;

		b << f;
		s << "dummy_source:\tslotchange [ 70, 90, 1, 1 ] (4 bytes)";
		expectStringResult(b, s);
    }
	
	
}

static CharPtrTest charPtrTest;

} // dispatch
} // hise
#include "application.h"
#include "CException/CException.h"
#include "unit-test/unit-test.h"

static void callInvalidFunction() {
	((void(*)())(0xdeadbeef))();
}

extern volatile int TestingTheFallback;
extern volatile int TestingTheFallbackId;
extern bool __cexception_hangOnUnHandledGlobalException;

static volatile bool threadRan;
static volatile bool threadStage1;
static volatile bool threadStage2;
static volatile CEXCEPTION_T threadException;
static volatile CExceptionThreadInfo threadInfo;

static void setUp(void)
{
	threadRan = false;
	threadStage1 = false;
	threadStage2 = false;
	threadException = 0;
    CExceptionFrames[0].pFrame = NULL;
    TestingTheFallback = 0;
    TestingTheFallbackId = 0;
    __cexception_hangOnUnHandledGlobalException = false;
}

static void tearDown(void)
{
	__cexception_hangOnUnHandledGlobalException = true;
}

test(CException_Group2_CatchAHardwareException) {
	setUp();

	CEXCEPTION_T e;
	bool caught = false;
	Try {
		callInvalidFunction();
	} Catch(e) {
		caught = true;
	}

	//verify an exception was caught
	assertTrue(caught);

	//verify a hardware exception code
	assertEqual(EXCEPTION_HARDWARE, e);

	//verify data from hardware exception data (PC at exception point, call of function at 0xdeadbeef)
	assertEqual(CEXCEPTION_CURRENT_DATA[6], 0xdeadbeee);

	tearDown();
}

test(CException_Group1_SetNumberOfThreads) {
	setUp();

	//verify test is starting with the default number of threads (1)
	assertEqual(__cexception_get_number_of_threads(), 1);

	CEXCEPTION_T e;
	Try {
		CEXCEPTION_SET_NUM_THREADS(5);
	} Catch(e) {
		fail();
	}

	//verify the number was raised correctly
	assertEqual(__cexception_get_number_of_threads(), 5);

	tearDown();
}

static void exceptionCallback(CEXCEPTION_T e, CExceptionThreadInfo* info) {
	threadException = e;
	memcpy((void*)&threadInfo, info, sizeof(CExceptionThreadInfo));
}

static void nothingThread(void* arg) {
	threadRan = true;
	LOG_DEBUG(INFO, "Doing Nothing");
	delay(2000);
	END_THREAD();
}

test(CException_Group2_ThreadWithoutHandlePtr) {
	setUp();

	assertTestPass(CException_Group1_SetNumberOfThreads);

	CEXCEPTION_T e;
	Try {
		NEW_THREAD(nullptr, "TestThread", OS_THREAD_PRIORITY_DEFAULT, nothingThread, nullptr, OS_THREAD_STACK_SIZE_DEFAULT, exceptionCallback);
	} Catch(e) {
		fail();
	}

	//wait for thread to end
	delay(3000);

	//verify thread ran
	assertTrue((bool)threadRan);

	tearDown();
}

test(CException_Group2_ThreadWithHandlePtr) {
	setUp();

	assertTestPass(CException_Group1_SetNumberOfThreads);

	os_thread_t thread;

	CEXCEPTION_T e;
	Try {
		NEW_THREAD(&thread, "TestThread", OS_THREAD_PRIORITY_DEFAULT, nothingThread, nullptr, OS_THREAD_STACK_SIZE_DEFAULT, exceptionCallback);
	} Catch(e) {
		fail();
	}

	//verify thread
	assertNotEqual((uint32_t)thread, (uint32_t)nullptr);

	//wait for thread to end
	delay(3000);

	//verify thread ran
	assertTrue((bool)threadRan);

	tearDown();
}

static void noEndThread(void* arg) {
	threadRan = true;
	delay(2000);
}

test(CException_Group2_ThreadWithoutEnd) {
	setUp();

	assertTestPass(CException_Group1_SetNumberOfThreads);

	CEXCEPTION_T e;
	Try {
		NEW_THREAD(nullptr, "TestThread", OS_THREAD_PRIORITY_DEFAULT, noEndThread, nullptr, OS_THREAD_STACK_SIZE_DEFAULT, exceptionCallback);
	} Catch(e) {
		fail();
	}

	//wait for thread to end
	delay(3000);

	//verify thread ran
	assertTrue((bool)threadRan);

	tearDown();
}

static void throwThread(void* arg) {
	threadRan = true;
	delay(2000);
	threadStage1 = true;
	Throw(0xdead);
	threadStage2 = true;

}

test(CException_Group2_ThrowThreadAndCallback) {
	setUp();

	threadRan = false;

	assertTestPass(CException_Group1_SetNumberOfThreads);

	os_thread_t thread;


	CEXCEPTION_T e;
	Try {
		NEW_THREAD(&thread, "TestThread", OS_THREAD_PRIORITY_DEFAULT, throwThread, nullptr, OS_THREAD_STACK_SIZE_DEFAULT, exceptionCallback);
	} Catch(e) {
		fail();
	}

	//verify we got a thread
	assertNotEqual((uint32_t)thread, (uint32_t)nullptr);

	//wait for thread to start
	delay(1000);

	//verify thread started
	assertTrue((bool)threadRan);

	//wait for thread to end
	delay(3000);

	//verify execution passed stage 1
	assertTrue((bool)threadStage1);

	//verify code broke before stage 2
	assertFalse((bool)threadStage2);

	//verify exception
	assertEqual(threadException, 0xdead);

	tearDown();
}

static void throwHardwareExceptionThread(void* arg) {
	threadRan = true;
	delay(2000);
	threadStage1 = true;
	callInvalidFunction();
	threadStage2 = true;

}

test(CException_Group2_HWExceptionThread) {
	setUp();

	threadRan = false;

	assertTestPass(CException_Group1_SetNumberOfThreads);

	os_thread_t thread;


	CEXCEPTION_T e;
	Try {
		NEW_THREAD(&thread, "TestThread", OS_THREAD_PRIORITY_DEFAULT, throwHardwareExceptionThread, nullptr, OS_THREAD_STACK_SIZE_DEFAULT, exceptionCallback);
	} Catch(e) {
		fail();
	}

	//check to make sure we got a thread
	assertNotEqual((uint32_t)thread, (uint32_t)nullptr);

	//wait for thread to start
	delay(1000);

	//verify thread started
	assertTrue((bool)threadRan);

	//wait for thread to finish
	delay(3000);

	//verify stage 1 was reached
	assertTrue((bool)threadStage1);

	//verify code broke before stage 2
	assertFalse((bool)threadStage2);

	//verify exception
	assertEqual(threadException, EXCEPTION_HARDWARE);
	//verify exception data--[6] is PC in exception frame, and invalid function call is to 0xdeadbeef
	//This becomes 0xdeadbeee in the actual call
	assertEqual((unsigned int)threadInfo.exceptionData[6], 0xdeadbeee);

	tearDown();
}

test(CException_Group2_TooManyThreadsAndThreadCount) {
	setUp();

	assertTestPass(CException_Group1_SetNumberOfThreads);

	CEXCEPTION_T e = 0xffff;
	bool t1 = false;
	bool t2 = false;
	bool t3 = false;
	bool t4 = false;
	bool t5 = false;
	bool caught = false;
	Try {
		NEW_THREAD(nullptr, "TestThread1", OS_THREAD_PRIORITY_DEFAULT, nothingThread, nullptr, OS_THREAD_STACK_SIZE_DEFAULT, exceptionCallback);
		t1 = true;
		delay(100);
		NEW_THREAD(nullptr, "TestThread2", OS_THREAD_PRIORITY_DEFAULT, nothingThread, nullptr, OS_THREAD_STACK_SIZE_DEFAULT, exceptionCallback);
		t2 = true;
		delay(100);
		NEW_THREAD(nullptr, "TestThread3", OS_THREAD_PRIORITY_DEFAULT, nothingThread, nullptr, OS_THREAD_STACK_SIZE_DEFAULT, exceptionCallback);
		t3 = true;
		delay(100);
		NEW_THREAD(nullptr, "TestThread4", OS_THREAD_PRIORITY_DEFAULT, nothingThread, nullptr, OS_THREAD_STACK_SIZE_DEFAULT, exceptionCallback);
		t4 = true;
		delay(1000);
		//set uptest to make sure thread did not actually run
		threadRan = false;
		NEW_THREAD(nullptr, "TestThread5", OS_THREAD_PRIORITY_DEFAULT, nothingThread, nullptr, OS_THREAD_STACK_SIZE_DEFAULT, exceptionCallback);
		t5 = true;
		delay(100);
	} Catch(e) {
		caught = true;
	}

	//verify the code broke at the right place
	assertTrue(t1);
	assertTrue(t2);
	assertTrue(t3);
	assertTrue(t4);
	assertFalse(t5);

	//verify the caught exception
	assertTrue(caught);
	assertEqual(e, EXCEPTION_TOO_MANY_THREADS);

	//verify that the expected number of threads is reported
	assertEqual(__cexception_get_active_thread_count(), 4);

	//wait for the test threads to end
	delay(5000);

	//verify the threads have all reported ending
	assertEqual(__cexception_get_active_thread_count(), 0);

	//verify thread 5 was not actually started
	assertFalse((bool)threadRan);

	//make sure no exceptions hit the global handler
	assertEqual((int)TestingTheFallback, 0);

	tearDown();
}

test(CException_Group2_ThrowSetInvalidThreadCount) {
	setUp();

	assertTestPass(CException_Group1_SetNumberOfThreads);

	bool caught = false;
	CEXCEPTION_T e;
	Try {
		CEXCEPTION_SET_NUM_THREADS(1);
	} Catch(e) {
		caught = true;
	}

	//should throw if CEXCEPTION_SET_NUM_THREADS is called with a number lower than the current setting
	assertTrue(caught);
	assertEqual(e, EXCEPTION_INVALID_ARGUMENT);

	tearDown();
}

test(CException_Group2_WITH_LOCK_SAFE_Throw) {
	std::mutex mutex;
	bool caught = false;
	CEXCEPTION_T e;
	Try {
		WITH_LOCK_SAFE(mutex)
		{
			Throw(0xbc);
		} LOCK_SAFE_CLEANUP();
	} Catch(e) {
		caught = true;
	}

	//make sure catch was trigger and the right exception is present
	assertTrue(caught);
	assertEqual(e, 0xbc);

	//the lock should be available now, if the LOCK_SAFE_CLEANUP worked correctly
	bool lockIsAvailable = mutex.try_lock();
	assertTrue(lockIsAvailable);
}

test(CException_Group2_WITH_LOCK_SAFE_NoThrow) {
	std::mutex mutex;
	bool caught = false;
	bool lockIsAvailableInside;
	CEXCEPTION_T e;
	Try {
		WITH_LOCK_SAFE(mutex)
		{
			lockIsAvailableInside = mutex.try_lock();
			if(lockIsAvailableInside)
				mutex.unlock();
		} LOCK_SAFE_CLEANUP();
	} Catch(e) {
		caught = true;
	}

	//should have nothing to catch here
	assertFalse(caught);
	//the lock should not be available within the WITH_LOCK_SAFE block
	assertFalse(lockIsAvailableInside);
	//the lock should be available now, however, if the block was exited correctly
	bool lockIsAvailable = mutex.try_lock();
	assertTrue(lockIsAvailable);

}

#include "core_cm3.h"

test(CException_Group1_Activate_Hardware_Handlers) {
	setUp();

	uint32_t originalHandlerAddress = ((uint32_t*)SCB->VTOR)[3];

	CEXCEPTION_ACTIVATE_HW_HANDLERS();

	uint32_t newHandlerAddress = ((uint32_t*)SCB->VTOR)[3];

	//verify the hard fault handler address has changed.
	assertNotEqual(originalHandlerAddress, newHandlerAddress);

	tearDown();
}



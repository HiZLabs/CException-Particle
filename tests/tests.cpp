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
    CExceptionFrames[0].pFrame = NULL; //TODO: need to initialize all frames
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

	uint32_t* data = CEXCEPTION_CURRENT_DATA;
	uint32_t hfsr = data[8];
	uint32_t cfsr = data[9];

	assertTrue(caught);
	assertTrue(hfsr & (1 << 30));    //forced hard fault
	assertTrue(cfsr & (0x00000001)); //invalid instruction address

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
	delay(20);
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
	delay(30);

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
	delay(30);

	//verify thread ran
	assertTrue((bool)threadRan);

	tearDown();
}

static void noEndThread(void* arg) {
	threadRan = true;
	delay(20);
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
	delay(30);

	//verify thread ran
	assertTrue((bool)threadRan);

	tearDown();
}

static void throwThread(void* arg) {
	threadRan = true;
	delay(20);
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
	delay(10);

	//verify thread started
	assertTrue((bool)threadRan);

	//wait for thread to end
	delay(30);

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
	delay(20);
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
	delay(10);

	//verify thread started
	assertTrue((bool)threadRan);

	//wait for thread to finish
	delay(30);

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
		delay(1);
		NEW_THREAD(nullptr, "TestThread2", OS_THREAD_PRIORITY_DEFAULT, nothingThread, nullptr, OS_THREAD_STACK_SIZE_DEFAULT, exceptionCallback);
		t2 = true;
		delay(1);
		NEW_THREAD(nullptr, "TestThread3", OS_THREAD_PRIORITY_DEFAULT, nothingThread, nullptr, OS_THREAD_STACK_SIZE_DEFAULT, exceptionCallback);
		t3 = true;
		delay(1);
		NEW_THREAD(nullptr, "TestThread4", OS_THREAD_PRIORITY_DEFAULT, nothingThread, nullptr, OS_THREAD_STACK_SIZE_DEFAULT, exceptionCallback);
		t4 = true;
		delay(10);
		//set uptest to make sure thread did not actually run
		threadRan = false;
		NEW_THREAD(nullptr, "TestThread5", OS_THREAD_PRIORITY_DEFAULT, nothingThread, nullptr, OS_THREAD_STACK_SIZE_DEFAULT, exceptionCallback);
		t5 = true;
		delay(10);
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
	delay(50);

	//verify the threads have all reported ending
	assertEqual(__cexception_get_active_thread_count(), 0);

	//verify thread 5 was not actually started
	assertFalse((bool)threadRan);

	//make sure no exceptions hit the global handler
	assertEqual((int)TestingTheFallback, 0);

	tearDown();
}

test(CException_Group2_HighPriorityThread) {
	setUp();

	assertTestPass(CException_Group1_SetNumberOfThreads);

	CEXCEPTION_T e = 0xffff;
	bool caught = false;
	Try {
		NEW_THREAD(nullptr, "TestThread1", OS_THREAD_PRIORITY_CRITICAL, nothingThread, nullptr, OS_THREAD_STACK_SIZE_DEFAULT, exceptionCallback);
	} Catch(e) {
		caught = true;
	}

	//verify the caught exception
	assertFalse(caught);
	assertEqual(e, 0xffff);

	//verify that the expected number of threads is reported
	assertEqual(__cexception_get_active_thread_count(), 1);

	//wait for the test threads to end
	delay(50);

	//verify the threads have all reported ending
	assertEqual(__cexception_get_active_thread_count(), 0);

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

test(CException_Group2_BEGIN_LOCK_SAFE_Throw) {
	std::mutex mutex;
	bool caught = false;
	CEXCEPTION_T e;
	Try {
		BEGIN_LOCK_SAFE(mutex)
		{
			Throw(0xbc);
		} END_LOCK_SAFE();
	} Catch(e) {
		caught = true;
	}

	//make sure catch was trigger and the right exception is present
	assertTrue(caught);
	assertEqual(e, 0xbc);

	//the lock should be available now, if the END_LOCK_SAFE worked correctly
	bool lockIsAvailable = mutex.try_lock();
	assertTrue(lockIsAvailable);
}

test(CException_Group2_BEGIN_LOCK_SAFE_NoThrow) {
	std::mutex mutex;
	bool caught = false;
	bool lockIsAvailableInside;
	CEXCEPTION_T e;
	Try {
		BEGIN_LOCK_SAFE(mutex)
		{
			lockIsAvailableInside = mutex.try_lock();
			if(lockIsAvailableInside)
				mutex.unlock();
		} END_LOCK_SAFE();
	} Catch(e) {
		caught = true;
	}

	//should have nothing to catch here
	assertFalse(caught);
	//the lock should not be available within the BEGIN_LOCK_SAFE block
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


test(CException_Group2_HWFaultUnaligned) {
	setUp();

	assertTestPass(CException_Group1_Activate_Hardware_Handlers);

	bool caught = false;
	CEXCEPTION_T e;
	Try {
		//force unaligned usage fault
		__asm volatile (
				"push {r0-r2}\n"
				"movt r0, #0x0000 \n"
				"mov r0, #0x0001 \n"
				"ldm r0, {r1-r2} \n"
				"pop {r0-r2} \n"
		);

	} Catch(e) {
		caught = true;
	}

	uint32_t* data = CEXCEPTION_CURRENT_DATA;
	uint32_t hfsr = data[8];
	uint32_t cfsr = data[9];

	assertTrue(caught);
	assertTrue(hfsr & (1 << 30)); //forced hard fault
	assertTrue(cfsr & (1 << 24)); //unaligned


	tearDown();
}

test(CException_Group2_HWFaultDiv0) {
	setUp();

	assertTestPass(CException_Group1_Activate_Hardware_Handlers);

	bool caught = false;
	CEXCEPTION_T e;
	Try {
		volatile int i = 1;
		volatile int j = 0;
		i = i/j;
	} Catch(e) {
		caught = true;
	}

	uint32_t* data = CEXCEPTION_CURRENT_DATA;
	uint32_t hfsr = data[8];
	uint32_t cfsr = data[9];

	assertTrue(caught);
	assertTrue(hfsr & (1 << 30)); //forced hard fault
	assertTrue(cfsr & (1 << 25)); //div 0


	tearDown();
}

test(CException_Group3_HWFaultClearCFSR)
{
	assertTestPass(CException_Group1_Activate_Hardware_Handlers);
	assertTestPass(CException_Group2_HWFaultDiv0);
	assertTestPass(CException_Group2_HWFaultUnaligned);

	bool caught = false;
	CEXCEPTION_T e;
	Try {
		callInvalidFunction();
	} Catch(e) {
		caught = true;
	}

	uint32_t* data = CEXCEPTION_CURRENT_DATA;
	uint32_t cfsr = data[9];

	assertTrue(caught);
	assertFalse(cfsr & (1 << 25)); //div 0
	assertFalse(cfsr & (1 << 24)); //unaligned
}

void* __cexception_get_bl_target(void* func, uint32_t idx);

test(CException_Group1_GetBLTargetFromFunctionPointer)
{
	void* c0 = __cexception_get_bl_target((void*)throwHardwareExceptionThread, 0);
	void* c1 = __cexception_get_bl_target((void*)throwHardwareExceptionThread, 1);
	void* c2 = __cexception_get_bl_target((void*)__gthread_mutex_destroy, 0);
	assertEqual((uint32_t)delay, (uint32_t)c0);
	assertEqual((uint32_t)callInvalidFunction, (uint32_t)c1);
	assertEqual((uint32_t)os_mutex_destroy, (uint32_t)c2);
}

static void throwMyHandleThread(void* arg) {
	delay(20);
	Throw((uint32_t)__cexception_get_current_thread_handle());
}

test(CException_Group2_GetThreadHandle)
{
	setUp();

	assertTestPass(CException_Group1_GetBLTargetFromFunctionPointer);
	assertTestPass(CException_Group1_SetNumberOfThreads);

	bool caught = false;
	os_thread_t handle = nullptr;
	CEXCEPTION_T e;
	Try {
		NEW_THREAD(&handle, "Test Thread", OS_THREAD_PRIORITY_DEFAULT, throwMyHandleThread, nullptr, OS_THREAD_STACK_SIZE_DEFAULT, exceptionCallback);
	} Catch(e) {
		caught = true;
	}

	assertFalse(caught);
	assertNotEqual((uint32_t)handle, 0);
	delay(25);
	assertEqual((uint32_t)handle, (uint32_t)threadException);

	tearDown();
}

#define TEST_NAME_LEN 15

static void copyNameThread(void* arg)
{
	const char* name = __cexception_get_current_thread_name();
	LOG(TRACE, "Thread Name: %s 0x%02x%02x%02x%02x", name, name[0], name[1], name[2], name[3]);
	delay(5);
	strncpy((char*)arg, name, TEST_NAME_LEN);
}

test(CException_Group3_GetThreadName)
{
	setUp();

	assertTestPass(CException_Group2_GetThreadHandle);

	bool caught = false;
	os_thread_t handle = nullptr;
	char nameCopy[TEST_NAME_LEN];
	CEXCEPTION_T e;
	Try {
		NEW_THREAD(&handle, "Test Thread", OS_THREAD_PRIORITY_DEFAULT, copyNameThread, nameCopy, OS_THREAD_STACK_SIZE_DEFAULT, exceptionCallback);
	} Catch(e) {
		caught = true;
	}

	assertFalse(caught);
	assertNotEqual((uint32_t)handle, 0);
	delay(20);
	assertTrue(strcmp("Test Thread", nameCopy) == 0);

	tearDown();
}



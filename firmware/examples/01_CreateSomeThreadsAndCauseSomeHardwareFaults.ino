#include "application.h"
#include <CException/CException.h>

STARTUP(CEXCEPTION_ACTIVATE_HW_HANDLERS());
SerialLogHandler logger(LOG_LEVEL_ALL);

void callInvalidFunction() {
    ((void(*)())(0xdeadbeef))();
}

void testThread2(void *arg) {
    LOG(INFO, "Thread started");
    CEXCEPTION_T e;
    Try {
        LOG(INFO, "SUCCESS");
        Throw(EXCEPTION_OUT_OF_MEM+2);
    } Catch(e) {
        LOG(INFO, "CAUGHT 0x%08x", e);
    }
    delay(100);
    //force hard fault
    callInvalidFunction();
}

void testThread(void*arg) {
    CEXCEPTION_T e;
    Try {
        LOG(INFO, "SUCCESS");
        Throw(EXCEPTION_OUT_OF_MEM+1);
    } Catch(e) {
        LOG(INFO, "CAUGHT 0x%08x", e);
    }

    delay(10000);
}


void threadUnhandledExceptionCallback(CEXCEPTION_T e, CExceptionThreadInfo* threadInfo) {
    LOG(INFO, "Got exception details from thread %s", threadInfo->name);
}


void setup() {
    CEXCEPTION_SET_NUM_THREADS(15);
    CEXCEPTION_T e;
    os_thread_t thread;
    LOG(INFO, "First thread");
    delay(200);
    Try {
        NEW_THREAD(&thread, "Test Thread 1a", 4, testThread, nullptr, 4096, threadUnhandledExceptionCallback);
        delay(1000);
        NEW_THREAD(&thread, "Test Thread 1b", 4, testThread, nullptr, 4096, threadUnhandledExceptionCallback);
        delay(1000);
        NEW_THREAD(&thread, "Test Thread 1c", 4, testThread, nullptr, 4096, threadUnhandledExceptionCallback);
    } Catch(e) {
        LOG(ERROR, "Caught exception launching thread: 0x%8x", e);
    }
    delay(3000);
    LOG(INFO, "Second thread");
    delay(200);
    Try {
        NEW_THREAD(&thread, "Test Thread 2", 4, testThread2, nullptr, 4096, threadUnhandledExceptionCallback);
    } Catch(e) {
        LOG(ERROR, "Caught exception launching thread 2: 0x%8x", e);
    }
}

void loop() {
    delay(2000);
    LOG(INFO, "Still alive");
    delay(1000);
    LOG(INFO, "And now I kill myself");
    delay(100);
    callInvalidFunction();
}
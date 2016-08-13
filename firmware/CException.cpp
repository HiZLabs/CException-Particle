#include "CException.h"
#include "application.h"
#include <mutex>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
volatile CEXCEPTION_FRAME_T CExceptionFrames[CEXCEPTION_NUM_ID] = {{ 0 }};
static volatile void* TaskIds[CEXCEPTION_NUM_ID] = { 0 };
static std::mutex taskLock;
#pragma GCC diagnostic pop


extern "C" unsigned int __cexception_register_thread(void* threadHandle)
{
	std::lock_guard<std::mutex> lck(taskLock);
	for(int i = 1; i < CEXCEPTION_NUM_ID; i++)
	{
		if(TaskIds[i] == nullptr)
		{
			TaskIds[i] = threadHandle;
			return i;
		}

	}
	PANIC(OutOfHeap, "Out of exception thread space");
	return UINT32_MAX;
}

extern "C" void __cexception_unregister_current_thread() {
	std::lock_guard<std::mutex> lck(taskLock);
	unsigned int taskNumber = __cexception_get_current_task_number();
	TaskIds[taskNumber] = nullptr;
}

extern "C" void __cexception_unregister_thread(void* threadHandle) {
	std::lock_guard<std::mutex> lck(taskLock);
	unsigned int taskNumber = __cexception_get_task_number(threadHandle);
	TaskIds[taskNumber] = nullptr;
}


extern "C" unsigned int __cexception_get_task_number(void* threadHandle) {
	unsigned int i;
	for(i = 1; i < CEXCEPTION_NUM_ID; i++)
	{
		if(threadHandle == (void*)TaskIds[i])
			return i;
	}

//	return __cexception_register_new_thread__gthread_self());
	PANIC(InvalidCase, "Try/Catch in unregistered thread");
	return UINT32_MAX; //that should break it!
}

extern "C" unsigned int __cexception_get_current_task_number() {
	if(HAL_IsISR())
		return 0; //if try/catch is used in an ISR that interrupts an ISR using try/catch, this will be a problem.

	unsigned int i;
	for(i = 1; i < CEXCEPTION_NUM_ID; i++)
	{
		if(os_thread_is_current((void*)TaskIds[i]))
			return i;
	}

//	return __cexception_register_new_thread__gthread_self());
	PANIC(InvalidCase, "Try/Catch in unregistered thread");
	return UINT32_MAX; //that should break it!
}

extern "C" void __global_exception_handler(CEXCEPTION_T ExceptionID) __attribute__((weak));
extern "C" void __global_exception_handler(CEXCEPTION_T ExceptionID)
{
	LOG(PANIC, "Unhandled exception %d", ExceptionID);
	delay(10);
	PANIC(PureVirtualCall, "Unhandled exception %d", ExceptionID);
}

//------------------------------------------------------------------------------------------
//  Throw
//------------------------------------------------------------------------------------------
extern "C" void Throw(CEXCEPTION_T ExceptionID)
{
    unsigned int MY_ID = CEXCEPTION_GET_ID;
    CExceptionFrames[MY_ID].Exception = ExceptionID;
    if (CExceptionFrames[MY_ID].pFrame)
    {
        longjmp(*CExceptionFrames[MY_ID].pFrame, 1);
    }
    CEXCEPTION_NO_CATCH_HANDLER(ExceptionID);
}

//------------------------------------------------------------------------------------------
//  Explanation of what it's all for:
//------------------------------------------------------------------------------------------
/*
#define Try
    {                                                                   <- give us some local scope.  most compilers are happy with this
        jmp_buf *PrevFrame, NewFrame;                                   <- prev frame points to the last try block's frame.  new frame gets created on stack for this Try block
        unsigned int MY_ID = CEXCEPTION_GET_ID;                         <- look up this task's id for use in frame array.  always 0 if single-tasking
        PrevFrame = CExceptionFrames[CEXCEPTION_GET_ID].pFrame;         <- set pointer to point at old frame (which array is currently pointing at)
        CExceptionFrames[MY_ID].pFrame = &NewFrame;                     <- set array to point at my new frame instead, now
        CExceptionFrames[MY_ID].Exception = CEXCEPTION_NONE;            <- initialize my exception id to be NONE
        if (setjmp(NewFrame) == 0) {                                    <- do setjmp.  it returns 1 if longjump called, otherwise 0
            if (&PrevFrame)                                             <- this is here to force proper scoping.  it requires braces or a single line to be but after Try, otherwise won't compile.  This is always true at this point.

#define Catch(e)
            else { }                                                    <- this also forces proper scoping.  Without this they could stick their own 'else' in and it would get ugly
            CExceptionFrames[MY_ID].Exception = CEXCEPTION_NONE;        <- no errors happened, so just set the exception id to NONE (in case it was corrupted)
        }
        else                                                            <- an exception occurred
        { e = CExceptionFrames[MY_ID].Exception; e=e;}                  <- assign the caught exception id to the variable passed in.
        CExceptionFrames[MY_ID].pFrame = PrevFrame;                     <- make the pointer in the array point at the previous frame again, as if NewFrame never existed.
    }                                                                   <- finish off that local scope we created to have our own variables
    if (CExceptionFrames[CEXCEPTION_GET_ID].Exception != CEXCEPTION_NONE)  <- start the actual 'catch' processing if we have an exception id saved away
 */

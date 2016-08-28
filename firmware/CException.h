#ifndef _CEXCEPTION_H
#define _CEXCEPTION_H

#include <setjmp.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

//#ifdef CEXCEPTION_USE_CONFIG_FILE
//#include "CExceptionConfig.h"
//#endif

#define CEXCEPTION_NONE      			(0x5A5A5A5A)
#define EXCEPTION_OUT_OF_MEM 			(0x5A5A0000)
#define EXCEPTION_THREAD_START_FAILED	(0x5A5A0001)
#define EXCEPTION_TOO_MANY_THREADS      (0x5A5A0002)
#define EXCEPTION_HARDWARE				(0x5A5A5AFF)
#define EXCEPTION_INVALID_ARGUMENT      (0x5A5A0002)

#define CEXCEPTION_T        unsigned int

#define CEXCEPTION_MAX_NAME_LEN 14
#define CEXCEPTION_DATA_COUNT 10

struct CExceptionThreadInfo {
	void* handle;
	char name[CEXCEPTION_MAX_NAME_LEN+1];
	void(*exceptionCallback)(CEXCEPTION_T, CExceptionThreadInfo*);
	uint32_t exceptionData[CEXCEPTION_DATA_COUNT];
};

unsigned int __cexception_get_task_number(void* threadHandle);
unsigned int __cexception_get_current_task_number();
unsigned int __cexception_register_thread(void* threadHandle, const char* name, void(*exceptionCallback)(CEXCEPTION_T, CExceptionThreadInfo*));
void __cexception_unregister_thread(void* threadHandle);
void __cexception_unregister_current_thread();
void __cexception_set_number_of_threads(unsigned int num);
unsigned int __cexception_get_number_of_threads();
void __cexception_thread_create(void** thread, const char* name, unsigned int priority, void(*fun)(void*), void* thread_param, unsigned int stack_size, void(*cb)(CEXCEPTION_T, CExceptionThreadInfo*));
void __cexception_activate_handlers();
unsigned int __cexception_get_active_thread_count();
uint32_t* __cexception_get_current_thread_exception_data();
void* __cexception_get_current_thread_handle();
const char* __cexception_get_thread_name(const void* threadHandle);
const char* __cexception_get_current_thread_name();

#define CEXCEPTION_CURRENT_DATA __cexception_get_current_thread_exception_data()

#define CEXCEPTION_ACTIVATE_HW_HANDLERS() __cexception_activate_handlers()
#define CEXCEPTION_SET_NUM_THREADS(number) __cexception_set_number_of_threads(number)

#define CEXCEPTION_REGISTER_THREAD(threadHandle) __cexception_register_new_thread(threadHandle)
#define CEXCEPTION_UNREGISTER_THREAD(threadHandle) __cexceptionregister_end_thread(threadHandle)

#define CEXCEPTION_GET_ID __cexception_get_current_task_number()

#define NEW_THREAD(threadHandle_p, taskName, priority, taskFunction, taskArg, stackSize, exceptionCallback)   __cexception_thread_create(threadHandle_p, taskName, priority, taskFunction, taskArg, stackSize, exceptionCallback)

#define KILL_THREAD(threadHandle) do {      \
	    __cexception_unregister_thread(threadHandle); \
	    os_thread_cleanup(threadHandle); } while(0)

#define END_THREAD()	KILL_THREAD(nullptr)

#define BEGIN_LOCK_SAFE(lock) { std::lock_guard<decltype(lock)> __lock##lock((lock)); CEXCEPTION_T __lock_safe_e = CEXCEPTION_NONE; decltype(lock)* __lock_safe_lock = &(lock); Try
//when BEGIN_LOCK_SAFE is used in unregistering the current thread, the Catch will have an invalid id and might throw inadvertently
//as a result, we should re-check in the catch block.
#define END_LOCK_SAFE() Catch(__lock_safe_e) { } if(__lock_safe_e != CEXCEPTION_NONE) { __lock_safe_lock->unlock(); Throw(__lock_safe_e); } }

//These hooks allow you to inject custom code into places, particularly useful for saving and restoring additional state
#ifndef CEXCEPTION_HOOK_START_TRY
#define CEXCEPTION_HOOK_START_TRY
#endif
#ifndef CEXCEPTION_HOOK_HAPPY_TRY
#define CEXCEPTION_HOOK_HAPPY_TRY
#endif
#ifndef CEXCEPTION_HOOK_AFTER_TRY
#define CEXCEPTION_HOOK_AFTER_TRY
#endif
#ifndef CEXCEPTION_HOOK_START_CATCH
#define CEXCEPTION_HOOK_START_CATCH
#endif

//exception frame structures
typedef struct {
  jmp_buf* pFrame;
  CEXCEPTION_T volatile Exception;
} CEXCEPTION_FRAME_T;

//actual root frame storage (only one if single-tasking)
extern volatile CEXCEPTION_FRAME_T * volatile CExceptionFrames;

//Try (see C file for explanation)
#define Try                                                         \
    {                                                               \
        jmp_buf *PrevFrame, NewFrame;                               \
        unsigned int MY_ID = CEXCEPTION_GET_ID;                     \
        PrevFrame = CExceptionFrames[MY_ID].pFrame;                 \
        CExceptionFrames[MY_ID].pFrame = (jmp_buf*)(&NewFrame);     \
        CExceptionFrames[MY_ID].Exception = CEXCEPTION_NONE;        \
        CEXCEPTION_HOOK_START_TRY;                                  \
        if (setjmp(NewFrame) == 0) {                                \
            if (1)

//Catch (see C file for explanation)
#define Catch(e)                                                    \
            else { }                                                \
            CExceptionFrames[MY_ID].Exception = CEXCEPTION_NONE;    \
            CEXCEPTION_HOOK_HAPPY_TRY;                              \
        }                                                           \
        else                                                        \
        {                                                           \
        	e = CExceptionFrames[MY_ID].Exception;                  \
            (void)e;                                                \
            CEXCEPTION_HOOK_START_CATCH;                            \
        }                                                           \
        CExceptionFrames[MY_ID].pFrame = PrevFrame;                 \
        CEXCEPTION_HOOK_AFTER_TRY;                                  \
    }                                                               \
	if(CExceptionFrames[CEXCEPTION_GET_ID].Exception != CEXCEPTION_NONE)

//Throw an Error
void Throw(CEXCEPTION_T ExceptionID);

//Just exit the Try block and skip the Catch.
#define ExitTry() Throw(CEXCEPTION_NONE)

#ifdef __cplusplus
}   // extern "C"
#endif


#endif // _CEXCEPTION_H

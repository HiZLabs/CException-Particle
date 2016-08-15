#ifndef _CEXCEPTION_H
#define _CEXCEPTION_H

#include <setjmp.h>

#ifdef __cplusplus
extern "C"
{
#endif

#ifdef CEXCEPTION_USE_CONFIG_FILE
#include "CExceptionConfig.h"
#endif

#define CEXCEPTION_NONE      			(0x5A5A5A5A)
#define EXCEPTION_OUT_OF_MEM 			(0x5A5A0000)
#define EXCEPTION_THREAD_START_FAILED	(0x5A5A0001)
#define EXCEPTION_HARD_FAULT			(0xFFFFFF00)


#if PLATFORM_THREADING
#define CEXCEPTION_NUM_ID    (30) //multithreads!
#else
#define CEXCEPTION_NUM_ID    (1)
#endif

unsigned int __cexception_get_task_number(void* threadHandle);
unsigned int __cexception_get_current_task_number();
unsigned int __cexception_register_thread(void* threadHandle);
void __cexception_unregister_thread(void* threadHandle);
void __cexception_unregister_current_thread();
//void __cexception_set_number_of_threads(unsigned int num);
void __cexception_thread_create(void** thread, const char* name, unsigned int priority, void(*fun)(void*), void* thread_param, unsigned int stack_size);

#define CEXCEPTION_REGISTER_THREAD(threadHandle) __cexception_register_new_thread(threadHandle)
#define CEXCEPTION_UNREGISTER_THREAD(threadHandle) __cexceptionregister_end_thread(threadHandle)

#ifndef CEXCEPTION_GET_ID
#define CEXCEPTION_GET_ID __cexception_get_current_task_number()
#endif

#define NEW_THREAD(threadHandle_p, taskName, priority, taskFunction, taskArg, stackSize)   __cexception_thread_create(threadHandle_p, taskName, priority, taskFunction, taskArg, stackSize)

#define KILL_THREAD(threadHandle) do {      \
	    __cexception_unregister_current_thread(); \
	    os_thread_cleanup(threadHandle); } while(0)

#define END_THREAD()	KILL_THREAD(nullptr)

#define CEXCEPTION_T         unsigned int

#ifdef CEXCEPTION_DECLARE
#define CEXCEPTION_EX_VAR_DECL CEXCEPTION_T
#else
#define CEXCEPTION_EX_VAR_DECL
#endif

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
extern volatile CEXCEPTION_FRAME_T CExceptionFrames[];

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
        	CEXCEPTION_EX_VAR_DECL e = CExceptionFrames[MY_ID].Exception; \
            (void)e;                                                \
            CEXCEPTION_HOOK_START_CATCH;                            \
        }                                                           \
        CExceptionFrames[MY_ID].pFrame = PrevFrame;                 \
        CEXCEPTION_HOOK_AFTER_TRY;                                  \
    }                                                               \
	for(unsigned char __done = 0; !__done; __done = 1)              \
		for(CEXCEPTION_EX_VAR_DECL e = CExceptionFrames[CEXCEPTION_GET_ID].Exception; \
			e != CEXCEPTION_NONE && !__done; __done = 1)

//Throw an Error
void Throw(CEXCEPTION_T ExceptionID);

//Just exit the Try block and skip the Catch.
#define ExitTry() Throw(CEXCEPTION_NONE)

#ifdef __cplusplus
}   // extern "C"
#endif


#endif // _CEXCEPTION_H

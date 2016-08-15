#include "CException.h"
#include "application.h"
#include <mutex>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
volatile CEXCEPTION_FRAME_T CExceptionFrames[CEXCEPTION_NUM_ID] = {{ 0 }};
static volatile void* TaskIds[CEXCEPTION_NUM_ID] = { 0 };
static std::recursive_mutex taskLock;
#pragma GCC diagnostic pop


extern "C" unsigned int __cexception_register_thread(void* threadHandle)
{
	std::lock_guard<std::recursive_mutex> lck(taskLock);
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
	std::lock_guard<std::recursive_mutex> lck(taskLock);
	unsigned int taskNumber = __cexception_get_current_task_number();
	TaskIds[taskNumber] = nullptr;
}

extern "C" void __cexception_unregister_thread(void* threadHandle) {
	std::lock_guard<std::recursive_mutex> lck(taskLock);
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

	LOG(TRACE, "Try/Catch from unregistered thread");
	return 0; // if the thread is not registered, we'll just have to try our luck
}

extern "C" unsigned int __cexception_get_current_task_number() {
	unsigned int i;
	for(i = 1; i < CEXCEPTION_NUM_ID; i++)
	{
		if(os_thread_is_current((void*)TaskIds[i]))
			return i;
	}

	LOG(TRACE, "Try/Catch from unregistered thread");
	return 0; // if the thread is not registered, we'll just have to try our luck
}

extern "C" void __global_exception_handler(CEXCEPTION_T ExceptionID) __attribute__((weak));
extern "C" void __global_exception_handler(CEXCEPTION_T ExceptionID)
{
	SINGLE_THREADED_SECTION();

	LOG(ERROR, "Unhandled exception 0x%08x", ExceptionID);
	delay(100);
	unsigned int panicCode = ExceptionID < 15 ? ExceptionID : HardFault;
	panic_((ePanicCode)panicCode, nullptr, HAL_Delay_Microseconds);
}

struct CEXCEPTION_THREAD_FUNC_T {
	char name[15];
	void (*func)(void*);
	void* arg;
};

void __cexception_thread_wrapper(void * arg) {
	//wait until the lock is free--this will give the thread launcher a chance to register the thread,
	//even if this thread is a higher priority.
	taskLock.lock();
	taskLock.unlock();

	LOG(TRACE, "Thread launched, id %d", CEXCEPTION_GET_ID);

	CEXCEPTION_T e;
	CEXCEPTION_THREAD_FUNC_T* tip = ((CEXCEPTION_THREAD_FUNC_T*) arg);
	CEXCEPTION_THREAD_FUNC_T threadInfo = *tip;
	free(tip);

	Try	{
		threadInfo.func(threadInfo.arg);
	} Catch(e) {
		LOG(ERROR, "Uncaught exception 0x%08x in thread %s, thread killed", e, threadInfo.name);
	}

	END_THREAD(); //if user ends thread, this will never get called.
}

extern "C" void __cexception_thread_create(void** thread, const char* name, unsigned int priority, void(*fun)(void*), void* thread_param, unsigned int stack_size)
{
	std::lock_guard<std::recursive_mutex> lck(taskLock);
	void** thp = thread;
	void* th;
	thp = thp ? thp : &th;
	CEXCEPTION_THREAD_FUNC_T *ti = (CEXCEPTION_THREAD_FUNC_T*) malloc(sizeof(CEXCEPTION_THREAD_FUNC_T));
	if (!ti)
		Throw(EXCEPTION_OUT_OF_MEM);

	strncpy(ti->name, name, sizeof(ti->name));
	ti->name[sizeof(ti->name - 1)] = '\0';
	ti->func = fun;
	ti->arg = thread_param;

	os_thread_create(thp, name, priority, __cexception_thread_wrapper, ti, stack_size);

	if (*thp == nullptr)
	{
		free(ti);
		Throw(EXCEPTION_THREAD_START_FAILED);
	}

	__cexception_register_thread(*thp);
}

extern "C" __attribute__((externally_visible)) void __CException_HardFault_Handler_Stage2( uint32_t *pulFaultStackAddress) {
	LOG(TRACE, "HardFault Stack Dump");
	LOG(TRACE, " r0  = 0x%08x", pulFaultStackAddress[0]);
	LOG(TRACE, " r1  = 0x%08x", pulFaultStackAddress[1]);
	LOG(TRACE, " r2  = 0x%08x", pulFaultStackAddress[2]);
	LOG(TRACE, " r3  = 0x%08x", pulFaultStackAddress[3]);
	LOG(TRACE, " r12 = 0x%08x", pulFaultStackAddress[4]);
	LOG(TRACE, " lr  = 0x%08x", pulFaultStackAddress[5]);
	LOG(TRACE, " pc  = 0x%08x", pulFaultStackAddress[6]);
	LOG(TRACE, "xPSR = 0x%08x", pulFaultStackAddress[7]);
	Throw(EXCEPTION_HARD_FAULT);
}

static void CException_HardFault_Handler( void ) __attribute__( ( naked ) );
static void CException_HardFault_Handler( void ) {
	__asm volatile (
		" tst lr, #4                                                \n" //test bit 4 of LR
		" ite eq                                                    \n" //if
		" mrseq r0, msp                                             \n" //then r0 = msp
		" mrsne r0, psp                                             \n" //else r0 = psp
		" ldr r3,cexception_stage2_const                            \n" //load secondary handler address to r3
		" str r3,[r0, #24]                                          \n" //store function address to PC on stack
		" bx lr                                                     \n" //return from handler - will jump to PC on stack
		" cexception_stage2_const: .word __CException_HardFault_Handler_Stage2  \n"
	);
}



static const unsigned int __cexception_vector_table_count = 99;
static void(*__cexception_vector_table[__cexception_vector_table_count])() __attribute__ ((aligned (256)));

#ifdef STM32F2XX
#include "core_cm3.h"
static void override_hard_fault_handler() {
	ATOMIC_BLOCK()
	{
		void(**currentVectorTable)() = (void(**)())SCB->VTOR;					//get active vector table address
		if((uint32_t)currentVectorTable & 1 << 29)								//if vector table is in SRAM
			currentVectorTable[3] = CException_HardFault_Handler;				//then update hard fault in SRAM
		else if(currentVectorTable != (void(**)())&__cexception_vector_table)	//if vector table is in ROM
		{																		//copy table
			memcpy(__cexception_vector_table, currentVectorTable, sizeof(__cexception_vector_table));
			__cexception_vector_table[3] = CException_HardFault_Handler;		//set handler
			SCB->VTOR = ((uint32_t)&__cexception_vector_table & 0xffffff00);	//activate vector table
		}
	}
}
STARTUP(override_hard_fault_handler()); //install hard fault handler automatically
#endif

extern "C" void Throw(CEXCEPTION_T ExceptionID)
{
    unsigned int MY_ID = CEXCEPTION_GET_ID;
    CExceptionFrames[MY_ID].Exception = ExceptionID;
    if (CExceptionFrames[MY_ID].pFrame)
    {
        longjmp(*CExceptionFrames[MY_ID].pFrame, 1);
    }
    __global_exception_handler(ExceptionID);
}

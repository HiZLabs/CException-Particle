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
	return 0; // if the thread is not registered, we'll just have to try our luck with a catch-all
}

extern "C" unsigned int __cexception_get_current_task_number() {
	unsigned int i;
	for(i = 1; i < CEXCEPTION_NUM_ID; i++)
	{
		if(os_thread_is_current((void*)TaskIds[i]))
			return i;
	}

	LOG(TRACE, "Try/Catch from unregistered thread");
	return 0; // if the thread is not registered, we'll just have to try our luck with a catch-all
}

extern "C" void __global_exception_handler(CEXCEPTION_T ExceptionID) __attribute__((weak));
extern "C" void __global_exception_handler(CEXCEPTION_T ExceptionID)
{
	SINGLE_THREADED_SECTION();

	LOG(ERROR, "Unhandled exception 0x%08x", ExceptionID);
	delay(100); //give the message a chance to bubble out
	unsigned int panicCode = ExceptionID < 15 ? ExceptionID : HardFault;
	panic_((ePanicCode)panicCode, nullptr, HAL_Delay_Microseconds);
}
#define CEXCEPTION_MAX_NAME_LEN 14
struct CEXCEPTION_THREAD_FUNC_T {
	char name[CEXCEPTION_MAX_NAME_LEN+1];
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
	ti->name[CEXCEPTION_MAX_NAME_LEN] = '\0';
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

volatile uint32_t __cexception_fault_stack[8];

extern "C" __attribute__((externally_visible)) void __CException_Fault_Handler_Stage2(/* uint32_t *pulFaultStackAddress, uint32_t val*/) {
	uint32_t frame[8];
	memcpy(frame, (void*)__cexception_fault_stack, sizeof(__cexception_fault_stack));
	__asm (" cpsie i \n");
	LOG(TRACE, "HARDWARE EXCEPTION CAUGHT");
	LOG(TRACE, " r0  = 0x%08x", frame[0]);
	LOG(TRACE, " r1  = 0x%08x", frame[1]);
	LOG(TRACE, " r2  = 0x%08x", frame[2]);
	LOG(TRACE, " r3  = 0x%08x", frame[3]);
	LOG(TRACE, " r12 = 0x%08x", frame[4]);
	LOG(TRACE, " pc  = 0x%08x", frame[5]);
	LOG(TRACE, " lr  = 0x%08x", frame[6]);
	LOG(TRACE, " psr = 0x%08x", frame[7]);
	Throw(EXCEPTION_HARD_FAULT);
}

static  __attribute__( ( naked ) ) void CException_Fault_Handler( void ) {
	//OVERVIEW
	// 0. Disable interrupts
	// 1. Copy the exception handler stack frame into a global variable
	// 2. Overwrite the exception frame PC value with the address of the stage 2 handler
	// 3. Initiate exception return - Cortex will use the PC from the frame to branch to the stage 2 handler
	// Interrupts are disabled by this handler and then enabled by the stage 2 handler after the global data
	// is copied.
	//
	//DETAIL
	//At exception entry, Cortex pushes a stack frame consisting of R0, R1, R2, R3, R12, LR, PC, and PSR onto
	//the stack. Obtaining PC will be particularly interesting, because it will contain the instruction that
	//caused the exception.
	//
	//System services are not available until after the handler has returned, so the data needs to be copied
	//out and then the return address swizzled to go to a stage 2 handler so that the data can be logged with
	//the OS running.
	//
	//The Cortex exception handling mechanism requires that a special value is loaded into PC to return from
	//the exception handler. There is a different value depending on the processor/privilege mode and stack
	//at the point of the exception. At the start of the handler, LR contains the value needed to exit back
	//into the same mode.
	//
	//Some great reference:
	// - http://www.hitex.co.uk/fileadmin/uk-files/pdf/ARM%20Seminar%20Presentations%202013/Feabhas%20Developing%20a%20Generic%20Hard%20Fault%20handler%20for%20ARM.pdf
	//But of course, the greatest reference comes from the reference manual:
	// - http://www.st.com/content/ccc/resource/technical/document/programming_manual/5b/ca/8d/83/56/7f/40/08/CD00228163.pdf/files/CD00228163.pdf/jcr:content/translations/en.CD00228163.pdf
	//
	//When the processor takes an exception, unless the exception is a tail-chained or a late- arriving
	//exception, the processor pushes information onto the current stack. This operation is referred as stacking
	//and the structure of eight data words is referred as stack frame. The stack frame contains the following
	//information:
	//
	//- R0-R3, R12
	//- Return address
	//- PSR
	//- LR.
	//
	//The order of the registers on the stack seems to be slightly different than the standard AAPCS frame
	//suggested by the first link. Testing seems to confirm this--but also to be different from the listing in
	//the reference manual:
	// 0: R0
	// 1: R1
	// 2: R2
	// 3: R3
	// 4: R12
	// 5: PC
	// 6: LR
	// 7: PSR

	__asm (
		" cpsid i 													\n" //disable interrupts
		" tst lr, #4                                                \n" //test bit 4 of LR
		" ite eq                                                    \n" //if
		" mrseq r0, msp                                             \n" //then r0 = msp (main stack pointer)
		" mrsne r0, psp                                             \n" //else r0 = psp (process stack pointer)
		" ldr r3, cexception_stack_const 							\n" //load variable address
		" ldr r2, [r0, #0] 											\n" //load previous r0
		" str r2, [r3, #0] 											\n" //store to variable
		" ldr r2, [r0, #4] 											\n" //load previous r1
		" str r2, [r3, #4] 											\n" //store to variable
		" ldr r2, [r0, #8] 											\n" //load previous r2
		" str r2, [r3, #8] 											\n" //store to variable
		" ldr r2, [r0, #12] 										\n" //load previous r3
		" str r2, [r3, #12] 										\n" //store to variable
		" ldr r2, [r0, #16] 										\n" //load previous r12
		" str r2, [r3, #16] 										\n" //store to variable
		" ldr r2, [r0, #20] 										\n" //load previous pc
		" str r2, [r3, #20] 										\n" //store to variable
		" ldr r2, [r0, #24] 										\n" //load previous lr
		" str r2, [r3, #24] 										\n" //store to variable
		" ldr r2, [r0, #28] 										\n" //load previous psr
		" str r2, [r3, #28] 										\n" //store to variable
		" ldr r3, cexception_stage2_const                           \n" //load secondary handler address to r3
		" str r3, [r0, #24]                                         \n" //overwrite PC on stack frame
		" bx lr                                                     \n" //return from handler - will jump to PC on stack
		" cexception_stage2_const: .word __CException_Fault_Handler_Stage2 \n"
		" cexception_stack_const:  .word __cexception_fault_stack     \n"
	);
}



static const unsigned int __cexception_vector_table_count = 99;
static void(*__cexception_vector_table[__cexception_vector_table_count])() __attribute__ ((aligned (256)));

#ifdef STM32F2XX

/* g_pfnVectors:
  	  .word  _estack
  	  .word  Reset_Handler
  	  .word  NMI_Handler
  	  .word  HardFault_Handler
  	  .word  MemManage_Handler
  	  .word  BusFault_Handler
  	  .word  UsageFault_Handler
  	  ...                        */
#include "core_cm3.h"
static void override_fault_handlers() {
	ATOMIC_BLOCK()
	{
		void(**currentVectorTable)() = (void(**)())SCB->VTOR;					//get active vector table address
		if(((uint32_t)currentVectorTable & 1 << 29) == 0)						//if vector table is in ROM
		{																		//copy table
			memcpy(__cexception_vector_table, currentVectorTable, sizeof(__cexception_vector_table));
			SCB->VTOR = ((uint32_t)&__cexception_vector_table);	//activate vector table
		}

		currentVectorTable = (void(**)())SCB->VTOR;

		currentVectorTable[3] = CException_Fault_Handler;						//set HardFault_Handler
		currentVectorTable[4] = CException_Fault_Handler;						//set MemManage_Handler
		currentVectorTable[5] = CException_Fault_Handler;						//set BusFault_Handler
		currentVectorTable[6] = CException_Fault_Handler;						//set UsageFault_Handler

	}
}
STARTUP(override_fault_handlers()); //install hard fault handler automatically
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

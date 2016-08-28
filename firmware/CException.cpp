#include "CException.h"
#include "application.h"
#include "core_cm3.h"
#include <mutex>
#include "system_threading.h"
#include "logging.h"

LOG_SOURCE_CATEGORY("cexception");

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
static CEXCEPTION_FRAME_T DefaultCExceptionFrame = { 0 };
#pragma GCC diagnostic pop

static std::mutex taskLock;

static volatile unsigned int CException_Num_Tasks = 1;
volatile CEXCEPTION_FRAME_T * volatile CExceptionFrames = &DefaultCExceptionFrame;
static volatile CExceptionThreadInfo * volatile TaskIds = nullptr;


void* __cexception_get_bl_target(void* func, uint32_t idx) {
	//verify the source function is thumb
	if((uint32_t)func & 0x00000001)
	{
		uint16_t* f = (uint16_t*)((uint32_t)func & 0xfffffffe);
		//find long branch (thumb) instruction
		uint32_t i = 0;
		uint32_t j = 0;
		do
		{
			j++;
			while(((f[i] & 0xf000) != 0xf000) || ((f[i+1] & 0xf000) != 0xf000)) i++;
			if(idx >= j)
				i += 2;
		} while(idx >= j);
		uint16_t h = f[i] & 0x7ff;
		uint16_t l = f[i+1] & 0x7ff;
		int32_t o = h & 0x400 ? 0xff800000 : 0; //if negative
		o |= (h << 12) | (l << 1);
		uint32_t a = o + (uint32_t)&(f[i]) + ((uint32_t)func & 0x00000001) + 4;
		LOG(TRACE, "Finding BL instruction: f = 0x%08x, i = %d, fa = 0x%04x, fb = 0x%04x, h = 0x%04x, l = 0x%04x, o = 0x%08x, pc = 0x%08x, a = 0x%08x",
				(uint32_t)f, (uint32_t)i, (uint32_t)f[i], (uint32_t)f[i+1], (uint32_t)h, (uint32_t)l, (uint32_t)o, (uint32_t)&(f[i]), a);
		return (void*)a;
	}
	return nullptr;
}

static os_thread_t(*xTaskGetCurrentTaskHandle)() = nullptr;
extern "C" const void* dynalib_location_hal_concurrent;

os_thread_t __cexception_get_current_thread_handle() {
	void* thread_is_current = (((void**)dynalib_location_hal_concurrent)[2]);
	LOG_DEBUG(TRACE, "os_thread_is_current: 0x%08x", (uint32_t)thread_is_current);
	if(xTaskGetCurrentTaskHandle == nullptr)
		xTaskGetCurrentTaskHandle = (os_thread_t(*)())__cexception_get_bl_target(thread_is_current, 0);
	os_thread_t handle = (xTaskGetCurrentTaskHandle());
	LOG_DEBUG(TRACE, "xTaskGetCurrentTaskHandle = 0x%08x, xTaskGetCurrentTaskHandle() = 0x%08x", (uint32_t)xTaskGetCurrentTaskHandle, (uint32_t)handle);
	return handle;
}

extern "C" __attribute__((weak)) os_thread_t __gthread_self() {
	return __cexception_get_current_thread_handle();
}

static unsigned int __cexception_get_current_task_number_internal() {
	unsigned int found = 0;
	for(unsigned int i = 1; i < CException_Num_Tasks && !found; i++)
	{
		if(os_thread_is_current(TaskIds[i].handle)) {
			found = i;
		}
	}
	return found;
}

extern "C" const char* __cexception_get_current_thread_name() {
	return __cexception_get_thread_name(__cexception_get_current_thread_handle());
}

extern "C" const char* __cexception_get_thread_name(const void* threadHandle)
{
	const char* name = (const char*)((uint32_t)__gthread_self() + 0x34);
//	int i;
//	for(i = 0; i < 20; i++)
//	{
//		if(name[i] > 20 && name[i] < 127)
//			break;
//	}
//	if(i < 20 && strlen(name+i) < 20)
//		return name+i;
	if(strlen(name) < 20)
		return name;
	else
		return "NO NAME";
}

uint32_t* __cexception_get_current_thread_exception_data() {
	return (uint32_t*)&TaskIds[__cexception_get_current_task_number_internal()].exceptionData;
}

static void dump_thread_list(unsigned int idToHighlight) {
	uint32_t lastPrinted = 0;
	for(;;)
	{
		uint32_t nextPrinted = UINT32_MAX;
		uint32_t nextIndex = 0;
		for(unsigned int i = 0; i < CException_Num_Tasks; i++)
		{
			uint32_t handle = (uint32_t)(TaskIds[i].handle);
			if(handle > lastPrinted && handle < nextPrinted)
			{
				nextPrinted = handle;
				nextIndex = i;
			}
		}
		if(lastPrinted == nextPrinted || nextPrinted == UINT32_MAX)
			break;
		else {
			LOG(INFO, " Thread %u: %-15s @ 0x%08x%s", nextIndex, TaskIds[nextIndex].name, TaskIds[nextIndex].handle, nextIndex == idToHighlight ? " <<<<" : "");
			lastPrinted = nextPrinted;
		}
	}
}

unsigned int __cexception_get_number_of_threads() { return CException_Num_Tasks; }
unsigned int __cexception_get_active_thread_count() {
	unsigned int count = 0;
	for(unsigned int i = 1; i < CException_Num_Tasks; i++)
	{
		if(TaskIds[i].handle)
			count++;
	}
	return count;
}

extern "C" void __cexception_set_number_of_threads(unsigned int num) {
	BEGIN_LOCK_SAFE(taskLock)
	{
		if(num <= CException_Num_Tasks)
			Throw(EXCEPTION_INVALID_ARGUMENT);

		CEXCEPTION_FRAME_T* newFrames = (CEXCEPTION_FRAME_T*)malloc(num*sizeof(CEXCEPTION_FRAME_T));
		CExceptionThreadInfo* newTaskList = (CExceptionThreadInfo*)malloc((num)*sizeof(CExceptionThreadInfo));
		if(newFrames == nullptr || newTaskList == nullptr)
		{
			if(newFrames)
				free(newFrames);
			if(newTaskList)
				free(newTaskList);

			Throw(EXCEPTION_OUT_OF_MEM);
		}

		memset(newFrames, 0, (num)*sizeof(CEXCEPTION_FRAME_T));
		memcpy(newFrames, (void*)CExceptionFrames, CException_Num_Tasks * sizeof(CEXCEPTION_FRAME_T));

		memset(newTaskList, 0, (num)*sizeof(CExceptionThreadInfo));
		if(CException_Num_Tasks > 1)
			memcpy(newTaskList, (void*)TaskIds, (CException_Num_Tasks)*sizeof(CExceptionThreadInfo));

		CExceptionFrames = newFrames;
		TaskIds = newTaskList;
		CException_Num_Tasks = num;
	} END_LOCK_SAFE();
}

unsigned int __cexception_register_thread_internal(void* threadHandle, const char* name, void(*exceptionCallback)(CEXCEPTION_T,CExceptionThreadInfo*))
{
	for(unsigned int i = 1; i < CException_Num_Tasks; i++)
	{
		if(TaskIds[i].handle == nullptr)
		{
			TaskIds[i].handle = threadHandle;
			strncpy((char*)TaskIds[i].name, name, CEXCEPTION_MAX_NAME_LEN);
			TaskIds[i].name[CEXCEPTION_MAX_NAME_LEN] = 0;
			TaskIds[i].exceptionCallback = exceptionCallback;
			return i;
		}

	}

	Throw(EXCEPTION_OUT_OF_MEM);
	return UINT32_MAX;
}

extern "C" unsigned int __cexception_register_thread(void* threadHandle, const char* name, void(*exceptionCallback)(CEXCEPTION_T,CExceptionThreadInfo*))
{
	BEGIN_LOCK_SAFE(taskLock)
	{
		return __cexception_register_thread_internal(threadHandle, name, exceptionCallback);
	} END_LOCK_SAFE();
}

extern "C" void __cexception_unregister_current_thread() {
	BEGIN_LOCK_SAFE(taskLock)
	{
		unsigned int taskNumber = __cexception_get_current_task_number_internal();
		LOG(INFO, "Unregistering thread %d (%s @ 0x%08x)", taskNumber, TaskIds[taskNumber].name, TaskIds[taskNumber].handle);

		TaskIds[taskNumber].handle = nullptr;
	} END_LOCK_SAFE();
}

extern "C" void __cexception_unregister_thread(void* threadHandle) {
	if(threadHandle)
	{
		BEGIN_LOCK_SAFE(taskLock)
		{
			unsigned int taskNumber = __cexception_get_task_number(threadHandle);
			LOG(INFO, "Unregistering thread %d (%s @ 0x%08x)", taskNumber, TaskIds[taskNumber].name, TaskIds[taskNumber].handle);

			TaskIds[taskNumber].handle = nullptr;
		} END_LOCK_SAFE();
	}
	else
		__cexception_unregister_current_thread();
}


extern "C" unsigned int __cexception_get_task_number(void* threadHandle) {
	unsigned int found = 0;
	for(unsigned int i = 1; i < CException_Num_Tasks && !found; i++)
	{
		if(threadHandle == TaskIds[i].handle) {
			found = i;
		}
	}

//	LOG(TRACE, "Found id %d", found);

	if(!found) // if the thread is not registered, we'll just have to try our luck with a catch-all
		LOG_DEBUG(TRACE, "Thread not registered, using default frame");
	return found;
}

extern "C" unsigned int __cexception_get_current_task_number() {
	unsigned int found = __cexception_get_current_task_number_internal();
	if(!found) // if the thread is not registered, we'll just have to try our luck with a catch-all
		LOG_DEBUG(TRACE, "Thread not registered, using default frame");
	return found;
}


__attribute__((weak)) bool __cexception_internal_global_handler(CEXCEPTION_T e) {
	return true;
}

extern "C" __attribute__((weak)) void CException_Global_Handler(CEXCEPTION_T ExceptionID)
{
	SINGLE_THREADED_SECTION();

	LOG(ERROR, "Unhandled exception 0x%08x", ExceptionID);
	if(__cexception_internal_global_handler(ExceptionID)) {
		LOG(ERROR, "Halting application", ExceptionID);
		delay(100); //give the message a chance to bubble out
		unsigned int panicCode = ExceptionID < 15 ? ExceptionID : HardFault;
		panic_((ePanicCode)panicCode, nullptr, HAL_Delay_Microseconds);
	}
}

struct CEXCEPTION_THREAD_FUNC_T {
	void (*func)(void*);
	void* arg;
};

void* system_internal(int item, void* reserved);
#define INVOKE_ASYNC(threadp, lambda) do { auto __lambda = lambda; if(threadp->isStarted() && !threadp->isCurrentThread()) threadp->invoke_async(FFL(__lambda)); else __lambda(); } while(0)


static void __cexception_thread_wrapper(void * arg) {
	//wait until the lock is free--this will give the thread launcher a chance to register the thread,
	//even if this thread is a higher priority.
	//TODO: figure out what along this execution path forces the stack to be large
	{ std::lock_guard<decltype(taskLock)> lck(taskLock); }

	unsigned int myId = __cexception_get_current_task_number_internal();
	ActiveObjectThreadQueue* SystemThread = ((ActiveObjectThreadQueue*)system_internal(1, nullptr));
	INVOKE_ASYNC(SystemThread, [myId]()
	{
		LOG(INFO, "Thread %d (%s @ 0x%08x) started", myId, TaskIds[myId].name, TaskIds[myId].handle);
	});


	CEXCEPTION_T e;
	CEXCEPTION_THREAD_FUNC_T* tip = ((CEXCEPTION_THREAD_FUNC_T*) arg);
	CEXCEPTION_THREAD_FUNC_T threadInfo = *tip;
	free(tip);

	Try	{
		threadInfo.func(threadInfo.arg);
	} Catch(e) {
		volatile bool done = 1;
		INVOKE_ASYNC(SystemThread, [&]()
		{
			if(TaskIds[myId].exceptionCallback)
				TaskIds[myId].exceptionCallback(e, (CExceptionThreadInfo*)&TaskIds[myId]);
			LOG(ERROR, "Exception 0x%08x not handled in thread %d (%s @ 0x%08x).", e, myId, TaskIds[myId].name, TaskIds[myId].handle);
			LOG(ERROR, "Thread %d terminated. **WARNING: dynamic or external resources are not cleaned up**", myId);

			dump_thread_list(myId);
			done = true;
		});

		while(!done) delay(10);
		delay(1);
	}

	END_THREAD(); //if user ends thread, this will never get called #notaproblem
}

extern "C" void __cexception_thread_create(void** thread, const char* name, unsigned int priority,
		void(*fun)(void*), void* thread_param, unsigned int stack_size, void(*exceptionCallback)(CEXCEPTION_T, CExceptionThreadInfo*))
{
	if(__cexception_get_active_thread_count() >= (CException_Num_Tasks - 1))
		Throw(EXCEPTION_TOO_MANY_THREADS);

	void** thp = thread;
	void* th;
	thp = thp ? thp : &th;
	CEXCEPTION_THREAD_FUNC_T *ti = (CEXCEPTION_THREAD_FUNC_T*) malloc(sizeof(CEXCEPTION_THREAD_FUNC_T));
	if (!ti)
		Throw(EXCEPTION_OUT_OF_MEM);

	ti->func = fun;
	ti->arg = thread_param;


	BEGIN_LOCK_SAFE(taskLock)
	{
		os_thread_create(thp, name, priority, __cexception_thread_wrapper, ti, stack_size+256);

		if (*thp == nullptr)
		{
			free(ti);
			Throw(EXCEPTION_THREAD_START_FAILED);
		}

		__cexception_register_thread_internal(*thp, name, exceptionCallback);
	} END_LOCK_SAFE();
}

volatile uint32_t __cexception_fault_stack[CEXCEPTION_DATA_COUNT];

extern "C" void CException_Fault_Handler() {
	volatile uint32_t* exceptionData = TaskIds[__cexception_get_current_task_number_internal()].exceptionData;
	memcpy((void *)exceptionData, (const void*)__cexception_fault_stack, sizeof(__cexception_fault_stack));
	__asm (" cpsie if \n");
	LOG(ERROR, "HARDWARE EXCEPTION CAUGHT");
	LOG(ERROR, "r0   = 0x%08x", exceptionData[0]);
	LOG(ERROR, "r1   = 0x%08x", exceptionData[1]);
	LOG(ERROR, "r2   = 0x%08x", exceptionData[2]);
	LOG(ERROR, "r3   = 0x%08x", exceptionData[3]);
	LOG(ERROR, "r12  = 0x%08x", exceptionData[4]);
	LOG(ERROR, "lr   = 0x%08x", exceptionData[5]);
	LOG(ERROR, "pc   = 0x%08x", exceptionData[6]);
	LOG(ERROR, "psr  = 0x%08x", exceptionData[7]);
	LOG(ERROR, "hfsr = 0x%08x", exceptionData[8]);
	LOG(ERROR, "cfsr = 0x%08x", exceptionData[9]);
	Throw(EXCEPTION_HARDWARE);
}

static  __attribute__( ( naked ) ) void __CException_Fault_Handler( void ) {
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
	//Also the reference manual:
	// - http://www.st.com/content/ccc/resource/technical/document/programming_manual/5b/ca/8d/83/56/7f/40/08/CD00228163.pdf/files/CD00228163.pdf/jcr:content/translations/en.CD00228163.pdf

	__asm (
		" cpsid if 													\n" //disable interrupts
		" tst lr, #4                                                \n" //compare lr to 0x4
		" ite eq                                                    \n" //if equal then else
		" mrseq r0, msp                                             \n" //then r0 = msp (main stack pointer)
		" mrsne r0, psp                                             \n" //else r0 = psp (process stack pointer)
		" ldr r3, cexception_stack_const                            \n" //load variable address
		" push {r4-r11}                                             \n" //save state - probably not necessary, since the code that was executing is now dead
		" ldm r0, {r4-r11}                                          \n" //load the exception stack frame into r4-r11
		" stm r3, {r4-r11}                                          \n" //store the data back to the global variable
		" pop {r4-r11}                                              \n" //restore state - again, probably unneeded, but who knows what gcc might do
	);

	__cexception_fault_stack[8] = SCB->HFSR;
	__cexception_fault_stack[9] = SCB->CFSR;
	//clear CFSR
	SCB->CFSR = 0xffffffff;

	__asm (
		" ldr r3, cexception_stage2_const                           \n" //load secondary handler address to r3
		" str r3, [r0, #24]                                         \n" //overwrite pc on stack frame
		" bx lr                                                     \n" //trigger handler return - will reset proc mode and branch to pc on stack - necessary because of __attribute__((naked))
		" cexception_stage2_const: .word CException_Fault_Handler   \n" //function address
		" cexception_stack_const:  .word __cexception_fault_stack   \n" //variable address
	);
}

//#ifdef STM32_DEVICE

/* g_pfnVectors:
  	  .word  _estack
  	  .word  Reset_Handler
  	  .word  NMI_Handler
  	  .word  HardFault_Handler
  	  .word  MemManage_Handler
  	  .word  BusFault_Handler
  	  .word  UsageFault_Handler
  	  ...                        */

static const uint32_t __cexception_vector_table_count = 99;
static void(*__cexception_vector_table[__cexception_vector_table_count])() __attribute__ ((aligned (256)));

extern "C" void __cexception_activate_handlers() {
	ATOMIC_BLOCK()
	{
		void(**currentVectorTable)() = (void(**)())SCB->VTOR;					//get active vector table address
		if(((uint32_t)currentVectorTable & 1 << 29) == 0)						//if vector table is in ROM
		{																		//copy table
			memcpy(__cexception_vector_table, currentVectorTable, sizeof(__cexception_vector_table));
			SCB->VTOR = ((uint32_t)&__cexception_vector_table);	//activate vector table
		}

		currentVectorTable = (void(**)())SCB->VTOR;								//update the pointer if it was changed

		currentVectorTable[3] = __CException_Fault_Handler;						//set HardFault_Handler
		currentVectorTable[4] = __CException_Fault_Handler;						//set MemManage_Handler
		currentVectorTable[5] = __CException_Fault_Handler;						//set BusFault_Handler
		currentVectorTable[6] = __CException_Fault_Handler;						//set UsageFault_Handler

	}
}
//#endif

extern "C" void Throw(CEXCEPTION_T ExceptionID)
{
    unsigned int MY_ID = CEXCEPTION_GET_ID;
    CExceptionFrames[MY_ID].Exception = ExceptionID;
    if (CExceptionFrames[MY_ID].pFrame)
    {
        longjmp(*CExceptionFrames[MY_ID].pFrame, 1);
    }
    CException_Global_Handler(ExceptionID);
}

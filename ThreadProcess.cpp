#include "ThreadProcess.h"
#include <iostream>
#include <intrin.h>


template <typename GuardObject>
class LockGuard
{
public:
	explicit
		LockGuard(GuardObject& InObjRef) :
		ObjRef(InObjRef)
	{
		ObjRef.Lock();
	}

	~LockGuard()
	{
		ObjRef.UnLock();
	}

	LockGuard(const LockGuard& Other) = delete;
	LockGuard& operator=(const LockGuard&) = delete;

private:
	LockGuard() {}

private:
	GuardObject& ObjRef;
};

Thread* Thread::Create(Runnable* ObjectToRun,
	UINT32 InitStackSize,
	ThreadPriority InitPriority,
	UINT64 AffinityMask)
{

	Thread* NewThread = nullptr;
	NewThread = WindowsThread::CreateThread();

	if (NewThread)
	{
		if (!NewThread->PlatformInit(ObjectToRun, InitStackSize, InitPriority, AffinityMask))
		{
			delete NewThread;
			NewThread = nullptr;

			//log
		}
	}

	return NewThread;
}


bool WindowsThread::PlatformInit(Runnable* ObjectToRun,
	UINT32 InitStackSize,
	ThreadPriority InitPriority,
	UINT64 AffinityMask)
{
	static bool SetMainThreadPri = false;
	if (!SetMainThreadPri)
	{
		SetMainThreadPri = true;
		::SetThreadPriority(::GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
	}

	RunObject = ObjectToRun;
	ThreadAffinityMask = AffinityMask;


	//Create auto reset sync event
	SyncEvent = ::CreateEvent(NULL, false, 0, nullptr);

	ThreadHandle = ::CreateThread(NULL, InitStackSize, ThreadEntrance, this, STACK_SIZE_PARAM_IS_A_RESERVATION | CREATE_SUSPENDED, (DWORD*)&ThreadID);

	if (ThreadHandle == NULL)
	{
		RunObject = nullptr;
	}
	else
	{
		::SetThreadDescription(ThreadHandle, L"FacialShadowMapGeneratorThread");

		::ResumeThread(ThreadHandle);

		//Here will wait for Runnable's Init() finish
		::WaitForSingleObject(SyncEvent, INFINITE);

		SetThreadPriority(InitPriority);
	}

	return ThreadHandle != NULL;
}


bool WindowsThread::Kill(bool WaitUntilExit)
{

	if (RunObject)
	{
		RunObject->Stop();
	}

	if (WaitUntilExit == true)
	{
		WaitForSingleObject(ThreadHandle, INFINITE);
	}

	if (SyncEvent != NULL)
	{
		CloseHandle(SyncEvent);
		SyncEvent = NULL;
	}

	CloseHandle(ThreadHandle);
	ThreadHandle = NULL;



	return true;
}




UINT32 WindowsThread::RunWrapper()
{
	UINT32 Result = 0;

	::SetThreadAffinityMask(::GetCurrentThread(), (DWORD_PTR)ThreadAffinityMask);

	if (::IsDebuggerPresent())
	{
		Result = Run();
	}
	else
	{

		//Structured exception handling
		__try
		{
			Result = Run();
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			std::cerr << "Error occurred on running Thread !!" << std::endl;
			TerminateProcess(GetCurrentProcess(), 1);
		}
	}

	return Result;
}



UINT32 WindowsThread::Run()
{
	UINT32 Result = 1;

	if (RunObject->Init())
	{
		//Set waiting point here
		::SetEvent(SyncEvent);

		Result = RunObject->Run();

		RunObject->Exit();
	}
	else
	{
		//Set waiting point here
		::SetEvent(SyncEvent);
	}

	return Result;
}


static void* InterlockedExchangePtr(void** Dest, void* Exchange)
{
	return (void*)_InterlockedExchange64((long long*)Dest, (long long)Exchange);
}





ThreadProcesser::ThreadProcesser() :
	WriterThreadPtr(nullptr),
	StopTrigger(0),
	WorkingCounter(0),
	ReportCounter(0),
	Progress(0.0)
{

	InterlockedExchangePtr((void**)&WriterThreadPtr, WindowsThread::Create(this, 0, ThreadPriority::BelowNormal));

}


ThreadProcesser::~ThreadProcesser()
{
	if (WriterThreadPtr != nullptr)
	{
		delete WriterThreadPtr;
		WriterThreadPtr = nullptr;
	}
}


bool ThreadProcesser::Init()
{
	return true;
}


UINT32 ThreadProcesser::Run()
{
	while (StopTrigger.GetCounter() == 0)
	{
		if (WorkingCounter.GetCounter() > 0)
		{
			InternelDoRequest();
		}
		else if (ReportCounter.GetCounter() > 0)
		{
			Report();
		}
		else
		{
			::Sleep(0.05f);
		}
	}

	return 0;
}


void ThreadProcesser::Stop()
{
	StopTrigger.Increment();
}



bool ThreadProcesser::Kick(int RequestType, void* Data)
{
	if (Data == nullptr) return;

	LockGuard<WindowsCriticalSection> Lock(CriticalSection);

	if (IsWorking())
		return false;

	// Do Generate
	if (RequestType == 1)
	{

	}
	// Do Bake
	else if (RequestType == 2)
	{

	}
	else
	{
		return;
	}

	WorkingCounter.Increment();

	return true;
}


bool ThreadProcesser::IsWorking()
{
	LockGuard<WindowsCriticalSection> Lock(CriticalSection);
	return WorkingCounter.GetCounter() > 0;
}

float ThreadProcesser::CanGetResutl()
{
	LockGuard<WindowsCriticalSection> Lock(CriticalSection);
	return Progress;
}


void ThreadProcesser::InternelDoRequest()
{

		const int32 ThisThreadDataStartPos = DataStartPos.GetCounter();
		const int32 ThisThreadDataEndPos = DataEndPos.GetCounter();

		if (ThisThreadDataStartPos <= ThisThreadDataEndPos)
		{
			//Copy straight forward
			FileSerializerPtr->Serialize(RingBuffer.Begin() + ThisThreadDataStartPos, ThisThreadDataEndPos - ThisThreadDataStartPos);
		}
		else
		{
			//Need to copy in ring
			FileSerializerPtr->Serialize(RingBuffer.Begin() + ThisThreadDataStartPos, RingBuffer.CurrentNum() - ThisThreadDataStartPos);
			FileSerializerPtr->Serialize(RingBuffer.Begin(), ThisThreadDataEndPos);
		}

		DataStartPos.SetCounter(ThisThreadDataEndPos);
		WriteRequestCounter.Decrement();

		//If the time large than a interval, flush it
		if ((PlatformTime::Time_Seconds() - LastFlushTime) > FlushInterval)
			InternalFlush();

		//If there has flush requests, flush it
		if (FlushRequestCounter.GetCounter() > 0)
		{
			InternalFlush();
			FlushRequestCounter.Decrement();
		}

		Progress += 0.1;
}









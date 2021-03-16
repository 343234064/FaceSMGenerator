#pragma once

#include <windows.h>
#include <string>
#include <vector>
#include "FaceSMProcess.h"

/************************************
Base Thread interface
*************************************/
struct PlatformAffinity
{
	static const UINT64 GetNormalThradMask()
	{
		return 0xFFFFFFFFFFFFFFFF;
	}

	static const UINT64 GetMainThreadMask()
	{
		return 0xFFFFFFFFFFFFFFFF;
	}

	static const UINT64 GetRenderThreadMask()
	{
		return 0xFFFFFFFFFFFFFFFF;
	}
};

enum class ThreadPriority
{
	Normal = 0,

	AboveNormal,

	BelowNormal,

	Highest,

	Lowest
};

class Runnable
{
public:
	Runnable() {}
	virtual ~Runnable() {}

	virtual bool Init() { return true; }

	//Return exit code
	virtual UINT32 Run() { return 0; }

	virtual void Stop() {}
	virtual void Exit() {}
};


class WindowsCriticalSection
{
public:
	__forceinline WindowsCriticalSection()
	{
		InitializeCriticalSection(&CriticalSection);
		SetCriticalSectionSpinCount(&CriticalSection, 4000);
	}

	__forceinline ~WindowsCriticalSection()
	{
		DeleteCriticalSection(&CriticalSection);
	}


	__forceinline void Lock()
	{
		if (TryEnterCriticalSection(&CriticalSection) == 0)
		{
			EnterCriticalSection(&CriticalSection);
		}
	}


	__forceinline
		void UnLock()
	{
		LeaveCriticalSection(&CriticalSection);
	}


	__forceinline
		bool TryLock()
	{
		return (TryEnterCriticalSection(&CriticalSection)) ? true : false;
	}

	WindowsCriticalSection(const WindowsCriticalSection& Other) = delete;
	WindowsCriticalSection& operator=(const WindowsCriticalSection& Other) = delete;

private:
	CRITICAL_SECTION CriticalSection;
};



class AtomicCounter
{


public:
	AtomicCounter(INT32 InitValue = 0) : Counter(InitValue) {}
	AtomicCounter(const AtomicCounter& other)
	{
		Counter = other.GetCounter();
	}

	~AtomicCounter() {}

	AtomicCounter(AtomicCounter&&) = delete;
	AtomicCounter& operator=(const AtomicCounter& Other) = delete;
	AtomicCounter& operator=(AtomicCounter&&) = delete;

	INT32 operator=(INT32 Val)
	{
		InterlockedExchange((long*)&Counter, (long)Val);
		return GetCounter();
	}

	INT32 GetCounter() const
	{
		return InterlockedCompareExchange((long*)&const_cast<AtomicCounter*>(this)->Counter, 0, 0);
	}


	void SetCounter(INT32 Val)
	{
		InterlockedExchange((long*)&Counter, (long)Val);
	}


	void Reset()
	{
		InterlockedExchange((long*)&Counter, 0);
	}


	INT32 Increment()
	{
		return InterlockedIncrement((long*)&Counter);
	}


	INT32 Add(INT32 AddValue)
	{
		return InterlockedAdd((long*)&Counter, (long)AddValue);
	}


	INT32 Decrement()
	{
		return InterlockedDecrement((long*)&Counter);
	}


	INT32 Sub(INT32 SubValue)
	{
		return InterlockedAdd((long*)&Counter, (long)-SubValue);
	}

protected:
	volatile INT32 Counter;
};




class Thread
{
public:
	static Thread* Create(Runnable* ObjectToRun,
		UINT32 InitStackSize = 0,
		ThreadPriority InitPriority = ThreadPriority::Normal,
		UINT64 AffinityMask = PlatformAffinity::GetNormalThradMask());

	virtual ~Thread() {}


	virtual void SetThreadPriority(ThreadPriority Priority) = 0;
	virtual void Pause() = 0;
	virtual void Resume() = 0;
	virtual bool Kill(bool WaitUntilExit = true) = 0;
	virtual void WaitForComplete() = 0;

	virtual const UINT32 GetThreadID() const
	{
		return ThreadID;
	}

	virtual const std::string& GetThreadName() const
	{
		return ThreadName;
	}

protected:
	Thread() :
		RunObject(nullptr),
		ThreadAffinityMask(PlatformAffinity::GetNormalThradMask()),
		Priority(ThreadPriority::Normal),
		ThreadID(0)
	{}


	virtual bool PlatformInit(Runnable* ObjectToRun,
		UINT32 InitStackSize = 0,
		ThreadPriority InitPriority = ThreadPriority::Normal,
		UINT64 AffinityMask = PlatformAffinity::GetNormalThradMask()) = 0;


protected:
	std::string ThreadName;

	Runnable* RunObject;

	UINT64 ThreadAffinityMask;

	ThreadPriority Priority;

	UINT32 ThreadID;

};

class WindowsThread : public Thread
{
public:
	static Thread* CreateThread()
	{
		return new WindowsThread();
	}

	virtual ~WindowsThread()
	{
		if (ThreadHandle != NULL)
		{
			Kill(true);
		}
	}


	void SetThreadPriority(ThreadPriority PriorityToSet) override
	{
		Priority = PriorityToSet;

		INT32 PlatformPriority = 0;
		//Here remapped the the normal priority to above normal 
		switch (PriorityToSet)
		{
		case ThreadPriority::AboveNormal: PlatformPriority = THREAD_PRIORITY_HIGHEST; break;
		case ThreadPriority::BelowNormal: PlatformPriority = THREAD_PRIORITY_NORMAL; break;
		case ThreadPriority::Highest:     PlatformPriority = THREAD_PRIORITY_HIGHEST; break;
		case ThreadPriority::Lowest:      PlatformPriority = THREAD_PRIORITY_LOWEST; break;
		default:
			PlatformPriority = THREAD_PRIORITY_ABOVE_NORMAL;
		}

		::SetThreadPriority(ThreadHandle, PlatformPriority);
	}

	void Pause() override
	{
		::SuspendThread(ThreadHandle);
	}

	void Resume() override
	{
		::ResumeThread(ThreadHandle);
	}

	bool Kill(bool WaitUntilExit = true) override;

	void WaitForComplete() override
	{
		::WaitForSingleObject(ThreadHandle, INFINITE);
	}


protected:
	WindowsThread() :
		ThreadHandle(NULL),
		SyncEvent(NULL)
	{}

	UINT32 RunWrapper();
	UINT32 Run();

	bool PlatformInit(Runnable* ObjectToRun,
		UINT32 InitStackSize = 0,
		ThreadPriority InitPriority = ThreadPriority::Normal,
		UINT64 AffinityMask = PlatformAffinity::GetNormalThradMask()) override;

	static DWORD WINAPI ThreadEntrance(LPVOID Object)
	{
		return ((WindowsThread*)Object)->RunWrapper();
	}



protected:
	HANDLE ThreadHandle;
	HANDLE SyncEvent;
};


enum class RequestType
{
	None = -1,
	Generate = 0,
	Bake = 1
};


class ThreadProcesser :public Runnable
{
public:
	ThreadProcesser();
	~ThreadProcesser();

	/****Call in Thread****/
	bool Init() override;
	UINT32 Run() override;
	void Stop() override;

	/****Call in Client****/
	bool Kick(RequestType Type, std::vector<TextureData>& Quests);
	bool IsWorking();
	float GetResult(TextureData* Result);
	RequestType GetQuestType() { return Request; }

private:
	void InternelDoRequest();

private:
	WindowsThread* WriterThreadPtr;

	WindowsCriticalSection CriticalSection;

	AtomicCounter StopTrigger;
	AtomicCounter WorkingCounter;
	AtomicCounter ReportCounter;

	std::vector<TextureData> QuestList;
	std::vector<TextureData> ResultList;

	int CurrentQuestPos;
	int CurrentResultPos;

	float Progress;

	RequestType Request;

	SDFGenerator Generator;
};
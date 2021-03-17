#include "ThreadProcess.h"
#include <iostream>
#include <intrin.h>
#include <utility>

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
	return (void*)InterlockedExchange64((long long*)Dest, (long long)Exchange);
}




ThreadProcesser::ThreadProcesser() :
	WriterThreadPtr(nullptr),
	StopTrigger(0),
	WorkingCounter(0),
	ReportCounter(0),
	CurrentQuestPos(0),
	CurrentResultPos(0),
	Progress(0.0),
	Request(RequestType::None)
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
			//::Sleep(5);
		}
		else
		{
			::Sleep(10);
		}
	}

	return 0;
}


void ThreadProcesser::Stop()
{
	StopTrigger.Increment();
}




bool ThreadProcesser::Kick(RequestType Type, void* Data)
{
	if (IsWorking())
		return false;

	LockGuard<WindowsCriticalSection> Lock(CriticalSection);

	QuestList.clear();
	ResultList.clear();

	Request = Type;

	if (Request == RequestType::Generate) {
		std::vector<TextureData>* Quests = (std::vector<TextureData>*)Data;
		for (size_t i = 0; i < Quests->size(); i++)
		{
			QuestList.push_back(Quests->at(i));
		}
	}
	else if (Request == RequestType::Bake)
	{
		std::pair<BakeSettting, std::vector<TextureData>>* Quests = (std::pair<BakeSettting, std::vector<TextureData>>*)Data;
		BakeSettting& Setting = Quests->first;
		std::vector<TextureData>& TextureDatas = Quests->second;

		int Height = 0;
		int Width = 0;
		for (int i = 0; i < TextureDatas.size(); i++)
		{
			Height = TextureDatas[i].Height;
			Width = TextureDatas[i].Width;
			Baker.SetSourceTexture(TextureDatas[i].SDFData);
		}
		Baker.SetHeightAndWidth(Height, Width);

		Baker.SetOutputFileName(Setting.FileName);
		Baker.SetSampleTimes(Setting.SampleTimes);
		Baker.Prepare();

		QuestList.push_back(TextureData(0, Height, Width, nullptr));
	}
	
	
	CurrentQuestPos = 0;
	CurrentResultPos = 0;
	Progress = 0.0f;

	WorkingCounter.Increment();

	return true;
}


bool ThreadProcesser::IsWorking()
{
	LockGuard<WindowsCriticalSection> Lock(CriticalSection);
	bool a = WorkingCounter.GetCounter() > 0;
	bool b = CurrentResultPos <= ((int)ResultList.size() - 1);

	return a || b;
}

float ThreadProcesser::GetResult(TextureData* Result)
{
	if (Result == nullptr) return 0.0f;

	LockGuard<WindowsCriticalSection> Lock(CriticalSection);

	if (CurrentResultPos <= ((int)ResultList.size() - 1))
	{
		*Result = ResultList[CurrentResultPos];

		std::cout << "GetResult -> " << ResultList[CurrentResultPos].Index << "|" << ResultList[CurrentResultPos].Width << "x" << ResultList[CurrentResultPos].Height << std::endl;

		CurrentResultPos += 1;
	}
	else
	{
		Result ->Index = -1;
	}


	return Progress;
}


void ThreadProcesser::InternelDoRequest()
{
	if (QuestList.size() == 0) return;

	// Do Generate
	if (Request == RequestType::Generate)
	{
		TextureData& CurrentQuest = QuestList[CurrentQuestPos];

		std::cout << "Handling -> " << CurrentQuest.Index << " |" << CurrentQuest.Width << "x" << CurrentQuest.Height << std::endl;
		std::cout << "Generating..." << std::endl;
		
		unsigned char* output = (unsigned char*)malloc(4 * CurrentQuest.Width * CurrentQuest.Height * sizeof(unsigned char));
		Generator.Run(CurrentQuest.Width, CurrentQuest.Height, CurrentQuest.Data, &output);
		CurrentQuest.SDFData = output;

		{
			LockGuard<WindowsCriticalSection> Lock(CriticalSection);
			ResultList.push_back(CurrentQuest);
		}

		CurrentQuestPos += 1;
		Progress += 1.0f / QuestList.size();
		if (Progress >= 0.9998f)
		{
			Progress = 1.0f;
		}
		if (CurrentQuestPos >= (int)QuestList.size())
		{
			WorkingCounter.Decrement();
			std::cout << "All Done" << std::endl;
		}
	}
	// Do Bake
	else if (Request == RequestType::Bake)
	{
		Progress += Baker.RunStep();
		
		if (Progress >= 0.99998f)
		{
			Progress = 1.0f;
		}
		if (Baker.IsCompleted())
		{
			{
				LockGuard<WindowsCriticalSection> Lock(CriticalSection);
				TextureData NewData = TextureData(0, QuestList[0].Height, QuestList[0].Width, nullptr);
				NewData.SDFData = (unsigned char*)Baker.GetOutputImage();
				ResultList.push_back(NewData);
			}
			WorkingCounter.Decrement();
			std::cout << "All Done" << std::endl;
		}

	}
	else
	{
		std::cout << "RequestType error:  " << (int)Request << std::endl;
	}

	
}









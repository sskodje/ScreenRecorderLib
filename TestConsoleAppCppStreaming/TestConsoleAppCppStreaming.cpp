#include <iostream>
#include <conio.h>
#include <vcclr.h>
#include <atlbase.h>
#include "WebStream.h"

using namespace System::IO;
using namespace System;
using namespace ScreenRecorderLib;
using namespace System::Threading;
using namespace System::Threading::Tasks;
using namespace System::Diagnostics;

void PrintElapsedTime(Object ^obj);
void Rec_OnStatusChanged(Object ^sender, RecordingStatusEventArgs ^e);
void Rec_OnRecordingComplete(Object ^sender, RecordingCompleteEventArgs ^e);
void Rec_OnRecordingFailed(System::Object ^sender, ScreenRecorderLib::RecordingFailedEventArgs ^e);

constexpr int ESC = 27;
constexpr int ENTER = 13;

static bool _isRecording;
gcroot<Stopwatch ^> _stopWatch;


/** Convenience function to create a locally implemented COM instance without the overhead of CoCreateInstance.
The COM class does not need to be registred for construction to succeed. However, lack of registration can
cause problems if transporting the class out-of-process. */
template <class T>
static CComPtr<T> CreateLocalInstance() {
	// create an object (with ref. count zero)
	CComObject<T> *tmp = nullptr;
	if (FAILED(CComObject<T>::CreateInstance(&tmp)))
		throw std::runtime_error("CreateInstance failed");

	// move into smart-ptr (will incr. ref. count to one)
	return CComPtr<T>(static_cast<T *>(tmp));
}

int main()
{
	Recorder ^rec = Recorder::CreateRecorder();
	rec->OnRecordingFailed += gcnew System::EventHandler<ScreenRecorderLib::RecordingFailedEventArgs ^>(&Rec_OnRecordingFailed);
	rec->OnRecordingComplete += gcnew System::EventHandler<ScreenRecorderLib::RecordingCompleteEventArgs ^>(&Rec_OnRecordingComplete);
	rec->OnStatusChanged += gcnew System::EventHandler<ScreenRecorderLib::RecordingStatusEventArgs ^>(&Rec_OnStatusChanged);

	Console::WriteLine("Press ENTER to start recording or ESC to exit");

	while (true)
	{
		ConsoleKeyInfo info = Console::ReadKey(true);
		if (info.Key == ConsoleKey::Enter)
		{
			break;
		}
		else if (info.Key == ConsoleKey::Escape)
		{
			return 0;
		}
	}
	auto ws = CreateLocalInstance<WebStream>();
	
	//auto stream = reinterpret_cast<IStream>(ws.p);
	//rec->Record(Path::ChangeExtension(Path::GetTempFileName(), ".mp4"));
	auto cts = gcnew CancellationTokenSource();
	auto token = cts->Token;

	Task::Factory->StartNew((Action<Object ^> ^)gcnew Action<Object ^>(&PrintElapsedTime), token);

	while (true)
	{
		ConsoleKeyInfo info = Console::ReadKey(true);
		if (info.Key == ConsoleKey::Escape)
		{
			break;
		}
	}
	cts->Cancel();
	rec->Stop();
	Console::WriteLine();
	Console::ReadKey();
	return 0;
}


void PrintElapsedTime(Object ^obj) {
	CancellationToken ^token = (CancellationToken ^)obj;
	while (true) {
		if (token->IsCancellationRequested)
			return;
		if (_isRecording)
		{
			Console::Write(String::Format("\rElapsed: {0}s:{1}ms", _stopWatch->Elapsed.Seconds, _stopWatch->Elapsed.Milliseconds));
		}
		Thread::Sleep(10);
	}
}

void Rec_OnStatusChanged(Object ^sender, RecordingStatusEventArgs ^e)
{
	switch (e->Status)
	{
		case RecorderStatus::Idle:
			break;
		case RecorderStatus::Recording:
			_stopWatch = gcnew Stopwatch();
			_stopWatch->Start();
			_isRecording = true;
			Console::WriteLine("Recording started");
			Console::WriteLine("Press ESC to stop recording");
			break;
		case RecorderStatus::Paused:
			Console::WriteLine("Recording paused");
			break;
		case RecorderStatus::Finishing:
			Console::WriteLine("Finishing encoding");
			break;
		default:
			break;
	}
}

void Rec_OnRecordingComplete(Object ^sender, RecordingCompleteEventArgs ^e)
{
	Console::WriteLine("Recording completed");
	_isRecording = false;
	_stopWatch->Stop();
	Console::WriteLine(String::Format("File: {0}", e->FilePath));
	Console::WriteLine();
	Console::WriteLine("Press any key to exit");
}

void Rec_OnRecordingFailed(System::Object ^sender, ScreenRecorderLib::RecordingFailedEventArgs ^e)
{
	Console::WriteLine("Recording failed with: " + e->Error);
	_isRecording = false;
	_stopWatch->Stop();
	Console::WriteLine();
	Console::WriteLine("Press any key to exit");
}
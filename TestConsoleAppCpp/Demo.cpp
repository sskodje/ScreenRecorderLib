#include <iostream>
#include <conio.h>
#include <vcclr.h>

using namespace System::IO;
using namespace System;
using namespace ScreenRecorderLib;
using namespace System::Threading;
using namespace System::Threading::Tasks;
using namespace System::Diagnostics;

void PrintElapsedTime(Object^ obj);
void Rec_OnStatusChanged(Object^ sender, RecordingStatusEventArgs^ e);
void Rec_OnRecordingComplete(Object^ sender, RecordingCompleteEventArgs^ e);
void Rec_OnRecordingFailed(System::Object^ sender, ScreenRecorderLib::RecordingFailedEventArgs^ e);

constexpr int ESC = 27;
constexpr int ENTER = 13;

static bool _isRecording;
gcroot<Stopwatch^> _stopWatch;

int main()
{
	Recorder^ rec = Recorder::CreateRecorder();
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
	auto timestamp = DateTime::Now.ToString("yyyy-MM-dd HH-mm-ss");
	auto filePath = Path::Combine(Path::GetTempPath(), "ScreenRecorder", timestamp, timestamp + ".mp4");
	rec->Record(filePath);
	auto cts = gcnew CancellationTokenSource();
	auto token = cts->Token;

	Task::Factory->StartNew((Action<Object^>^)gcnew Action<Object^>(&PrintElapsedTime), token);

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

void PrintElapsedTime(Object^ obj) {
	CancellationToken^ token = (CancellationToken^)obj;
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

void Rec_OnStatusChanged(Object^ sender, RecordingStatusEventArgs^ e)
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

void Rec_OnRecordingComplete(Object^ sender, RecordingCompleteEventArgs^ e)
{
	Console::WriteLine("Recording completed");
	_isRecording = false;
	_stopWatch->Stop();
	Console::WriteLine(String::Format("File: {0}", e->FilePath));
	Console::WriteLine();
	Console::WriteLine("Press any key to exit");
}

void Rec_OnRecordingFailed(System::Object^ sender, ScreenRecorderLib::RecordingFailedEventArgs^ e)
{
	Console::WriteLine("Recording failed with: " + e->Error);
	_isRecording = false;
	_stopWatch->Stop();
	Console::WriteLine();
	Console::WriteLine("Press any key to exit");
}
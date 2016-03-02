#include <Windows.h>
#include <locale.h>
#include <map>
#include "pxcmetadata.h"
#include "pxcsensemanager.h"
#include "pxcspeechrecognition.h"
#include "service/pxcsessionservice.h"

extern "C" {
	int RecognitionFlag = 0;
	wchar_t RecognitionLabel[1024];

	int num_source = 0;
	int num_module = 0;
	int num_language = 0;

	PXCSession  *g_session = 0;
	std::map<int, PXCAudioSource::DeviceInfo> g_devices;
	std::map<int, pxcUID> g_modules;

	class MyHandler : public PXCSpeechRecognition::Handler {
	public:

		MyHandler(HWND hWnd) :m_hWnd(hWnd) {}
		virtual ~MyHandler() {}

		virtual void PXCAPI OnRecognition(const PXCSpeechRecognition::RecognitionData *data) {
			if (data->scores[0].label<0) {
				wcscpy_s(RecognitionLabel, (pxcCHAR*)data->scores[0].sentence);
				RecognitionFlag = 1;
			}
		}

		virtual void PXCAPI OnAlert(const PXCSpeechRecognition::AlertData *data) {
		}

	protected:

		HWND m_hWnd;
	};

	PXCAudioSource* g_source = 0;
	PXCSpeechRecognition* g_vrec = 0;
	MyHandler* g_handler = 0;

	static DWORD WINAPI ProcessingThread(LPVOID arg)
	{
		HWND hWnd = (HWND)arg;
		RecognitionFlag = 0;

		/* Create an Audio Source */
		g_source = g_session->CreateAudioSource();

		/* Set Audio Source */
		PXCAudioSource::DeviceInfo dinfo = g_devices[num_source];
		pxcStatus sts = g_source->SetDevice(&dinfo);

		/* Set Module */
		PXCSession::ImplDesc mdesc = {};
		mdesc.iuid = g_modules[num_module];
		sts = g_session->CreateImpl<PXCSpeechRecognition>(&g_vrec);

		/* Set configuration according to user-selected language */
		PXCSpeechRecognition::ProfileInfo pinfo;
		int language_idx = num_language;
		g_vrec->QueryProfile(language_idx, &pinfo);
		sts = g_vrec->SetProfile(&pinfo);

		/* Set Command/Control or Dictation */
		// Dictation mode
		g_vrec->SetDictation();

		/* Start Recognition */
		g_handler = new MyHandler(hWnd);
		sts = g_vrec->StartRec(g_source, g_handler);

		return 0;
	}

	__declspec(dllexport) void Init(int m_num)
	{
		g_session = PXCSession::CreateInstance();

		/* Optional steps to send feedback to Intel Corporation to understand how often each SDK sample is used. */
		PXCMetadata * md = g_session->QueryInstance<PXCMetadata>();
		if (md)
		{
			pxcCHAR sample_name[] = L"Voice Recognition";
			md->AttachBuffer(PXCSessionService::FEEDBACK_SAMPLE_INFO, (pxcBYTE*)sample_name, sizeof(sample_name));
		}

		//Modules
		g_modules.clear();
		PXCSession::ImplDesc desc = {}, desc1;
		desc.cuids[0] = PXCSpeechRecognition::CUID;
		int i;
		for (i = 0;; i++) {
			if (g_session->QueryImpl(&desc, i, &desc1) < PXC_STATUS_NO_ERROR) break;
			g_modules[i] = desc1.iuid;
		}
		num_module = 0;

		//Source
		g_devices.clear();
		PXCAudioSource *source = g_session->CreateAudioSource();
		if (source) {
			source->ScanDevices();

			for (int i = 0;; i++) {
				PXCAudioSource::DeviceInfo dinfo = {};
				if (source->QueryDeviceInfo(i, &dinfo) < PXC_STATUS_NO_ERROR) break;
				g_devices[i] = dinfo;
			}

			source->Release();
		}
		num_source = m_num;

		//Language
		PXCSession::ImplDesc desc_l, desc1_l;
		memset(&desc_l, 0, sizeof(desc_l));
		desc_l.cuids[0] = PXCSpeechRecognition::CUID;
		if (g_session->QueryImpl(&desc_l, num_module /*ID_MODULE*/, &desc1_l) >= PXC_STATUS_NO_ERROR) {
			PXCSpeechRecognition *vrec;
			if (g_session->CreateImpl<PXCSpeechRecognition>(&desc1_l, &vrec) >= PXC_STATUS_NO_ERROR) {
				for (int k = 0;; k++) {
					PXCSpeechRecognition::ProfileInfo pinfo;
					if (vrec->QueryProfile(k, &pinfo) < PXC_STATUS_NO_ERROR) break;
					if (pinfo.language == PXCSpeechRecognition::LANGUAGE_JP_JAPANESE) num_language = k;
				}
				vrec->Release();
			}
		}

		//Start
		wchar_t cBuff[256] = L"";
		GetConsoleTitle(cBuff, 256);
		HWND dialogWindow = FindWindow(NULL, cBuff);
		CreateThread(0, 0, ProcessingThread, dialogWindow, 0, 0);
		Sleep(5);
	}

	__declspec(dllexport) void Stop()
	{
		if (g_vrec) g_vrec->StopRec();

		if (g_handler) delete g_handler, g_handler = 0;
		if (g_vrec) g_vrec->Release(), g_vrec = 0;
		if (g_source) g_source->Release(), g_source = 0;

		g_session->Release();
	}


	__declspec(dllexport) int GetRecognitionLabel(wchar_t* label)
	{
		if (RecognitionFlag==1)
		{
			wcscpy_s(label, 1024, RecognitionLabel);
			RecognitionFlag = 0;
			return wcslen(RecognitionLabel);
		}
		return RecognitionFlag;
	}
}
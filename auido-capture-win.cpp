#include "audio-capture.h"
#include <MMDeviceAPI.h>
#include <AudioClient.h>
#include "stdio.h"
#define REFTIMES_PER_SEC       (10000000)
#define REFTIMES_PER_MILLISEC  (10000)

#define SAFE_RELEASE(punk)  \
	if ((punk) != NULL)  \
				{ (punk)->Release(); (punk) = NULL; }

const CLSID CLSID_MMDeviceEnumerator = __uuidof(MMDeviceEnumerator);
const IID   IID_IMMDeviceEnumerator = __uuidof(IMMDeviceEnumerator);
const IID   IID_IAudioClient = __uuidof(IAudioClient);
const IID   IID_IAudioCaptureClient = __uuidof(IAudioCaptureClient);

#define MoveMemory RtlMoveMemory
#define CopyMemory RtlCopyMemory
#define FillMemory RtlFillMemory
#define ZeroMemory RtlZeroMemory

#define min(a,b)            (((a) < (b)) ? (a) : (b))

#define CAPTURE_MIC

struct WAVEHEADER
{
	DWORD   dwRiff;                     // "RIFF"
	DWORD   dwSize;                     // Size
	DWORD   dwWave;                     // "WAVE"
	DWORD   dwFmt;                      // "fmt "
	DWORD   dwFmtSize;                  // Wave Format Size
};

//  Static RIFF header, we'll append the format to it.
const BYTE WaveHeader[] =
{
	'R',   'I',   'F',   'F',  0x00,  0x00,  0x00,  0x00, 'W',   'A',   'V',   'E',   'f',   'm',   't',   ' ', 0x00, 0x00, 0x00, 0x00
};

//  Static wave DATA tag.
const BYTE WaveData[] = { 'd', 'a', 't', 'a' };

//
//  Write the contents of a WAV file.  We take as input the data to write and the format of that data.
//
bool WriteWaveFile(HANDLE FileHandle, const BYTE * Buffer, const size_t BufferSize, const WAVEFORMATEX * WaveFormat)
{
	DWORD waveFileSize = sizeof(WAVEHEADER) + sizeof(WAVEFORMATEX) + WaveFormat->cbSize + sizeof(WaveData) + sizeof(DWORD) + static_cast<DWORD>(BufferSize);
	BYTE* waveFileData = new BYTE[waveFileSize];
	BYTE* waveFilePointer = waveFileData;
	WAVEHEADER* waveHeader = reinterpret_cast<WAVEHEADER*>(waveFileData);

	if (waveFileData == NULL)
	{
		printf("Unable to allocate %d bytes to hold output wave data\n", waveFileSize);
		return false;
	}

	//
	//  Copy in the wave header - we'll fix up the lengths later.
	//
	CopyMemory(waveFilePointer, WaveHeader, sizeof(WaveHeader));
	waveFilePointer += sizeof(WaveHeader);

	//
	//  Update the sizes in the header.
	//
	waveHeader->dwSize = waveFileSize - (2 * sizeof(DWORD));
	waveHeader->dwFmtSize = sizeof(WAVEFORMATEX) + WaveFormat->cbSize;

	//
	//  Next copy in the WaveFormatex structure.
	//
	CopyMemory(waveFilePointer, WaveFormat, sizeof(WAVEFORMATEX) + WaveFormat->cbSize);
	waveFilePointer += sizeof(WAVEFORMATEX) + WaveFormat->cbSize;


	//
	//  Then the data header.
	//
	CopyMemory(waveFilePointer, WaveData, sizeof(WaveData));
	waveFilePointer += sizeof(WaveData);
	*(reinterpret_cast<DWORD*>(waveFilePointer)) = static_cast<DWORD>(BufferSize);
	waveFilePointer += sizeof(DWORD);

	//
	//  And finally copy in the audio data.
	//
	CopyMemory(waveFilePointer, Buffer, BufferSize);

	//
	//  Last but not least, write the data to the file.
	//
	DWORD bytesWritten;
	if (!WriteFile(FileHandle, waveFileData, waveFileSize, &bytesWritten, NULL))
	{
		printf("Unable to write wave file: %d\n", GetLastError());
		delete[]waveFileData;
		return false;
	}

	if (bytesWritten != waveFileSize)
	{
		printf("Failed to write entire wave file\n");
		delete[]waveFileData;
		return false;
	}
	delete[]waveFileData;
	return true;
}

//
//  Write the captured wave data to an output file so that it can be examined later.
//
void SaveWaveData(BYTE * CaptureBuffer, size_t BufferSize, const WAVEFORMATEX * WaveFormat)
{
	HRESULT hr = NOERROR;

	SYSTEMTIME st;
	GetLocalTime(&st);
	char waveFileName[_MAX_PATH] = { 0 };
	sprintf_s(waveFileName, ".\\WAS_%04d-%02d-%02d_%02d_%02d_%02d_%02d.wav",
		st.wYear, st.wMonth, st.wDay,
		st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);

	HANDLE waveHandle = CreateFile(waveFileName, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS,
		FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
		NULL);
	if (waveHandle != INVALID_HANDLE_VALUE)
	{
		if (WriteWaveFile(waveHandle, CaptureBuffer, BufferSize, WaveFormat))
		{
			printf("Successfully wrote WAVE data to %s\n", waveFileName);
		}
		else
		{
			printf("Unable to write wave file\n");
		}
		CloseHandle(waveHandle);
	}
	else
	{
		printf("Unable to open output WAV file %s: %d\n", waveFileName, GetLastError());
	}

}

static void dump_raw(BYTE * data, int size)
{
	static FILE* file = NULL;
	if (file == NULL)
	{
		char Buf[128];
		sprintf(Buf, "D:\\output.raw");
		file = fopen(Buf, "wb");
	}
	if (file != NULL) {
		fwrite(data, size, 1, file);
	}
}

static bool stillPlaying = true;

void audio_capture_start()
{
	HRESULT hr;

	IMMDeviceEnumerator* pEnumerator = NULL;
	IMMDevice* pDevice = NULL;
	IAudioClient* pAudioClient = NULL;
	IAudioCaptureClient* pCaptureClient = NULL;
	WAVEFORMATEX* pwfx = NULL;

	REFERENCE_TIME hnsRequestedDuration = REFTIMES_PER_SEC;
	UINT32         bufferFrameCount;
	UINT32         numFramesAvailable;

	BYTE* pData;
	UINT32         packetLength = 0;
	DWORD          flags;

	hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
	
	// ����ö�������Ƶ�豸
	// ����������ʱ���ȡ������������п��õ��豸����ָ������Ҫ�õ����Ǹ�����
	hr = CoCreateInstance(CLSID_MMDeviceEnumerator,
		NULL,
		CLSCTX_ALL,
		IID_IMMDeviceEnumerator,
		(void**)&pEnumerator);

#ifdef CAPTURE_MIC
	hr = pEnumerator->GetDefaultAudioEndpoint(eCapture, eConsole, &pDevice); // �ɼ���˷�
#else
	hr = pEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &pDevice);  // �ɼ�����
#endif 

	// ����һ���������ͨ�������Ի�ȡ������Ҫ��һ������
	hr = pDevice->Activate(IID_IAudioClient, CLSCTX_ALL, NULL, (void**)&pAudioClient);
	
	hr = pAudioClient->GetMixFormat(&pwfx);

	int nFrameSize = (pwfx->wBitsPerSample / 8) * pwfx->nChannels;

	// ��ʼ�������������������ָ��������󻺳������ȣ��������Ҫ��
	// Ӧ�ó���������ݿ�Ĵ�С�Լ���ʱ���̶�������ĳ�ʼ�������������ҿ����ĵ�����
#ifdef CAPTURE_MIC
	hr = pAudioClient->Initialize(
		AUDCLNT_SHAREMODE_SHARED,
		AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_NOPERSIST,
		hnsRequestedDuration,
		0,
		pwfx,
		NULL);
#else
	hr = pAudioClient->Initialize(
		AUDCLNT_SHAREMODE_SHARED,
		AUDCLNT_STREAMFLAGS_LOOPBACK, // ����ģʽ�£���Ƶengine�Ὣrending�豸���ڲ��ŵ���Ƶ���� ����һ�ݵ���Ƶ��endpoint buffer
									  // �����Ļ���WASAPI client���Բɼ���the stream.
									  // ���AUDCLNT_STREAMFLAGS_LOOPBACK�����ã�IAudioClient::Initialize�᳢��
									  // ��rending�豸����һ��capture buffer��
									  // AUDCLNT_STREAMFLAGS_LOOPBACKֻ��rending�豸��Ч��
									  // Initialize����AUDCLNT_SHAREMODE_SHAREDʱ�ſ���ʹ��, ����Initialize��ʧ�ܡ�
									  // Initialize�ɹ��󣬿�����IAudioClient::GetService�ɻ�ȡ��rending�豸��IAudioCaptureClient�ӿڡ�
		hnsRequestedDuration,
		0,
		pwfx,
		NULL);
#endif

	REFERENCE_TIME hnsStreamLatency;
	hr = pAudioClient->GetStreamLatency(&hnsStreamLatency);

	REFERENCE_TIME hnsDefaultDevicePeriod;
	REFERENCE_TIME hnsMinimumDevicePeriod;
	hr = pAudioClient->GetDevicePeriod(&hnsDefaultDevicePeriod, &hnsMinimumDevicePeriod);

	hr = pAudioClient->GetBufferSize(&bufferFrameCount);

	HANDLE hAudioSamplesReadyEvent = CreateEventEx(NULL, NULL, 0, EVENT_MODIFY_STATE | SYNCHRONIZE);

	hr = pAudioClient->SetEventHandle(hAudioSamplesReadyEvent);

	hr = pAudioClient->GetService(IID_IAudioCaptureClient, (void**)&pCaptureClient);

	hr = pAudioClient->Start();  // Start recording.

	HANDLE waitArray[3];
	waitArray[0] = hAudioSamplesReadyEvent;
	
	stillPlaying = TRUE;

	// Each loop fills about half of the shared buffer.
	while (stillPlaying)
	{
		DWORD waitResult = WaitForMultipleObjects(1, waitArray, FALSE, INFINITE);
		switch (waitResult)
		{
		case WAIT_OBJECT_0 + 0:     // _AudioSamplesReadyEvent
			hr = pCaptureClient->GetNextPacketSize(&packetLength);

			while (packetLength != 0)
			{
				// Get the available data in the shared buffer.
				// ��������������ȡ����

				hr = pCaptureClient->GetBuffer(&pData,
					&numFramesAvailable,
					&flags, NULL, NULL);

				if (numFramesAvailable != 0)
				{
					//
					//  The flags on capture tell us information about the data.
					//
					//  We only really care about the silent flag since we want to put frames of silence into the buffer
					//  when we receive silence.  We rely on the fact that a logical bit 0 is silence for both float and int formats.
					//
					if (flags & AUDCLNT_BUFFERFLAGS_SILENT)
					{
						//
						//  Fill 0s from the capture buffer to the output buffer.
						//
						memset(pData, 0, numFramesAvailable * nFrameSize);
						/*BYTE* outBuffer = (BYTE * )malloc(numFramesAvailable * nFrameSize);
						ZeroMemory(outBuffer, numFramesAvailable * nFrameSize);
						dump_raw(outBuffer, numFramesAvailable * nFrameSize);
						free(outBuffer);*/
					}
					else
					{
						//
						//  Copy data from the audio engine buffer to the output buffer.
						//

					}
					dump_raw(pData, numFramesAvailable * nFrameSize);

				}

				hr = pCaptureClient->ReleaseBuffer(numFramesAvailable);

				hr = pCaptureClient->GetNextPacketSize(&packetLength);

					// test GetCurrentPadding
					//////////////////////////////////////////////////////////////////////////
					/*
					This method retrieves a padding value that indicates the amount of
					valid, unread data that the endpoint buffer currently contains.
					����buffer�кϷ���δ��ȡ�����ݴ�С��
					The padding value is expressed as a number of audio frames.
					The size in bytes of an audio frame equals
					the number of channels in the stream multiplied by the sample size per channel.
					For example, the frame size is four bytes for a stereo (2-channel) stream with 16-bit samples.
					The padding value�ĵ�λ��audio frame��
					һ��audio frame�Ĵ�С���� ͨ���� * ÿ��ͨ����sample��С��
					For a shared-mode capture stream, the padding value reported by GetCurrentPadding
					specifies the number of frames of capture data
					that are available in the next packet in the endpoint buffer.
					*/
				UINT32 ui32NumPaddingFrames;
				hr = pAudioClient->GetCurrentPadding(&ui32NumPaddingFrames);
	

			} // end of 'while (packetLength != 0)'

			break;
		} // end of 'switch (waitResult)'

	} // end of 'while (stillPlaying)'


	hr = pAudioClient->Stop();  // Stop recording.

	CoTaskMemFree(pwfx);
	SAFE_RELEASE(pEnumerator)
	SAFE_RELEASE(pDevice)
	SAFE_RELEASE(pAudioClient)
	SAFE_RELEASE(pCaptureClient)

	CoUninitialize();
	printf("stop capture audio\n");
}
void audio_capture_stop() 
{
	stillPlaying = FALSE;
}
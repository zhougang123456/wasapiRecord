// ����������WAS�ɼ���Ƶ��demo
/*
References:
https://msdn.microsoft.com/en-us/library/dd370800(v=vs.85).aspx
http://blog.csdn.net/leave_rainbow/article/details/50917043
http://blog.csdn.net/lwsas1/article/details/46862195?locationNum=1
WindowsSDK7-Samples-master\multimedia\audio\CaptureSharedEventDriven
*/
#include <MMDeviceAPI.h>
#include <AudioClient.h>
#include <iostream>
#include "audio-capture.h"

#define DEF_CAPTURE_MIC
/*
ע1: ����ʱ ���0
ע2: ����ʱ Ӧ�ý�¼���豸�е���˷���ΪĬ���豸
ע3: ����DEF_CAPTURE_MICʱ�����Բɼ���˷� ������Բɼ�������
ע4:
���Բɼ�����:
Initializeʱ��Ҫ����AUDCLNT_STREAMFLAGS_LOOPBACK
����ģʽ�£���Ƶengine�Ὣrending�豸���ڲ��ŵ���Ƶ���� ����һ�ݵ���Ƶ��endpoint buffer
�����Ļ���WASAPI client���Բɼ���the stream.
��ʱ���ɼ���Speaker������
*/
DWORD WINAPI fun(LPVOID lpParameter)
{
	audio_capture_start();
	return 0;
}

int main(int argc, char* argv[])
{	
	HANDLE hThread = CreateThread(NULL, 0, fun, NULL, 0, NULL);
	Sleep(10000);
	audio_capture_stop();
	Sleep(2000);
	//CloseHandle(hThread);
	return 0;
}
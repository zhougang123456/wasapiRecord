// 基本的利用WAS采集音频的demo
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
注1: 静音时 填充0
注2: 测试时 应该将录音设备中的麦克风设为默认设备
注3: 定义DEF_CAPTURE_MIC时仅测试采集麦克风 否则测试采集声卡。
注4:
测试采集声卡:
Initialize时需要设置AUDCLNT_STREAMFLAGS_LOOPBACK
这种模式下，音频engine会将rending设备正在播放的音频流， 拷贝一份到音频的endpoint buffer
这样的话，WASAPI client可以采集到the stream.
此时仅采集到Speaker的声音
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
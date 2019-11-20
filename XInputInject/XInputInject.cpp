﻿#include <Windows.h>
#include <thread>
#include <math.h>
#include <atlstr.h> 
#include "MinHook.h"

#if defined _M_X64
#pragma comment(lib, "libMinHook-x64-v141-md.lib")
#elif defined _M_IX86
#pragma comment(lib, "libMinHook-x86-v141-md.lib")
#endif

#pragma comment(lib, "winmm.lib")

typedef struct _XINPUT_GAMEPAD
{
	WORD                                wButtons;
	BYTE                                bLeftTrigger;
	BYTE                                bRightTrigger;
	SHORT                               sThumbLX;
	SHORT                               sThumbLY;
	SHORT                               sThumbRX;
	SHORT                               sThumbRY;
} XINPUT_GAMEPAD, *PXINPUT_GAMEPAD;

typedef struct _XINPUT_STATE
{
	DWORD                               dwPacketNumber;
	XINPUT_GAMEPAD                      Gamepad;
} XINPUT_STATE, *PXINPUT_STATE;


typedef DWORD(WINAPI *XINPUTGETSTATE)(DWORD, XINPUT_STATE*);

// Pointer for calling original
static XINPUTGETSTATE hookedXInputGetState = nullptr;

// wrapper for easier setting up hooks for MinHook
template <typename T>
inline MH_STATUS MH_CreateHookEx(LPVOID pTarget, LPVOID pDetour, T** ppOriginal)
{
	return MH_CreateHook(pTarget, pDetour, reinterpret_cast<LPVOID*>(ppOriginal));
}

template <typename T>
inline MH_STATUS MH_CreateHookApiEx(LPCWSTR pszModule, LPCSTR pszProcName, LPVOID pDetour, T** ppOriginal)
{
	return MH_CreateHookApi(pszModule, pszProcName, pDetour, reinterpret_cast<LPVOID*>(ppOriginal));
}

bool ArduinoInit = false, ArduinoWork = false;
std::thread *pArduinothread = NULL;
HANDLE hSerial;
float ArduinoData[4] = { 0, 0, 0, 0 }; //Mode, Yaw, Pitch, Roll
float LastArduinoData[4] = { 0, 0, 0, 0 };
float YRPOffset[3] = { 0, 0, 0 };
BYTE GameMode = 0;
DWORD WheelAngle, SensX, SensY;//, TriggerSens;
int last_x = 0, last_y = 0;
DWORD WorkStatus = 0;

void Centering()
{
	YRPOffset[0] = ArduinoData[1];
	YRPOffset[1] = ArduinoData[2];
	YRPOffset[2] = ArduinoData[3];
}

bool CorrectAngleValue(float Value)
{
	if (Value > -180 && Value < 180)
	{
		return true;
	}
	else
	{
		return false;
	}
}

void ArduinoRead()
{
	DWORD bytesRead;

	while (ArduinoWork) {
		ReadFile(hSerial, &ArduinoData, sizeof(ArduinoData), &bytesRead, 0);

		//Filter incorrect values
		if (CorrectAngleValue(ArduinoData[1]) == false || CorrectAngleValue(ArduinoData[2]) == false || CorrectAngleValue(ArduinoData[3]) == false)
		{
			//Last correct values
			ArduinoData[0] = LastArduinoData[0];
			ArduinoData[1] = LastArduinoData[1];
			ArduinoData[2] = LastArduinoData[2];
			ArduinoData[3] = LastArduinoData[3];

			PurgeComm(hSerial, PURGE_TXCLEAR | PURGE_RXCLEAR);
		}

		//Save last correct values
		if (CorrectAngleValue(ArduinoData[1]) && CorrectAngleValue(ArduinoData[2]) && CorrectAngleValue(ArduinoData[3]))
		{
			LastArduinoData[0] = ArduinoData[0];
			LastArduinoData[1] = ArduinoData[1];
			LastArduinoData[2] = ArduinoData[2];
			LastArduinoData[3] = ArduinoData[3];
		}

		if (ArduinoData[0] == 1) { GameMode = 0; }
		if (ArduinoData[0] == 2)
		{
			GameMode = 1;
			PurgeComm(hSerial, PURGE_TXCLEAR | PURGE_RXCLEAR);
			Centering();
		}
		if (ArduinoData[0] == 4)
		{
			GameMode = 2;
			PurgeComm(hSerial, PURGE_TXCLEAR | PURGE_RXCLEAR);
			Centering();
		}
		if (ArduinoData[0] == 6)
		{
			GameMode = 3;
			PurgeComm(hSerial, PURGE_TXCLEAR | PURGE_RXCLEAR);
			Centering();
		}
	}
}

void ArduinoStart() {
	
	CRegKey key;
	DWORD PortNumber = 0;

	LONG status = key.Open(HKEY_CURRENT_USER, _T("Software\\r57zone\\X360Advance"));
	if (status == ERROR_SUCCESS)
	{

		key.QueryDWORDValue(_T("Port"), PortNumber);
		
		key.QueryDWORDValue(_T("WheelAngle"), WheelAngle);
		key.QueryDWORDValue(_T("SensX"), SensX); //def 45
		key.QueryDWORDValue(_T("SensY"), SensY); //def 35

	}
	key.Close();


	CString sPortName;
	sPortName.Format(_T("COM%d"), PortNumber);

	hSerial = ::CreateFile(sPortName, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);

	if (hSerial != INVALID_HANDLE_VALUE && GetLastError() != ERROR_FILE_NOT_FOUND) {

		DCB dcbSerialParams = { 0 };
		dcbSerialParams.DCBlength = sizeof(dcbSerialParams);

		if (GetCommState(hSerial, &dcbSerialParams))
		{
			dcbSerialParams.BaudRate = CBR_115200;
			dcbSerialParams.ByteSize = 8;
			dcbSerialParams.StopBits = ONESTOPBIT;
			dcbSerialParams.Parity = NOPARITY;

			if (SetCommState(hSerial, &dcbSerialParams))
			{
				if (WorkStatus == 3)
					PlaySound("C:\\Windows\\Media\\Windows Hardware Insert.wav", NULL, SND_ASYNC); //Alarm04.wav
				ArduinoWork = true;
				PurgeComm(hSerial, PURGE_TXCLEAR | PURGE_RXCLEAR);
				pArduinothread = new std::thread(ArduinoRead);
			}
		}
	}
}

SHORT ToLeftStick(double Value)
{
	int MyValue = round((32767 / WheelAngle) * Value);
	if (MyValue < -32767) MyValue = -32767;
	if (MyValue > 32767) MyValue = 32767;
	return MyValue;
}

SHORT ThumbFix(double Value)
{
	int MyValue = round(Value);
	if (MyValue > 32767) MyValue = 32767;
	if (MyValue < -32767) MyValue = -32767;
	return MyValue;
}

double OffsetYPR(float f, float f2)
{
	f -= f2;
	if (f < -180) {
		f += 360;
	}
	else if (f > 180) {
		f -= 360;
	}

	return f;
}

int MouseGetDelta(int val, int prev) //Implementation from OpenTrack https://github.com/opentrack/opentrack/blob/unstable/proto-mouse/
{
	const int a = std::abs(val - prev), b = std::abs(val + prev);
	if (b < a)
		return val + prev;
	else
		return val - prev;
}

void MouseMove(const double axisX, const double axisY) //Implementation from OpenTrack https://github.com/opentrack/opentrack/blob/unstable/proto-mouse/
{
	int mouse_x = 0, mouse_y = 0;

	mouse_x = round(axisX * SensX * 0.1 * 2); //* 0.1
	mouse_y = round(axisY * SensY * 0.1 * 2);

	const int dx = MouseGetDelta(mouse_x, last_x);
	const int dy = MouseGetDelta(mouse_y, last_y);

	last_x = mouse_x;
	last_y = mouse_y;

	if (dx || dy)
	{
		INPUT input;
		input.type = INPUT_MOUSE;
		MOUSEINPUT& mi = input.mi;
		mi = {};
		mi.dx = dx;
		mi.dy = dy;
		mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_MOVE_NOCOALESCE;

		SendInput(1, &input, sizeof(input));
	}
}

//Own GetState
DWORD WINAPI detourXInputGetState(DWORD dwUserIndex, XINPUT_STATE* pState)
{

	if (ArduinoInit == false) {
		ArduinoInit = true;
		ArduinoStart();
	}

	// first call the original function
	//ZeroMemory(pState, sizeof(XINPUT_STATE));
	DWORD toReturn = hookedXInputGetState(dwUserIndex, pState);
	
	if (ArduinoWork) {
	

		switch (GameMode)
		{
		case 1: //Wheel
		{
			pState->Gamepad.sThumbLX = ToLeftStick(OffsetYPR(ArduinoData[1], YRPOffset[0])) * -1;
			break;
		}


		case 2:	//FPS
		{
			//Fully emulation
			//pState->Gamepad.sThumbRX = ThumbFix(OffsetYPR(ArduinoData[1], YRPOffset[0]) * -750);
			//pState->Gamepad.sThumbRY = ThumbFix(OffsetYPR(ArduinoData[3], YRPOffset[2]) * -750);

			//Gyroscope offset
			//pState->Gamepad.sThumbRX = ThumbFix(myPState.Gamepad.sThumbRX + OffsetYPR(ArduinoData[1], YRPOffset[0]) * -182 * StickSensX); //StickSensX - 9
			//pState->Gamepad.sThumbRY = ThumbFix(myPState.Gamepad.sThumbRY + OffsetYPR(ArduinoData[3], YRPOffset[2]) * -182 * StickSensY); //StickSensX - 7

			/*if (pState->Gamepad.bLeftTrigger == 0) {
				MouseMove(OffsetYPR(ArduinoData[1], YRPOffset[0]) * -1, OffsetYPR(ArduinoData[3], YRPOffset[2]));
			} else {
				MouseMove(OffsetYPR(ArduinoData[1], YRPOffset[0]) * -1 * TriggerSens, OffsetYPR(ArduinoData[3], YRPOffset[2]) * TriggerSens);
			}*/

			MouseMove(OffsetYPR(ArduinoData[1], YRPOffset[0]) * -1, OffsetYPR(ArduinoData[3], YRPOffset[2]));
			break;
		}

		case 3:	//Fully emulation
		{
			pState->Gamepad.sThumbRX = ThumbFix(OffsetYPR(ArduinoData[1], YRPOffset[0]) * -750);
			pState->Gamepad.sThumbRY = ThumbFix(OffsetYPR(ArduinoData[3], YRPOffset[2]) * -750);

			break;
		}
		}

	}

	return toReturn;
}

BOOL APIENTRY DllMain(HMODULE hModule,
	DWORD  ul_reason_for_call,
	LPVOID lpReserved
)
{
	switch (ul_reason_for_call){
		case DLL_PROCESS_ATTACH: {

			if (MH_Initialize() == MH_OK)
				WorkStatus++;

			//1.0
			if (MH_CreateHookApiEx(L"XINPUT9_1_0", "XInputGetState", &detourXInputGetState, &hookedXInputGetState) == MH_OK)
				WorkStatus++;

			//1_1
			if (hookedXInputGetState == nullptr)
				if (MH_CreateHookApiEx(L"XINPUT_1_1", "XInputGetState", &detourXInputGetState, &hookedXInputGetState) == MH_OK)
					WorkStatus++;

			//1_2
			if (hookedXInputGetState == nullptr)
				if (MH_CreateHookApiEx(L"XINPUT_1_2", "XInputGetState", &detourXInputGetState, &hookedXInputGetState) == MH_OK)
					WorkStatus++;

			//1_3
			if (hookedXInputGetState == nullptr)
				if (MH_CreateHookApiEx(L"XINPUT1_3", "XInputGetState", &detourXInputGetState, &hookedXInputGetState) == MH_OK)
					WorkStatus++;

			//1_4
			if (hookedXInputGetState == nullptr)
				if (MH_CreateHookApiEx(L"XINPUT1_4", "XInputGetState", &detourXInputGetState, &hookedXInputGetState) == MH_OK)
					WorkStatus++;
		

			//if (MH_EnableHook(&detourXInputGetState) == MH_OK) //Not working
			if (MH_EnableHook(MH_ALL_HOOKS) == MH_OK)
				WorkStatus++;
				//MessageBox(0, "XInput hooked", "XINPUT", MB_OK);


			break;
		}

		/*case DLL_THREAD_ATTACH:
			{
				MessageBox(0, "THREAD_ATTACH", "XINPUT", MB_OK);
				break;
			}

		case DLL_THREAD_DETACH:
			{
			MessageBox(0, "THREAD_DETACH", "XINPUT", MB_OK);
			break;
		}*/

		case DLL_PROCESS_DETACH:
		{
			if (ArduinoWork) {
				ArduinoWork = false;
				if (pArduinothread)
				{
					pArduinothread->join();
					delete pArduinothread;
					pArduinothread = nullptr;
				}
			}
			CloseHandle(hSerial);
			//MessageBox(0, "PROCESS_DETACH", "XINPUT", MB_OK);
			MH_DisableHook(&detourXInputGetState);
			MH_Uninitialize();
			break;
		}
	}
	return TRUE;
}

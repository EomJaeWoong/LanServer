// LanServer.cpp : 콘솔 응용 프로그램에 대한 진입점을 정의합니다.
//

#include "stdafx.h"

CLanServerEcho EchoServer;

int _tmain(int argc, _TCHAR* argv[])
{
	char chControlKey;
	SYSTEMTIME lst;

	EchoServer.Start(L"127.0.0.1", 6000, 10, false, 100);
	
    GetLocalTime(&lst);
 
	while (1)
	{
		wprintf(L"==========================================================\n");
		wprintf(L"                        LanServer Echo Test\n");
		wprintf(L"==========================================================\n");
		wprintf(L"Connect Session : %d\n", EchoServer.GetSessionCount());
		wprintf(L"Accept TPS : %d\n", EchoServer._lAcceptTPS);
		wprintf(L"Accept Total : %d\n", EchoServer._lAcceptTotalTPS);
		wprintf(L"RecvPacket TPS : %d\n", EchoServer._lRecvPacketTPS);
		wprintf(L"SendPacket TPS : %d\n", EchoServer._lSendPacketTPS);

		wprintf(L"PacketPool Use : %d\n", 0);
		wprintf(L"PacketPool Alloc : %d\n", EchoServer._lPacketPoolTPS);
		wprintf(L"\n\n\n\n\n\n\n\n\n\n\n\n\n");
		wprintf(L"Start time : %04d-%02d-%02d %02d:%02d:%02d\n",
			lst.wYear, lst.wMonth, lst.wDay, lst.wHour, lst.wMinute, lst.wSecond);
		wprintf(L"==========================================================\n");

		if (_kbhit() != 0){
			chControlKey = _getch();

			switch (chControlKey)
			{
			case 'x' :
			case 'X' :
				//------------------------------------------------
				// 종료처리
				//------------------------------------------------
				break;

			case 'g' :
			case 'G' :
				//------------------------------------------------
				// 시작처리
				//------------------------------------------------
				EchoServer.Start(L"127.0.0.1", 5000, 1, false, 100);
				break;

			case 's' :
			case 'S' :
				//------------------------------------------------
				// 정지처리
				//------------------------------------------------
				EchoServer.Stop();
				break;

			case 'p' :
			case 'P' :
				SaveProfile();
				break;

			default :
				break;
			}
		}

		Sleep(999);
	}


	return 0;
}


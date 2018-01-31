// LanServer.cpp : 콘솔 응용 프로그램에 대한 진입점을 정의합니다.
//

#include "stdafx.h"

CLanServerEcho EchoServer;

int _tmain(int argc, _TCHAR* argv[])
{
	char chControlKey;

	EchoServer.Start(L"127.0.0.1", 6000, 2, false, 100);

	while (1)
	{
		wprintf(L"------------------------------------------------\n");
		wprintf(L"Connect Session : %d\n", EchoServer._iSessionCount);
		wprintf(L"Accept TPS : %d\n", EchoServer._lAcceptTPS);
		wprintf(L"Accept Total : %d\n", EchoServer._lAcceptTotalTPS);
		wprintf(L"RecvPacket TPS : %d\n", EchoServer._lRecvPacketTPS);
		wprintf(L"SendPacket TPS : %d\n", EchoServer._lSendPacketTPS);
		wprintf(L"PacketPool Use : %d\n", 0);
		wprintf(L"PacketPool Alloc : %d\n", 0);
		wprintf(L"------------------------------------------------\n\n");

		Sleep(999);

		if (_kbhit() != 0){
			chControlKey = _getch();
			if (chControlKey == 'q' || chControlKey == 'Q')
			{
				//------------------------------------------------
				// 종료처리
				//------------------------------------------------
				break;
			}

			else if (chControlKey == 'G' || chControlKey == 'g')
			{
				//------------------------------------------------
				// 시작처리
				//------------------------------------------------
				EchoServer.Start(L"127.0.0.1", 5000, 1, false, 100);
			}

			else if (chControlKey == 'S' || chControlKey == 's')
			{
				//------------------------------------------------
				// 정지처리
				//------------------------------------------------
				EchoServer.Stop();
			}

			/*
			else if (chControlKey == 'P' || chControlKey == 'p')
			{
			SaveProfile();
			}
			*/
		}
	}


	return 0;
}


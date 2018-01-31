// LanServer.cpp : �ܼ� ���� ���α׷��� ���� �������� �����մϴ�.
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
				// ����ó��
				//------------------------------------------------
				break;
			}

			else if (chControlKey == 'G' || chControlKey == 'g')
			{
				//------------------------------------------------
				// ����ó��
				//------------------------------------------------
				EchoServer.Start(L"127.0.0.1", 5000, 1, false, 100);
			}

			else if (chControlKey == 'S' || chControlKey == 's')
			{
				//------------------------------------------------
				// ����ó��
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


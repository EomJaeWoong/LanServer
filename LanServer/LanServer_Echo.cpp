#include "stdafx.h"

CLanServerEcho::CLanServerEcho()
	: CLanServer(){}

CLanServerEcho::~CLanServerEcho(){}



void CLanServerEcho::OnClientJoin(SESSIONINFO *pSessionInfo, __int64 iSessionID)		// Accept �� ����ó�� �Ϸ� �� ȣ��.
{
	
}

void CLanServerEcho::OnClientLeave(__int64 ClientID)   					// Disconnect �� ȣ��
{

}

bool CLanServerEcho::OnConnectionRequest(SESSIONINFO *pSessionInfo)		// accept ����
{
	return true;
}

void CLanServerEcho::OnRecv(__int64 ClientID, CNPacket *pPacket)			// ��Ŷ ���� �Ϸ� ��
{
	CNPacket *pSendPacket = CNPacket::Alloc();

	__int64 iValue;

	// Packet Process
	*pPacket >> iValue;

	*pSendPacket << iValue;
	//////////////////////

	SendPacket(ClientID, pSendPacket);

	pSendPacket->Free();
}

void CLanServerEcho::OnSend(__int64 ClientID, int sendsize)				// ��Ŷ �۽� �Ϸ� ��
{

}

void CLanServerEcho::OnWorkerThreadBegin()								// ��Ŀ������ GQCS �ٷ� �ϴܿ��� ȣ��
{

}

void CLanServerEcho::OnWorkerThreadEnd()								// ��Ŀ������ 1���� ���� ��
{

}

void CLanServerEcho::OnError(int errorCode, WCHAR *errorString)
{
	
}
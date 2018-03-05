#include "stdafx.h"

CLanServerEcho::CLanServerEcho()
	: CLanServer(){}

CLanServerEcho::~CLanServerEcho(){}



void CLanServerEcho::OnClientJoin(SESSIONINFO *pSessionInfo, __int64 iSessionID)		// Accept �� ����ó�� �Ϸ� �� ȣ��.
{
	////////////////////////////////////////////////////////////////////////////
	// Login Packet ������
	////////////////////////////////////////////////////////////////////////////
	PRO_BEGIN(L"LoginPacket Alloc");
	CNPacket *pLoginPacket = CNPacket::Alloc();
	PRO_END(L"LoginPacket Alloc");

	*pLoginPacket << 0x7fffffffffffffff;
	pLoginPacket->SetCustomShortHeader(pLoginPacket->GetDataSize());

	PRO_BEGIN(L"SendPacket");
	SendPacket(iSessionID, pLoginPacket);
	PRO_END(L"SendPacket");

	PRO_BEGIN(L"LoginPacket Free");
	pLoginPacket->Free();
	PRO_END(L"LoginPacket Free");
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
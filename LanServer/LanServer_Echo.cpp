#include "stdafx.h"

CLanServerEcho::CLanServerEcho()
	: CLanServer(){}

CLanServerEcho::~CLanServerEcho(){}



void CLanServerEcho::OnClientJoin(SESSIONINFO *pSessionInfo, __int64 iSessionID)		// Accept 후 접속처리 완료 후 호출.
{
	
}

void CLanServerEcho::OnClientLeave(__int64 ClientID)   					// Disconnect 후 호출
{

}

bool CLanServerEcho::OnConnectionRequest(SESSIONINFO *pSessionInfo)		// accept 직후
{
	return true;
}

void CLanServerEcho::OnRecv(__int64 ClientID, CNPacket *pPacket)			// 패킷 수신 완료 후
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

void CLanServerEcho::OnSend(__int64 ClientID, int sendsize)				// 패킷 송신 완료 후
{

}

void CLanServerEcho::OnWorkerThreadBegin()								// 워커스레드 GQCS 바로 하단에서 호출
{

}

void CLanServerEcho::OnWorkerThreadEnd()								// 워커스레드 1루프 종료 후
{

}

void CLanServerEcho::OnError(int errorCode, WCHAR *errorString)
{
	
}
#include "stdafx.h"

//-----------------------------------------------------------------------------------------
// ������, �Ҹ���
//-----------------------------------------------------------------------------------------
CLanServer::CLanServer()
{
	CCrashDump::CCrashDump();
	ProfileInit();

	if (!CNPacket::_ValueSizeCheck())
		CCrashDump::Crash();

	_pBlankStack = new CArrayStack<int>[eMAX_SESSION];

	///////////////////////////////////////////////////////////////////////////////
	// �� ���� ����
	///////////////////////////////////////////////////////////////////////////////
	for (int iCnt = eMAX_SESSION - 1; iCnt >= 0; iCnt--)
	{
		_Session[iCnt] = new SESSION;
		///////////////////////////////////////////////////////////////////////////
		// ���� ���� ����ü �ʱ�ȭ
		///////////////////////////////////////////////////////////////////////////
		_Session[iCnt]->_SessionInfo._Socket = INVALID_SOCKET;
		memset(&_Session[iCnt]->_SessionInfo._wIP, 0, sizeof(_Session[iCnt]->_SessionInfo._wIP));
		_Session[iCnt]->_SessionInfo._iPort = 0;

		///////////////////////////////////////////////////////////////////////////
		// ���� �ʱ�ȭ
		///////////////////////////////////////////////////////////////////////////
		_Session[iCnt]->_iSessionID = -1;

		memset(&_Session[iCnt]->_SendOverlapped, 0, sizeof(OVERLAPPED));
		memset(&_Session[iCnt]->_RecvOverlapped, 0, sizeof(OVERLAPPED));

		_Session[iCnt]->_SendQ.ClearBuffer();
		_Session[iCnt]->_RecvQ.ClearBuffer();

		_Session[iCnt]->_bSendFlag = false;
		_Session[iCnt]->_lIOCount = 0;

		memset(_Session[iCnt]->_pSentPacket, 0, sizeof(_Session[iCnt]->_pSentPacket));
		_Session[iCnt]->_lSentPacketCnt = 0;

		InsertBlankSessionIndex(iCnt);
	}

	
	///////////////////////////////////////////////////////////////////////////////
	// LanServer ���� ����
	///////////////////////////////////////////////////////////////////////////////
	_iSessionID = 0;
}

CLanServer::~CLanServer()
{

}


//-----------------------------------------------------------------------------------------
// ���� ����
//-----------------------------------------------------------------------------------------
bool				CLanServer::Start(WCHAR* wOpenIP, int iPort, int iWorkerThreadNum, bool bNagle, int iMaxConnect)
{
	int				result;
	unsigned long	ul = 1;


	///////////////////////////////////////////////////////////////////////////////////////
	// ���� �ʱ�ȭ
	///////////////////////////////////////////////////////////////////////////////////////
	WSADATA wsa;
	if (0 != WSAStartup(MAKEWORD(2, 2), &wsa))
		return false;

	///////////////////////////////////////////////////////////////////////////////////////
	// IO Completion Port ����
	///////////////////////////////////////////////////////////////////////////////////////
	_hIOCP = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
	if (NULL == _hIOCP)
		return false;

	///////////////////////////////////////////////////////////////////////////////////////
	// �������� ����
	///////////////////////////////////////////////////////////////////////////////////////
	_ListenSocket = socket(AF_INET, SOCK_STREAM, 0);
	if (INVALID_SOCKET == _ListenSocket)
		return false;

	///////////////////////////////////////////////////////////////////////////////////////
	// bind
	///////////////////////////////////////////////////////////////////////////////////////
	SOCKADDR_IN serverAddr;
	serverAddr.sin_family = AF_INET;
	serverAddr.sin_port = htons(iPort);
	InetPton(AF_INET, wOpenIP, &serverAddr.sin_addr);
	result = bind(_ListenSocket, (SOCKADDR *)&serverAddr, sizeof(SOCKADDR_IN));
	if (SOCKET_ERROR == result)
		return false;

	///////////////////////////////////////////////////////////////////////////////////
	// non-block ���� ��ȯ
	// SOCKET_ERROR�� ���� �������� ��ȯ�� ������
	///////////////////////////////////////////////////////////////////////////////////
	result = ioctlsocket(_ListenSocket, FIONBIO, (unsigned long *)&ul);
	if (SOCKET_ERROR == result)
		return false;

	///////////////////////////////////////////////////////////////////////////////////
	// ���� Send Buffer ������ 0���� ����
	///////////////////////////////////////////////////////////////////////////////////
	int optval;
	setsockopt(_ListenSocket, SOL_SOCKET, SO_SNDBUF, (char*)&optval, 0);
		
	///////////////////////////////////////////////////////////////////////////////////////
	// listen
	///////////////////////////////////////////////////////////////////////////////////////
	result = listen(_ListenSocket, SOMAXCONN);
	if (SOCKET_ERROR == result)
		return false;

	///////////////////////////////////////////////////////////////////////////////////////
	// nagle option
	///////////////////////////////////////////////////////////////////////////////////////
	_bNagle = bNagle;


	///////////////////////////////////////////////////////////////////////////////////////
	// Thread ����
	///////////////////////////////////////////////////////////////////////////////////////
	DWORD dwThreadID;

	_hAcceptThread = (HANDLE)_beginthreadex(
		NULL,
		0,
		AcceptThread,
		this,
		0,
		(unsigned int *)&dwThreadID
		);

	_hMonitorThread = (HANDLE)_beginthreadex(
		NULL,
		0,
		MonitorThread,
		this,
		0,
		(unsigned int *)&dwThreadID
		);

	if (iWorkerThreadNum > eMAX_THREAD)
		return false;

	_iWorkerThreadNum = iWorkerThreadNum;

	for (int iCnt = 0; iCnt < iWorkerThreadNum; iCnt++)
	{
		_hWorkerThread[iCnt] = (HANDLE)_beginthreadex(
			NULL,
			0,
			WorkerThread,
			this,
			0,
			(unsigned int *)&dwThreadID
			);
	}

	return true;
}


///////////////////////////////////////////////////////////////////////////////////////////
// ���� ����
///////////////////////////////////////////////////////////////////////////////////////////
void				CLanServer::Stop()
{
	
}


///////////////////////////////////////////////////////////////////////////////////////////
// ���� ����
///////////////////////////////////////////////////////////////////////////////////////////
bool				CLanServer::SendPacket(__int64 iSessionID, CNPacket *pPacket)
{
	int iSessionIndex = GET_SESSIONINDEX(iSessionID);
	
	pPacket->SetCustomShortHeader(pPacket->GetDataSize());

	PRO_BEGIN(L"Packet addref");
	pPacket->addRef();
	PRO_END(L"Packet addref");

	PRO_BEGIN(L"PacketQueue Put");
	_Session[iSessionIndex]->_SendQ.Lock();
	int iPutSize = _Session[iSessionIndex]->_SendQ.Put(
		(char *)&pPacket,
		sizeof(char *)
		);
	_Session[iSessionIndex]->_SendQ.Unlock();
	PRO_END(L"PacketQueue Put");

	PRO_BEGIN(L"SendPost");
	SendPost(_Session[iSessionIndex]);
	PRO_END(L"SendPost");

	InterlockedIncrement((LONG *)&_lSendPacketCounter);

	return true;
}


///////////////////////////////////////////////////////////////////////////////////////////
// ���� ����
///////////////////////////////////////////////////////////////////////////////////////////
bool				CLanServer::Disconnect(__int64 iSessionID)
{
	int iSessionIndex = GET_SESSIONINDEX(iSessionID);

	DisconnectSession(_Session[iSessionIndex]);
}

///////////////////////////////////////////////////////////////////////////////////////////
// ���� �����ϴ� ������ �κ�
///////////////////////////////////////////////////////////////////////////////////////////
int					CLanServer::AccpetThread_update()
{
	HANDLE			hResult;

	SOCKET			ClientSocket;
	SOCKADDR_IN		ClientAddr;
	int				iAddrLen = sizeof(SOCKADDR_IN);
	

	SESSIONINFO		SessionInfo;

	int				iBlankIndex;


	while (1)
	{
		///////////////////////////////////////////////////////////////////////////////////
		// accpet
		///////////////////////////////////////////////////////////////////////////////////
		ClientSocket = accept(_ListenSocket, (SOCKADDR *)&ClientAddr, &iAddrLen);
		if (INVALID_SOCKET == ClientSocket)
		{
			int iErrorCode = WSAGetLastError();
			if (iErrorCode == WSAEWOULDBLOCK)
				continue;

			CCrashDump::Crash();
		}

		///////////////////////////////////////////////////////////////////////////////////
		// accpet ���� ������ block �������� �ٽ� ��ȯ
		///////////////////////////////////////////////////////////////////////////////////
		unsigned long	ul = 0;
		if (SOCKET_ERROR == ioctlsocket(ClientSocket, FIONBIO, (unsigned long *)&ul))
			CCrashDump::Crash();



		InterlockedIncrement((LONG *)&_lAcceptCounter);
		InterlockedIncrement((LONG *)&_lAcceptTotalCounter);

		///////////////////////////////////////////////////////////////////////////////////
		// ���� ���� ���� ����
		///////////////////////////////////////////////////////////////////////////////////
		SessionInfo._Socket = ClientSocket;
		InetNtop(AF_INET, &ClientAddr.sin_addr, SessionInfo._wIP, 16);
		SessionInfo._iPort = ntohs(ClientAddr.sin_port);

		iBlankIndex = GetBlankSessionIndex();

		///////////////////////////////////////////////////////////////////////////////////
		// �ִ� ���� �ʰ���
		///////////////////////////////////////////////////////////////////////////////////
		if (iBlankIndex < 0)
		{
			closesocket(ClientSocket);
			continue;
		}

		///////////////////////////////////////////////////////////////////////////////////
		// ���� ��û(White IP�� �����ϰ� �ϱ� ��)
		///////////////////////////////////////////////////////////////////////////////////
		if (!OnConnectionRequest(&SessionInfo))
			continue;
		
		///////////////////////////////////////////////////////////////////////////////////
		// keepalive �ɼ�
		//
		// onoff			 -> keepalive ���� ����(0�� �ƴϸ� ����)
		// keepalivetime	 -> ù keepalive ��Ŷ�� ���� �������� �ð�
		// keepaliveinterval -> ������ ������� ���� keepalive ��Ŷ�� ���۵Ǵ� ���� 
		///////////////////////////////////////////////////////////////////////////////////
		tcp_keepalive tcpkl;

		tcpkl.onoff = 1;
		tcpkl.keepalivetime = 30000;
		tcpkl.keepaliveinterval = 2000;

		DWORD dwReturnByte;
		WSAIoctl(SessionInfo._Socket, SIO_KEEPALIVE_VALS, &tcpkl, sizeof(tcp_keepalive),
			0, 0, &dwReturnByte, NULL, NULL);

		///////////////////////////////////////////////////////////////////////////////////
		// nagle �ɼ�
		///////////////////////////////////////////////////////////////////////////////////
		if (_bNagle)
		{
			char opt_val = true;
			setsockopt(SessionInfo._Socket, IPPROTO_TCP, TCP_NODELAY, &opt_val, sizeof(opt_val));
		}

		///////////////////////////////////////////////////////////////////////////////////
		// �� ���� ã�� ���� ����
		///////////////////////////////////////////////////////////////////////////////////
		_Session[iBlankIndex]->_SessionInfo = SessionInfo;

		///////////////////////////////////////////////////////////////////////////////////
		// ���� ID �����ؼ� �����
		///////////////////////////////////////////////////////////////////////////////////
		int iSessionID = InterlockedIncrement((LONG *)&_iSessionID);
		_Session[iBlankIndex]->_iSessionID = COMBINE_ID_WITH_INDEX(iSessionID, iBlankIndex);

		/////////////////////////////////////////////////////////////////////
		// IOCP ���
		/////////////////////////////////////////////////////////////////////
		hResult = CreateIoCompletionPort((HANDLE)_Session[iBlankIndex]->_SessionInfo._Socket,
			_hIOCP,
			(ULONG_PTR)_Session[iBlankIndex],
			0);
		if (!hResult)
			PostQueuedCompletionStatus(_hIOCP, 0, 0, 0);

		/////////////////////////////////////////////////////////////////////
		// OnClientJoin
		// �������ʿ� ������ �������� �˸�
		// �α��� ��Ŷ ������ �߿� ���� �� ������ IOCount�� �̸� �÷��ش�
		/////////////////////////////////////////////////////////////////////
		InterlockedIncrement((long *)&_Session[iBlankIndex]->_lIOCount);
		OnClientJoin(&_Session[iBlankIndex]->_SessionInfo, _Session[iBlankIndex]->_iSessionID);

		InterlockedIncrement((LONG *)&_lSessionCount);

		PRO_BEGIN(L"RecvPost - AccpetTH");
		RecvPost(_Session[iBlankIndex], true);
		PRO_END(L"RecvPost - AccpetTH");
	}

	return 0;
}

int					CLanServer::WorkerThread_update()
{
	int				result;

	OVERLAPPED		*pOverlapped;
	SESSION			*pSession;
	DWORD			dwTransferred;

	while (1)
	{
		pOverlapped		= NULL;
		pSession		= NULL;
		dwTransferred	= 0;

		PRO_BEGIN(L"GQCS IOComplete");
		result = GetQueuedCompletionStatus(
			_hIOCP,
			&dwTransferred,
			(PULONG_PTR)&pSession,
			&pOverlapped,
			INFINITE);
		PRO_END(L"GQCS IOComplete");

		OnWorkerThreadBegin();

		///////////////////////////////////////////////////////////////////////////////////
		// Error, ���� ó��
		///////////////////////////////////////////////////////////////////////////////////
		// IOCP ���� ���� ����
		if (result == FALSE && (pOverlapped == NULL || pSession == NULL))
		{
			int iErrorCode = WSAGetLastError();
			OnError(iErrorCode, L"IOCP HANDLE Error\n");

			break;
		}

		// ��Ŀ������ ���� ����
		else if (dwTransferred == 0 && pSession == NULL && pOverlapped == NULL)
		{
			OnError(0, L"Worker Thread Done.\n");
			PostQueuedCompletionStatus(_hIOCP, 0, NULL, NULL);
			return 0;
		}

		//----------------------------------------------------------------------------
		// ��������
		// Ŭ���̾�Ʈ ���� closesocket() Ȥ�� shutdown() �Լ��� ȣ���� ����
		//----------------------------------------------------------------------------
		else if (dwTransferred == 0)
		{
			DisconnectSession(pSession);
		}
		//----------------------------------------------------------------------------

		if (pOverlapped == &(pSession->_RecvOverlapped))
		{
			PRO_BEGIN(L"CompleteRecv");
			CompleteRecv(pSession, dwTransferred);
			PRO_END(L"CompleteRecv");
		}

		if (pOverlapped == &(pSession->_SendOverlapped))
		{
			PRO_BEGIN(L"CompleteSend");
			CompleteSend(pSession, dwTransferred);
			PRO_END(L"CompleteSend");
		}

		if (0 == InterlockedDecrement((LONG *)&pSession->_lIOCount))
			ReleaseSession(pSession);

		OnWorkerThreadEnd();
	}

	return true;
}

int					CLanServer::MonitorThread_update()
{
	timeBeginPeriod(1);

	DWORD iGetTime = timeGetTime();

	while (1)
	{
		if (timeGetTime() - iGetTime < 1000)
			continue;

		_lAcceptTPS = _lAcceptCounter;
		_lAcceptTotalTPS += _lAcceptTotalCounter;
		_lRecvPacketTPS = _lRecvPacketCounter;
		_lSendPacketTPS = _lSendPacketCounter;
		_lPacketPoolTPS = CNPacket::GetPacketCount();

		_lAcceptCounter = 0;
		_lAcceptTotalCounter = 0;
		_lRecvPacketCounter = 0;
		_lSendPacketCounter = 0;

		iGetTime = timeGetTime();
	}

	timeEndPeriod(1);

	return 0;
}



///////////////////////////////////////////////////////////////////////////////////////////
// Recv, Send ���
///////////////////////////////////////////////////////////////////////////////////////////
void				CLanServer::RecvPost(SESSION *pSession, bool bAcceptRecv)
{
	int result, iCount = 1;
	DWORD dwRecvSize, dwFlag = 0;
	WSABUF wBuf[2];

	///////////////////////////////////////////////////////////////////////////////////////
	// WSABUF ���
	///////////////////////////////////////////////////////////////////////////////////////
	wBuf[0].buf = pSession->_RecvQ.GetWriteBufferPtr();
	wBuf[0].len = pSession->_RecvQ.GetNotBrokenPutSize();

	///////////////////////////////////////////////////////////////////////////////////////
	// ������ �������� ��� ���� ���� ���
	///////////////////////////////////////////////////////////////////////////////////////
	if (pSession->_RecvQ.GetFreeSize() > pSession->_RecvQ.GetNotBrokenPutSize())
	{
		wBuf[1].buf = pSession->_RecvQ.GetBufferPtr();
		wBuf[1].len = pSession->_RecvQ.GetFreeSize() -
						pSession->_RecvQ.GetNotBrokenPutSize();

		iCount++;
	}
	
	memset(&pSession->_RecvOverlapped, 0, sizeof(OVERLAPPED));

	///////////////////////////////////////////////////////////////////////////////////////
	// ù ���ӽ��� Recv�� IOCount�� �ø��� ����(�α��� ��Ŷ ����)
	///////////////////////////////////////////////////////////////////////////////////////
	if (!bAcceptRecv)
		InterlockedIncrement((LONG *)&pSession->_lIOCount);

	PRO_BEGIN(L"WSARecv");
	result = WSARecv(
		pSession->_SessionInfo._Socket, 
		wBuf,
		iCount, 
		&dwRecvSize,
		&dwFlag,
		&pSession->_RecvOverlapped, 
		NULL
		);

	if (result == SOCKET_ERROR)
	{
		int iErrorCode = GetLastError();
		///////////////////////////////////////////////////////////////////////////////////
		// WSA_IO_PENDING -> Overlapped ������ �غ�Ǿ����� �Ϸ���� ���� ���
		// �� �ܿ��� ������ ��
		///////////////////////////////////////////////////////////////////////////////////
		if (iErrorCode != WSA_IO_PENDING)
		{
			///////////////////////////////////////////////////////////////////////////////
			// 10054 : Ŭ���̾�Ʈ �ʿ��� ������ ������ ���
			// 10058 : �ش� ������ shutdown�� ���
			///////////////////////////////////////////////////////////////////////////////
			if ((WSAECONNABORTED != iErrorCode) &&
				(WSAECONNRESET != iErrorCode) &&
				(WSAESHUTDOWN != iErrorCode))
				CCrashDump::Crash();


			if (0 == InterlockedDecrement((LONG *)&pSession->_lIOCount))
				ReleaseSession(pSession);
		}
	}
	PRO_END(L"WSARecv");
}

bool				CLanServer::SendPost(SESSION *pSession)
{
	int result, iCount;
	DWORD dwSendSize, dwFlag = 0;
	WSABUF wBuf[eMAX_WSABUF];

	do
	{
		///////////////////////////////////////////////////////////////////////////////////
		// SendFlag Ȯ�� �� ����
		///////////////////////////////////////////////////////////////////////////////////
		bool bSendFlag = InterlockedCompareExchange((long *)&pSession->_bSendFlag, true, false);
		if (true == bSendFlag)
			break;

		///////////////////////////////////////////////////////////////////////////////////
		// SendQ ������ ����
		///////////////////////////////////////////////////////////////////////////////////
		int iSendQWritePos = pSession->_SendQ.GetWriteBufferPtr() - pSession->_SendQ.GetBufferPtr();
		int iSendQReadPos = pSession->_SendQ.GetReadBufferPtr() - pSession->_SendQ.GetBufferPtr();
		int iSendQUseSize = 0;

		if (iSendQWritePos >= iSendQReadPos)
			iSendQUseSize = iSendQWritePos - iSendQReadPos;
		else
			iSendQUseSize = (pSession->_SendQ.GetBufferSize() - iSendQReadPos) + iSendQWritePos;

		int iPacketSize = sizeof(char *);

		iSendQUseSize /= iPacketSize;

		///////////////////////////////////////////////////////////////////////////////////
		// SendQ������ �ٽ� Ȯ��
		///////////////////////////////////////////////////////////////////////////////////
		if (0 == iSendQUseSize)
		{
			if (false == InterlockedCompareExchange((long *)&pSession->_bSendFlag, false, true))
				CCrashDump::Crash();

			if (0 < pSession->_SendQ.GetUseSize())
				continue;

			else			break;
		}
	
		else if (eMAX_WSABUF <= iSendQUseSize)
			iSendQUseSize = eMAX_WSABUF;

		memset(wBuf, 0, sizeof(wBuf));

		///////////////////////////////////////////////////////////////////////////////////
		// WSABUF�� �ֱ�
		///////////////////////////////////////////////////////////////////////////////////
		CNPacket *pPacket;
		for (iCount = 0; iCount < iSendQUseSize; iCount++)
		{
			pPacket = nullptr;
			int iGetSize = pSession->_SendQ.Get((char *)&pPacket, iPacketSize);
			if (iPacketSize != iGetSize)
				CCrashDump::Crash();
			
			wBuf[iCount].buf = (char *)pPacket->GetBufferHeaderPtr();
			wBuf[iCount].len = pPacket->GetDataSize();
			
			pSession->_pSentPacket[iCount] = (char *)pPacket;
		}

		if (iCount > eMAX_WSABUF)
			CCrashDump::Crash();

		pSession->_lSentPacketCnt += iCount;
		
		memset(&pSession->_SendOverlapped, 0, sizeof(OVERLAPPED));

		InterlockedIncrement((LONG *)&pSession->_lIOCount);

		PRO_BEGIN(L"WSASend");
		result = WSASend(
			pSession->_SessionInfo._Socket,
			wBuf,
			iCount,
			&dwSendSize,
			dwFlag,
			&pSession->_SendOverlapped,
			NULL
			);

		if (result == SOCKET_ERROR)
		{
			int iErrorCode = WSAGetLastError();
			///////////////////////////////////////////////////////////////////////////////////
			// WSA_IO_PENDING -> Overlapped ������ �غ�Ǿ����� �Ϸ���� ���� ���
			// �� �ܿ��� ������ ��
			///////////////////////////////////////////////////////////////////////////////////
			if (iErrorCode != WSA_IO_PENDING)
			{
				// �ý��� �α� �����
				if (WSAENOBUFS == iErrorCode)
					CCrashDump::Crash();
				
				///////////////////////////////////////////////////////////////////////////////
				// Ŭ���̾�Ʈ �ʿ��� ������ ������ ���
				///////////////////////////////////////////////////////////////////////////////
				if ((WSAECONNABORTED != iErrorCode) &&
					(WSAECONNRESET != iErrorCode) &&
					(WSAESHUTDOWN != iErrorCode))
					CCrashDump::Crash();

				if (0 == InterlockedDecrement((LONG *)&pSession->_lIOCount))
					ReleaseSession(pSession);
			}
		}
		PRO_END(L"WSASend");

	} while (0);

	return true;
}



///////////////////////////////////////////////////////////////////////////////////////////
// Recv, Send ó��
///////////////////////////////////////////////////////////////////////////////////////////
bool				CLanServer::CompleteRecv(SESSION *pSession, DWORD dwTransferred)
{
	short header;

	//////////////////////////////////////////////////////////////////////////////
	// RecvQ WritePos �̵�(���� ��ŭ)
	//////////////////////////////////////////////////////////////////////////////
	if (dwTransferred != pSession->_RecvQ.MoveWritePos(dwTransferred))
		CCrashDump::Crash();

	PRO_BEGIN(L"Packet Alloc");
	CNPacket *pPacket = CNPacket::Alloc();
	PRO_END(L"Packet Alloc");

	PRO_BEGIN(L"Recv BufferDeque");
	while (pSession->_RecvQ.GetUseSize() > 0)
	{
		//////////////////////////////////////////////////////////////////////////
		// RecvQ�� ��� ���̸�ŭ �ִ��� �˻� �� ������ Peek
		//////////////////////////////////////////////////////////////////////////
		if (pSession->_RecvQ.GetUseSize() <= sizeof(header))
			break;
		pSession->_RecvQ.Peek((char *)&header, sizeof(header));

		//////////////////////////////////////////////////////////////////////////
		// RecvQ�� ��� ���� + Payload ��ŭ �ִ��� �˻� �� ��� ����
		//////////////////////////////////////////////////////////////////////////
		if (pSession->_RecvQ.GetUseSize() < sizeof(header) + header)
			break;;
		pSession->_RecvQ.RemoveData(sizeof(header));

		//////////////////////////////////////////////////////////////////////////
		// Payload�� ���� �� ��Ŷ Ŭ������ ����
		//////////////////////////////////////////////////////////////////////////
		pPacket->PutData((unsigned char *)pSession->_RecvQ.GetReadBufferPtr(), header);
		pSession->_RecvQ.RemoveData(header);

		//////////////////////////////////////////////////////////////////////////
		// OnRecv ȣ��
		//////////////////////////////////////////////////////////////////////////
		OnRecv(pSession->_iSessionID, pPacket);

		InterlockedIncrement((LONG *)&_lRecvPacketCounter);
	}
	PRO_END(L"Recv BufferDeque");

	PRO_BEGIN(L"Packet Free");
	pPacket->Free();
	PRO_END(L"Packet Free");

	PRO_BEGIN(L"RecvPost");
	RecvPost(pSession);
	PRO_END(L"RecvPost");

	return true;
}

bool				CLanServer::CompleteSend(SESSION *pSession, DWORD dwTransferred)
{
	CNPacket*	pPacket;
	int iSentCnt;

	//////////////////////////////////////////////////////////////////////////////
	// ������ �Ϸ�� ������ ����
	//////////////////////////////////////////////////////////////////////////////
	PRO_BEGIN(L"SentPacket Remove");
	for (iSentCnt = 0; iSentCnt < pSession->_lSentPacketCnt; iSentCnt++)
	{
		pPacket = (CNPacket *)pSession->_pSentPacket[iSentCnt];
		pPacket->Free();
		pSession->_pSentPacket[iSentCnt] = nullptr;
	}

	pSession->_lSentPacketCnt -= iSentCnt;
	PRO_END(L"SentPacket Remove");

	//////////////////////////////////////////////////////////////////////////////
	// �� ���´ٰ� Flag ��ȯ
	//////////////////////////////////////////////////////////////////////////////
	if (false == InterlockedCompareExchange((long *)&pSession->_bSendFlag, false, true))
		CCrashDump::Crash();

	PRO_BEGIN(L"SendPost - WorkerTh");
	pSession->_SendQ.Lock();
	//////////////////////////////////////////////////////////////////////////////
	// ������ ���������� �ٽ� ���
	//////////////////////////////////////////////////////////////////////////////
	SendPost(pSession);
	pSession->_SendQ.Unlock();
	PRO_END(L"SendPost - WorkerTh");

	return true;
}


///////////////////////////////////////////////////////////////////////////////////////////
// �� ���� ���
///////////////////////////////////////////////////////////////////////////////////////////
int					CLanServer::GetBlankSessionIndex()
{
	int iBlankIndex;

	_pBlankStack->Lock();

	if (_pBlankStack->isEmpty())
		iBlankIndex = -1;
	else
		iBlankIndex = _pBlankStack->Pop();

	_pBlankStack->Unlock();

	return iBlankIndex;
}

///////////////////////////////////////////////////////////////////////////////////////////
// ���� �ݳ�
///////////////////////////////////////////////////////////////////////////////////////////
void				CLanServer::InsertBlankSessionIndex(int iSessionIndex)
{
	_pBlankStack->Lock();

	if (_pBlankStack->isFull())
		CCrashDump::Crash();
	else
		_pBlankStack->Push(iSessionIndex);

	_pBlankStack->Unlock();
}


///////////////////////////////////////////////////////////////////////////////////////////
// Disconnection
///////////////////////////////////////////////////////////////////////////////////////////
void				CLanServer::DisconnectSession(SESSION *pSession)
{
	CloseSocket(pSession->_SessionInfo._Socket);
}

///////////////////////////////////////////////////////////////////////////////////////////
// ���� ���� ����
///////////////////////////////////////////////////////////////////////////////////////////
void				CLanServer::CloseSocket(SOCKET socket)
{
	shutdown(socket, SD_BOTH);
}

///////////////////////////////////////////////////////////////////////////////////////////
// Release
///////////////////////////////////////////////////////////////////////////////////////////
void				CLanServer::ReleaseSession(SESSION *pSession)
{
	if ((pSession->_SessionInfo._Socket == INVALID_SOCKET) ||
		(pSession->_iSessionID == -1))
		CCrashDump::Crash();

	closesocket(pSession->_SessionInfo._Socket);
	pSession->_SessionInfo._Socket = INVALID_SOCKET;

	pSession->_SessionInfo._iPort = 0;
	memset(&pSession->_SessionInfo._wIP, 0, sizeof(pSession->_SessionInfo._wIP));

	pSession->_RecvQ.ClearBuffer();
	pSession->_SendQ.ClearBuffer();

	memset(&pSession->_RecvOverlapped, 0, sizeof(OVERLAPPED));
	memset(&pSession->_SendOverlapped, 0, sizeof(OVERLAPPED));

	for (int iSentCnt = 0; iSentCnt < pSession->_lSentPacketCnt; iSentCnt++)
	{
		CNPacket *pPacket = (CNPacket *)pSession->_pSentPacket[iSentCnt];
		pPacket->Free();
		pSession->_pSentPacket[iSentCnt] = nullptr;
	}

	pSession->_lSentPacketCnt = 0;

	InterlockedExchange((long *)&pSession->_bSendFlag, false);

	OnClientLeave(pSession->_iSessionID);

	InsertBlankSessionIndex(GET_SESSIONINDEX(pSession->_iSessionID));

	//pSession->_iSessionID = -1;

	InterlockedDecrement((LONG *)&_lSessionCount);
}



///////////////////////////////////////////////////////////////////////////////////////////
// ���� ������ �κ�
///////////////////////////////////////////////////////////////////////////////////////////
unsigned __stdcall	CLanServer::AcceptThread(LPVOID AcceptParam)
{
	return ((CLanServer *)AcceptParam)->AccpetThread_update();
}

unsigned __stdcall	CLanServer::WorkerThread(LPVOID WorkerParam)
{
	return ((CLanServer *)WorkerParam)->WorkerThread_update();
}

unsigned __stdcall	CLanServer::MonitorThread(LPVOID MonitorParam)
{
	return ((CLanServer *)MonitorParam)->MonitorThread_update();
}
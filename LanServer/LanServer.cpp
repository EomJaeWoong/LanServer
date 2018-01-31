#include "stdafx.h"

//-----------------------------------------------------------------------------------------
// 생성자, 소멸자
//-----------------------------------------------------------------------------------------
CLanServer::CLanServer()
{
	CNPacket::_ValueSizeCheck();
	CCrashDump::CCrashDump();

	//if (!CNPacket::_ValueSizeCheck())
	//	CCrashDump::Crash();

	_Session = new SESSION[MAX_SESSION];

	///////////////////////////////////////////////////////////////////////////////
	// 빈 세션 생성
	///////////////////////////////////////////////////////////////////////////////
	for (int iCnt = 0; iCnt < MAX_SESSION; iCnt++)
	{
		///////////////////////////////////////////////////////////////////////////
		// 세션 정보 구조체 초기화
		///////////////////////////////////////////////////////////////////////////
		_Session[iCnt]._SessionInfo._Socket = INVALID_SOCKET;
		memset(&_Session[iCnt]._SessionInfo._wIP, 0, sizeof(_Session[iCnt]._SessionInfo._wIP));
		_Session[iCnt]._SessionInfo._iPort = 0;

		///////////////////////////////////////////////////////////////////////////
		// 세션 초기화
		///////////////////////////////////////////////////////////////////////////
		_Session[iCnt]._iSessionID = -1;

		memset(&_Session[iCnt]._SendOverlapped, 0, sizeof(OVERLAPPED));
		memset(&_Session[iCnt]._RecvOverlapped, 0, sizeof(OVERLAPPED));

		_Session[iCnt]._SendQ.ClearBuffer();
		_Session[iCnt]._RecvQ.ClearBuffer();

		_Session[iCnt]._bSendFlag = false;
		_Session[iCnt]._lIOCount = 0;

		//_Session[iCnt]._Debug = new WSABUF[2];
	}

	
	///////////////////////////////////////////////////////////////////////////////
	// LanServer 변수 설정
	///////////////////////////////////////////////////////////////////////////////
	_iSessionID = 0;
}

CLanServer::~CLanServer()
{

}


//-----------------------------------------------------------------------------------------
// 서버 시작
//-----------------------------------------------------------------------------------------
bool				CLanServer::Start(WCHAR* wOpenIP, int iPort, int iWorkerThreadNum, bool bNagle, int iMaxConnect)
{
	int result;

	///////////////////////////////////////////////////////////////////////////////////////
	// 윈속 초기화
	///////////////////////////////////////////////////////////////////////////////////////
	WSADATA wsa;
	if (0 != WSAStartup(MAKEWORD(2, 2), &wsa))
		return false;

	///////////////////////////////////////////////////////////////////////////////////////
	// IO Completion Port 생성
	///////////////////////////////////////////////////////////////////////////////////////
	_hIOCP = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
	if (NULL == _hIOCP)
		return false;

	///////////////////////////////////////////////////////////////////////////////////////
	// 리슨소켓 생성
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
	// Thread 생성
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

	if (iWorkerThreadNum > MAX_THREAD)
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
// 서버 멈춤
///////////////////////////////////////////////////////////////////////////////////////////
void				CLanServer::Stop()
{
	
}


///////////////////////////////////////////////////////////////////////////////////////////
// 연결 끊기
///////////////////////////////////////////////////////////////////////////////////////////
bool				CLanServer::SendPacket(__int64 iSessionID, CNPacket *pPacket)
{
	for (int iCnt = 0; iCnt < MAX_SESSION; iCnt++)
	{
		if (iSessionID == _Session[iCnt]._iSessionID)
		{
			pPacket->SetCustomShortHeader(pPacket->GetDataSize());

			_Session[iCnt]._SendQ.Lock();
			int iPutSize = _Session[iCnt]._SendQ.Put(
				(char *)pPacket->GetBufferHeaderPtr(),
				pPacket->GetDataSize()
				);

			///////////////////////////////////////////////////////////////////////////////
			// SendQ가 가득 찼을 때
			///////////////////////////////////////////////////////////////////////////////
			/*
			if (iPutSize < pPacket->GetDataSize())
			{
				_Session[iCnt]._SendQ.Put(
					(char *)pPacket->GetBufferHeaderPtr() + (pPacket->GetDataSize() - iPutSize))
			}
			*/

			SendPost(&_Session[iCnt]);

			_Session[iCnt]._SendQ.Unlock();

			InterlockedIncrement((LONG *)&_lSendPacketCounter);
			break;
		}
	}

	return true;
}


///////////////////////////////////////////////////////////////////////////////////////////
// 연결 끊기
///////////////////////////////////////////////////////////////////////////////////////////
bool				CLanServer::Disconnect(__int64 iSessionID)
{
	for (int iCnt = 0; iCnt < MAX_SESSION; iCnt++)
	{
		if (_Session[iCnt]._iSessionID == iSessionID)
		{
			DisconnectSession(&_Session[iCnt]);
			break;
		}
	}
}

///////////////////////////////////////////////////////////////////////////////////////////
// 실제 동작하는 스레드 부분
///////////////////////////////////////////////////////////////////////////////////////////
int					CLanServer::AccpetThread_update()
{
	HANDLE			result;

	SOCKET			ClientSocket;
	SOCKADDR_IN		ClientAddr;
	int				iAddrLen = sizeof(SOCKADDR_IN);

	SESSIONINFO		SessionInfo;

	while (1)
	{
		ClientSocket = accept(_ListenSocket, (SOCKADDR *)&ClientAddr, &iAddrLen);
		if (INVALID_SOCKET == ClientSocket)
		{
			int iErrorCode = WSAGetLastError();
			return -1;
		}

		///////////////////////////////////////////////////////////////////////////////////
		// 세션 접속 정보 생성
		///////////////////////////////////////////////////////////////////////////////////
		SessionInfo._Socket = ClientSocket;
		InetNtop(AF_INET, &ClientAddr.sin_addr, SessionInfo._wIP, 16);
		SessionInfo._iPort = ntohs(ClientAddr.sin_port);

		///////////////////////////////////////////////////////////////////////////////////
		// 접속 요청
		///////////////////////////////////////////////////////////////////////////////////
		if (!OnConnectionRequest(&SessionInfo))
			continue;
		
		///////////////////////////////////////////////////////////////////////////////////
		// 빈 세션 찾아 세션 생성
		///////////////////////////////////////////////////////////////////////////////////
		for (int iCnt = 0; iCnt < MAX_SESSION; iCnt++)
		{
			if (-1 == _Session[iCnt]._iSessionID)
			{
				_Session[iCnt]._SessionInfo = SessionInfo;

				_Session[iCnt]._iSessionID = InterlockedIncrement((LONG *)&_iSessionID);

				/*
				_Session[iCnt]._bSendFlag = false;
				_Session[iCnt]._lIOCount = 0;

				_Session[iCnt]._SendQ.ClearBuffer();
				_Session[iCnt]._RecvQ.ClearBuffer();

				memset(&_Session[iCnt]._RecvOverlapped, 0, sizeof(OVERLAPPED));
				memset(&_Session[iCnt]._SendOverlapped, 0, sizeof(OVERLAPPED));
				*/
				/////////////////////////////////////////////////////////////////////
				// IOCP 등록
				/////////////////////////////////////////////////////////////////////
				result = CreateIoCompletionPort((HANDLE)_Session[iCnt]._SessionInfo._Socket,
					_hIOCP,
					(ULONG_PTR)&_Session[iCnt],
					0);
				if (!result)
					PostQueuedCompletionStatus(_hIOCP, 0, 0, 0);

				InterlockedIncrement64((LONG64 *)&_Session[iCnt]._lIOCount);

				/////////////////////////////////////////////////////////////////////
				// OnClientJoin
				// 컨텐츠쪽에 세션이 들어왔음을 알림
				/////////////////////////////////////////////////////////////////////
				OnClientJoin(&_Session[iCnt]._SessionInfo, _Session[iCnt]._iSessionID);

				InterlockedIncrement((LONG *)&_lAcceptCounter);
				InterlockedIncrement((LONG *)&_lAcceptTotalCounter);

				RecvPost(&_Session[iCnt]);

				break;
			}
		}
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

		result = GetQueuedCompletionStatus(
			_hIOCP,
			&dwTransferred,
			(PULONG_PTR)&pSession,
			&pOverlapped,
			INFINITE);

		OnWorkerThreadBegin();

		///////////////////////////////////////////////////////////////////////////////////
		// Error, 종료 처리
		///////////////////////////////////////////////////////////////////////////////////
		// IOCP 에러 서버 종료
		if (result == FALSE && (pOverlapped == NULL || pSession == NULL))
		{
			int iErrorCode = WSAGetLastError();
			OnError(iErrorCode, L"IOCP HANDLE Error\n");

			break;
		}

		// 워커스레드 정상 종료
		else if (dwTransferred == 0 && pSession == NULL && pOverlapped == NULL)
		{
			OnError(0, L"Worker Thread Done.\n");
			PostQueuedCompletionStatus(_hIOCP, 0, NULL, NULL);
			return 0;
		}

		//----------------------------------------------------------------------------
		// 정상종료
		// 클라이언트 에서 closesocket() 혹은 shutdown() 함수를 호출한 시점
		//----------------------------------------------------------------------------
		else if (dwTransferred == 0)
		{
			CCrashDump::Crash();
				
			DisconnectSession(pSession);

			continue;
		}
		//----------------------------------------------------------------------------

		if (pOverlapped == &pSession->_RecvOverlapped)
			CompleteRecv(pSession, dwTransferred);

		if (pOverlapped == &pSession->_SendOverlapped)
			CompleteSend(pSession, dwTransferred);

		DWORD lIOCount = InterlockedDecrement((LONG *)&pSession->_lIOCount);
		if (0 == lIOCount)
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
		_lPacketPoolTPS = 0;

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
// Recv, Send 등록
///////////////////////////////////////////////////////////////////////////////////////////
void				CLanServer::RecvPost(SESSION *pSession)
{
	int result, iCount = 1;
	DWORD dwRecvSize, dwFlag = 0;
	WSABUF wBuf[2];

	///////////////////////////////////////////////////////////////////////////////////////
	// WSABUF 등록
	///////////////////////////////////////////////////////////////////////////////////////
	wBuf[0].buf = pSession->_RecvQ.GetWriteBufferPtr();
	wBuf[0].len = pSession->_RecvQ.GetNotBrokenPutSize();

	///////////////////////////////////////////////////////////////////////////////////////
	// 공간이 남아있을 경우 남은 공간 등록
	///////////////////////////////////////////////////////////////////////////////////////
	if (pSession->_RecvQ.GetFreeSize() > pSession->_RecvQ.GetNotBrokenPutSize())
	{
		wBuf[1].buf = pSession->_RecvQ.GetBufferPtr();
		wBuf[1].len = pSession->_RecvQ.GetFreeSize() -
						pSession->_RecvQ.GetNotBrokenPutSize();

		iCount++;
	}
	
	memset(&pSession->_RecvOverlapped, 0, sizeof(OVERLAPPED));

	InterlockedIncrement((LONG *)&pSession->_lIOCount);

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
		// WSA_IO_PENDING -> Overlapped 연산이 준비되었으나 완료되지 않은 경우
		// 이 외에는 에러로 봄
		///////////////////////////////////////////////////////////////////////////////////
		if (iErrorCode != WSA_IO_PENDING)
		{
			CCrashDump::Crash();

			if (0 == InterlockedDecrement((LONG *)&pSession->_lIOCount))
				ReleaseSession(pSession);
		}
	}
}

bool				CLanServer::SendPost(SESSION *pSession)
{
	int result, iCount = 1;
	DWORD dwSendSize, dwFlag = 0;
	WSABUF wBuf[2];

	do
	{
		///////////////////////////////////////////////////////////////////////////////////
		// SendFlag 확인 및 변경
		///////////////////////////////////////////////////////////////////////////////////
		if (true == InterlockedCompareExchange((long *)&pSession->_bSendFlag, true, false))
			break;

		memset(wBuf, 0, sizeof(WSABUF) * 2);

		///////////////////////////////////////////////////////////////////////////////////
		// SendQ사이즈 다시 확인
		///////////////////////////////////////////////////////////////////////////////////
		if (0 == pSession->_SendQ.GetUseSize())
		{
			InterlockedExchange((long *)&pSession->_bSendFlag, false);
			if (0 < pSession->_SendQ.GetUseSize())		
				continue;

			else			break;
		}

		///////////////////////////////////////////////////////////////////////////////////////
		// WSABUF 등록
		///////////////////////////////////////////////////////////////////////////////////////
		wBuf[0].buf = pSession->_SendQ.GetReadBufferPtr();
		wBuf[0].len = pSession->_SendQ.GetNotBrokenGetSize();

		pSession->_Debug[0].buf = pSession->_SendQ.GetReadBufferPtr();
		pSession->_Debug[0].len = pSession->_SendQ.GetNotBrokenGetSize();

		///////////////////////////////////////////////////////////////////////////////////////
		// 공간이 남아있을 경우 남은 공간 등록
		///////////////////////////////////////////////////////////////////////////////////////
		if (pSession->_SendQ.GetUseSize() > pSession->_SendQ.GetNotBrokenGetSize())
		{
			wBuf[1].buf = pSession->_SendQ.GetBufferPtr();
			wBuf[1].len = pSession->_SendQ.GetUseSize() -
				pSession->_SendQ.GetNotBrokenGetSize();

			pSession->_Debug[1].buf = pSession->_SendQ.GetBufferPtr();
			pSession->_Debug[1].len = pSession->_SendQ.GetUseSize() -
				pSession->_SendQ.GetNotBrokenGetSize();

			iCount++;
		}

		memset(&pSession->_SendOverlapped, 0, sizeof(OVERLAPPED));

		InterlockedIncrement((LONG *)&pSession->_lIOCount);

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
			// WSA_IO_PENDING -> Overlapped 연산이 준비되었으나 완료되지 않은 경우
			// 이 외에는 에러로 봄
			///////////////////////////////////////////////////////////////////////////////////
			if (iErrorCode != WSA_IO_PENDING)
			{
				CCrashDump::Crash();

				if (0 == InterlockedDecrement((LONG *)&pSession->_lIOCount))
					ReleaseSession(pSession);
			}
		}
	} while (0);

	return true;
}



///////////////////////////////////////////////////////////////////////////////////////////
// Recv, Send 처리
///////////////////////////////////////////////////////////////////////////////////////////
bool				CLanServer::CompleteRecv(SESSION *pSession, DWORD dwTransferred)
{
	short header;

	//////////////////////////////////////////////////////////////////////////////
	// RecvQ WritePos 이동(받은 만큼)
	//////////////////////////////////////////////////////////////////////////////
	if (dwTransferred != pSession->_RecvQ.MoveWritePos(dwTransferred))
		CCrashDump::Crash();

	CNPacket *pPacket = CNPacket::Alloc();

	while (pSession->_RecvQ.GetUseSize() > 0)
	{
		//////////////////////////////////////////////////////////////////////////
		// RecvQ에 헤더 길이만큼 있는지 검사 후 있으면 Peek
		//////////////////////////////////////////////////////////////////////////
		if (pSession->_RecvQ.GetUseSize() <= sizeof(header))
			break;
		pSession->_RecvQ.Peek((char *)&header, sizeof(header));

		//////////////////////////////////////////////////////////////////////////
		// RecvQ에 헤더 길이 + Payload 만큼 있는지 검사 후 헤더 제거
		//////////////////////////////////////////////////////////////////////////
		if (pSession->_RecvQ.GetUseSize() < sizeof(header) + header)
			break;;
		pSession->_RecvQ.RemoveData(sizeof(header));

		//////////////////////////////////////////////////////////////////////////
		// Payload를 뽑은 후 패킷 클래스에 넣음
		//////////////////////////////////////////////////////////////////////////
		pPacket->PutData((unsigned char *)pSession->_RecvQ.GetReadBufferPtr(), header);
		pSession->_RecvQ.RemoveData(header);

		//////////////////////////////////////////////////////////////////////////
		// OnRecv 호출
		//////////////////////////////////////////////////////////////////////////
		OnRecv(pSession->_iSessionID, pPacket);

		InterlockedIncrement((LONG *)&_lRecvPacketCounter);
	}

	pPacket->Free();

	RecvPost(pSession);

	return true;
}

bool				CLanServer::CompleteSend(SESSION *pSession, DWORD dwTransferred)
{
	pSession->_SendQ.Lock();
	//////////////////////////////////////////////////////////////////////////////
	// 보내기 완료된 데이터 제거
	//////////////////////////////////////////////////////////////////////////////
	pSession->_SendQ.RemoveData(dwTransferred);
	
	//////////////////////////////////////////////////////////////////////////////
	// 다 보냈다고 Flag 변환
	//////////////////////////////////////////////////////////////////////////////
	InterlockedExchange((long *)&pSession->_bSendFlag, false);

	//////////////////////////////////////////////////////////////////////////////
	// 보낼게 남아있으면 다시 등록
	//////////////////////////////////////////////////////////////////////////////
	SendPost(pSession);
	pSession->_SendQ.Unlock();

	return true;
}



///////////////////////////////////////////////////////////////////////////////////////////
// Disconnection
///////////////////////////////////////////////////////////////////////////////////////////
void				CLanServer::DisconnectSession(SESSION *pSession)
{
	shutdown(pSession->_SessionInfo._Socket, SD_SEND);
}

///////////////////////////////////////////////////////////////////////////////////////////
// Release
///////////////////////////////////////////////////////////////////////////////////////////
void				CLanServer::ReleaseSession(SESSION *pSession)
{
	
}



///////////////////////////////////////////////////////////////////////////////////////////
// 실제 스레드 부분
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
/*
 * Client.cpp
 *
 *  Created on: 2015-11-10
 *      Author: Max
 */

#include "Client.h"

Client::Client() {
	// TODO Auto-generated constructor stub
	fd = -1;
	isOnline = false;
    ip = "";

    mBuffer.Reset();

    miSendingPacket = 0;
    miSentPacket = 0;

    miDataPacketRecvSeq = 0;

	mpClientCallback = NULL;
	mpIdleMessageList = NULL;

	mbError = false;
}

Client::~Client() {
	// TODO Auto-generated destructor stub
	mpIdleMessageList = NULL;
}

void Client::SetClientCallback(ClientCallback* pClientCallback) {
	mpClientCallback = pClientCallback;
}

void Client::SetMessageList(MessageList* pIdleMessageList) {
	mpIdleMessageList = pIdleMessageList;
}

int Client::ParseData(Message* m)  {
	int ret = 0;

	char* buffer = m->buffer;
	int len = m->len;
	int seq = m->seq;

	mKMutex.lock();

	LogManager::GetLogManager()->Log(
			LOG_STAT,
			"Client::ParseData( "
			"tid : %d, "
			"[收到客户端数据包], "
			"miDataPacketRecvSeq : %d, "
			"seq : %d "
			")",
			(int)syscall(SYS_gettid),
			miDataPacketRecvSeq,
			seq
			);

	if( mbError ) {
		LogManager::GetLogManager()->Log(
				LOG_STAT,
				"Client::ParseData( "
				"tid : %d, "
				"[客户端数据包已经解析错误, 不再处理] "
				")",
				(int)syscall(SYS_gettid)
				);

		ret = -1;
	}

	if( ret != -1 ) {
		if( miDataPacketRecvSeq != seq ) {
			// 收到客户端乱序数据包
			LogManager::GetLogManager()->Log(
					LOG_STAT,
					"Client::ParseData( "
					"tid : %d, "
					"[收到客户端乱序的数据包] "
					")",
					(int)syscall(SYS_gettid)
					);

			// 缓存到队列
			Message* mc = mpIdleMessageList->PopFront();
			if( mc != NULL ) {
				LogManager::GetLogManager()->Log(
						LOG_STAT,
						"Client::ParseData( "
						"tid : %d, "
						"[缓存客户端乱序的数据包到队列] "
						")",
						(int)syscall(SYS_gettid)
						);

				if( len > 0 ) {
					memcpy(mc->buffer, buffer, len);
				}

				mc->len = len;
				mMessageMap.Insert(seq, mc);

			} else {
				LogManager::GetLogManager()->Log(
						LOG_WARNING,
						"Client::ParseData( "
						"tid : %d, "
						"[客户端没有缓存空间] "
						")",
						(int)syscall(SYS_gettid)
						);

				ret = -1;
			}

		} else {
			// 收到客户端顺序的数据包
			LogManager::GetLogManager()->Log(
					LOG_STAT,
					"Client::ParseData( "
					"tid : %d, "
					"[收到客户端顺序的数据包] "
					")",
					(int)syscall(SYS_gettid)
					);

			ret = 1;
		}

		// 开始解析数据包
		if( ret == 1 ) {
			LogManager::GetLogManager()->Log(
					LOG_STAT,
					"Client::ParseData( "
					"tid : %d, "
					"[开始解析客户端当前数据包] "
					")",
					(int)syscall(SYS_gettid)
					);

			// 解当前数据包
			ret = ParseCurrentPackage(m);
			if( ret != -1 ) {
				miDataPacketRecvSeq++;

				LogManager::GetLogManager()->Log(
						LOG_STAT,
						"Client::ParseData( "
						"tid : %d, "
						"[开始解析客户端缓存数据包] "
						")",
						(int)syscall(SYS_gettid)
						);

				// 解析缓存数据包
				MessageMap::iterator itr;
				while( true ) {
					itr = mMessageMap.Find(miDataPacketRecvSeq);
					if( itr != mMessageMap.End() ) {
						// 找到对应缓存包
						LogManager::GetLogManager()->Log(
								LOG_STAT,
								"Client::ParseData( "
								"tid : %d, "
								"[找到对应缓存包], "
								"miDataPacketRecvSeq : %d "
								")",
								(int)syscall(SYS_gettid),
								miDataPacketRecvSeq
								);

						Message* mc = itr->second;
						ret = ParseCurrentPackage(mc);

						mpIdleMessageList->PushBack(mc);
						mMessageMap.Erase(itr);

						if( ret != -1 ) {
							miDataPacketRecvSeq++;

						} else {
							break;
						}

					} else {
						// 已经没有缓存包
						LogManager::GetLogManager()->Log(
								LOG_STAT,
								"Client::ParseData( "
								"tid : %d, "
								"[已经没有缓存包]"
								")",
								(int)syscall(SYS_gettid)
								);
						break;
					}
				}
			}
		}
	}

	if( ret == -1 ) {
		if( !mbError ) {
			LogManager::GetLogManager()->Log(
					LOG_WARNING,
					"Client::ParseData( "
					"tid : %d, "
					"[客户端数据包解析错误] "
					")",
					(int)syscall(SYS_gettid)
					);

		}

		mbError = true;
	}

	LogManager::GetLogManager()->Log(
			LOG_STAT,
			"Client::ParseData( "
			"tid : %d, "
			"[解析客户端数据包完成], "
			"ret : %d "
			")",
			(int)syscall(SYS_gettid),
			ret
			);

	mKMutex.unlock();

	return ret;
}

int Client::ParseCurrentPackage(Message* m) {
	int ret = 0;

	char* buffer = m->buffer;
	int len = m->len;

	LogManager::GetLogManager()->Log(
			LOG_STAT,
			"Client::ParseCurrentPackage( "
			"tid : %d, "
			"[解析当前数据包], "
			"miDataPacketRecvSeq : %d "
			")",
			(int)syscall(SYS_gettid),
			miDataPacketRecvSeq
			);

	int last = len;
	int recved = 0;
	while( ret != -1 && last > 0 ) {
		int recv = (last < MAX_BUFFER_LEN - mBuffer.len)?last:(MAX_BUFFER_LEN - mBuffer.len);
		if( recv > 0 ) {
			memcpy(mBuffer.buffer + mBuffer.len, buffer + recved, recv);
			mBuffer.len += recv;
			last -= recv;
			recved += recv;
		}

		LogManager::GetLogManager()->Log(
				LOG_STAT,
				"Client::ParseCurrentPackage( "
				"tid : %d, "
				"[解析数据], "
				"mBuffer.len : %d, "
				"recv : %d, "
				"last : %d, "
				"len : %d "
				")",
				(int)syscall(SYS_gettid),
				mBuffer.len,
				recv,
				last,
				len
				);

		// 解析命令
		DataHttpParser parser;
		int parsedLen = parser.ParseData(mBuffer.buffer, mBuffer.len);
		while( parsedLen != 0 ) {
			if( parsedLen != -1 ) {
				// 解析成功
				ParseCommand(&parser);

				// 替换数据
				mBuffer.len -= parsedLen;
				if( mBuffer.len > 0 ) {
					memcpy(mBuffer.buffer, mBuffer.buffer + parsedLen, mBuffer.len);
				}

				LogManager::GetLogManager()->Log(
						LOG_STAT,
						"Client::ParseCurrentPackage( "
						"tid : %d, "
						"[替换数据], "
						"mBuffer.len : %d "
						")",
						(int)syscall(SYS_gettid),
						mBuffer.len
						);

				ret = 1;

				// 继续解析
//				parsedLen = parser.ParseData(mBuffer.buffer, mBuffer.len);
				// 短连接只解析一次
				break;

			} else {
				// 解析错误
				ret = -1;
				break;
			}
		}
	}

	LogManager::GetLogManager()->Log(
			LOG_STAT,
			"Client::ParseCurrentPackage( "
			"tid : %d, "
			"[解析当前数据包], "
			"ret : %d "
			")",
			(int)syscall(SYS_gettid),
			ret
			);

	return ret;
}

void Client::ParseCommand(DataHttpParser* pDataHttpParser) {
	LogManager::GetLogManager()->Log(
			LOG_STAT,
			"Client::ParseCommand( "
			"tid : %d, "
			"[解析命令], "
			"pDataHttpParser->GetPath() : %s "
			")",
			(int)syscall(SYS_gettid),
			pDataHttpParser->GetPath().c_str()
			);
	if( pDataHttpParser->GetPath() == "/GETDIALPLAN" ) {
		// 进入会议室
		if( mpClientCallback != NULL ) {
			const string rtmpSession = pDataHttpParser->GetParam("rtmpSession");
			const string channelId = pDataHttpParser->GetParam("channelId");
			const string conference = pDataHttpParser->GetParam("conference");
			const string serverId = pDataHttpParser->GetParam("serverId");
			const string siteId = pDataHttpParser->GetParam("siteId");

			LogManager::GetLogManager()->Log(
					LOG_STAT,
					"Client::ParseCommand( "
					"tid : %d, "
					"[解析命令:获取拨号计划] "
					")",
					(int)syscall(SYS_gettid)
					);

			mpClientCallback->OnClientGetDialplan(this, rtmpSession, channelId, conference, serverId, siteId);
		}
	} else {
		if( mpClientCallback != NULL ) {
			LogManager::GetLogManager()->Log(
					LOG_STAT,
					"Client::ParseCommand( "
					"tid : %d, "
					"[解析命令:未知命令] "
					")",
					(int)syscall(SYS_gettid)
					);
			mpClientCallback->OnClientUndefinedCommand(this);
		}
	}
}

int Client::AddSendingPacket() {
	int seq;
	mKMutexSend.lock();
	seq = miSendingPacket++;
	mKMutexSend.unlock();
	return seq;
}

int Client::AddSentPacket() {
	int seq;
	mKMutexSend.lock();
	seq = miSentPacket++;
	mKMutexSend.unlock();
	return seq;
}

bool Client::IsAllPacketSent() {
	bool bFlag = false;
	mKMutexSend.lock();
	if( miSentPacket == miSendingPacket ) {
		bFlag = true;
	}
	mKMutexSend.unlock();
	return bFlag;
}

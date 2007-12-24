#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include "tftpserver.h"
#include "filefactory.h"

#ifdef DS
#define socklen_t int
#else
#include <sys/ioctl.h>
#endif

#include <nds.h>
#include <memory>

extern void WaitForVBlank();
extern void ResetTimer();
extern int GetTimer();

#define TFTP_TIMEOUT 2
#define TFTP_MAX_TIMEOUTS 10

#define TFTP_PORT 69
#define	TFTP_DEFAULT_BLOCKSIZE 512
#define TFTP_HEADERSIZE 4

#define	TFTP_MSG_RRQ   01  // read request
#define	TFTP_MSG_WRQ   02  // write request
#define	TFTP_MSG_DATA  03  // data packet
#define	TFTP_MSG_ACK   04  // acknowledgement
#define	TFTP_MSG_ERROR 05  // error code
#define	TFTP_MSG_OACK  06  // option acknowledgement

struct TftpMsg
{
	short op;
};

struct TftpMsgRrq
{
	short op;
	char options[];
};

struct TftpMsgWrq
{
	short op;
	char options[];
};

struct TftpMsgData
{
	short op;
	short block;
	char data[];
};

struct TftpMsgAck
{
	short op;
	short block;
};

struct TftpMsgError
{
	short op;
	short error;
	char message[];
};

struct TftpMsgOAck
{
	short op;
	char options[];
};

#define	TFTP_EUNDEF    0  // Not defined, see error message (if any).
#define	TFTP_ENOTFOUND 1  // File not found.
#define	TFTP_EACCESS   2  // Access violation.
#define	TFTP_ENOSPACE  3  // Disk full or allocation exceeded.
#define	TFTP_EBADOP    4  // Illegal TFTP operation.
#define	TFTP_EBADID    5  // Unknown transfer ID.
#define	TFTP_EEXISTS   6  // File already exists.
#define	TFTP_ENOUSER   7  // No such user.

#define THROW_ERRNO(s) {char e[1024]; sprintf(e, "%s: %s (%i) (%s:%i)", s, strerror(errno), errno, __FILE__, __LINE__); throw e;}
#define THROW(s) throw s;

TftpServer::TftpServer()
{
	// create socket
	sock = socket(AF_INET, SOCK_DGRAM, 0);
	if(sock == -1) { THROW_ERRNO("socket"); }

	// bind to default port
	struct sockaddr_in sain;
	sain.sin_family = AF_INET;
	sain.sin_port = htons(TFTP_PORT);
	sain.sin_addr.s_addr = INADDR_ANY;
	int result = bind(sock, (struct sockaddr *)&sain, sizeof(sain));
	if(result == -1) { THROW_ERRNO("bind"); }

	// set socket to non-blocking
	int i = 1;
	ioctl(sock, FIONBIO, &i);

	printf("TFTP server ready...\n");
}

TftpServer::~TftpServer()
{
	close(sock);
}

bool TftpServer::Poll()
{
	socklen_t remotelen = sizeof(remote);
	char buffer[2048];

	int count = recvfrom(
		sock,
		buffer,
		sizeof(buffer),
		0,
		(struct sockaddr *)&remote,
		&remotelen);
	if(count == -1)
	{
		if(errno != EAGAIN)
		{
			THROW_ERRNO("recvfrom");
		}
		return false;
	}

	unsigned int ip = remote.sin_addr.s_addr;
	printf("%u.%u.%u.%u:%u connected\n",
		(ip >>  0) & 0xFF,
		(ip >>  8) & 0xFF,
		(ip >> 16) & 0xFF,
		(ip >> 24) & 0xFF,
		ntohs(remote.sin_port));

	timeouts = 0;

	try
	{
		TftpMsg* msg = (TftpMsg*)buffer;
		switch(ntohs(msg->op))
		{
		case TFTP_MSG_RRQ:
			{
				TftpMsgRrq* rrqMsg = (TftpMsgRrq*)msg;
				ParseOptions(rrqMsg->options, count-2);
				SendFile();
			}
			break;

		case TFTP_MSG_WRQ:
			{
				TftpMsgWrq* wrqMsg = (TftpMsgWrq*)msg;
				ParseOptions(wrqMsg->options, count-2);
				ReceiveFile();
			}
			break;

		default:
			THROW("Unexpected operation");
		}
	}
	catch(const char* exception)
	{
		printf("\nError: %s\n", exception);
		SendError(exception);
	}

	return true;
}

void TftpServer::ReceiveFile()
{
	File* file = FileFactory::OpenFile(filename, true);

	if(blocksize != -1)
	{
		SendOAck();
	}
	else
	{
		blocksize = TFTP_DEFAULT_BLOCKSIZE;
		SendAck(0);
	}


	unsigned short lastReceivedBlock = 0;
	unsigned int bytesReceived = 0;
	int length = blocksize;
	std::auto_ptr<char> buffer(new char[TFTP_HEADERSIZE + blocksize]);
	SendAck(lastReceivedBlock);
	printf("Received: \e[s    0 k");
	while(length == blocksize)
	{
		int count = ReceiveMsg(buffer.get());
		if(count < 0)
		{
			SendAck(lastReceivedBlock);
			continue;
		}

		TftpMsg* msg = (TftpMsg*)buffer.get();
		switch(ntohs(msg->op))
		{
		case TFTP_MSG_DATA:
			{
				TftpMsgData* dataMsg = (TftpMsgData*)msg;
				if(ntohs(dataMsg->block) != ((lastReceivedBlock+1) & 0xFFFF))
				{
					printf("\e[u\e[0Kout of order (%u)", lastReceivedBlock);
					
					// we should only send an ack if the block number is too high
					// see http://en.wikipedia.org/wiki/Sorcerer%27s_Apprentice_Syndrome
					if(ntohs(dataMsg->block) > ((lastReceivedBlock+1) & 0xFFFF))
					{
						SendAck(lastReceivedBlock);
					}

					continue;
				}
				lastReceivedBlock = (lastReceivedBlock + 1) & 0xFFFF;
				length = count - TFTP_HEADERSIZE;
				if(length > 0)
				{
					file->Write(dataMsg->data, length);
				}

				bytesReceived += length;
				printf("\e[u\e[0K%5u k", bytesReceived >> 10);
			}
			break;

		case TFTP_MSG_ERROR:
			{
				TftpMsgError* errMsg = (TftpMsgError*)msg;
				THROW(errMsg->message);
			}

		default:
			THROW("Unexpected operation");
		}

		SendAck(lastReceivedBlock);
	}

	file->Close();
	delete file;
	printf("\nFile received successfully.\n");
}

void TftpServer::SendFile()
{
	if(blocksize != -1)
	{
		SendOAck();
	}
	else
	{
		blocksize = TFTP_DEFAULT_BLOCKSIZE;
	}

	File* file = FileFactory::OpenFile(filename, false);

	printf("Sent: \e[s    0 k");

	unsigned short lastSentBlock = 0;
	unsigned int blocksAcked = 0;
	int length = blocksize;
	std::auto_ptr<char> buffer(new char[TFTP_HEADERSIZE + blocksize]);
	while(length == blocksize)
	{
		lastSentBlock = (lastSentBlock + 1) & 0xFFFF;
		TftpMsgData* data = (TftpMsgData*)buffer.get();
		data->op = htons(TFTP_MSG_DATA);
		data->block = htons(lastSentBlock);
		length = file->Read(data->data, blocksize);
		bool acked = false;
		while(!acked)
		{
			SendDataMsg(buffer.get(), TFTP_HEADERSIZE + length);

			if(ReceiveMsg(buffer.get()) < 0)
			{
				continue;
			}

			TftpMsg* msg = (TftpMsg*)buffer.get();
			switch(ntohs(msg->op))
			{
			case TFTP_MSG_ACK:
				{
					TftpMsgAck* ackMsg = (TftpMsgAck*)msg;
					if(ntohs(ackMsg->block) == lastSentBlock)
					{
						acked = true;
						blocksAcked++;
						printf("\e[u\e[0K%5u k", blocksAcked/2);
					}
				}
				break;

			case TFTP_MSG_ERROR:
				{
					TftpMsgError* errMsg = (TftpMsgError*)msg;
					THROW(errMsg->message);
				}

			default:
				THROW("Unexpected operation");
			}
		}
	}

	file->Close();
	delete file;
	printf("\nFile sent successfully.\n");
}

int TftpServer::ReceiveMsg(void* buffer)
{
	int count;
	ResetTimer();
	do
	{
		socklen_t remotelen = sizeof(remote);
		count = recvfrom(
			sock,
			buffer,
			TFTP_HEADERSIZE + blocksize,
			0,
			(struct sockaddr *)&remote, &remotelen);
		if(count >= 0)
		{
			timeouts = 0;
			return count;
		}
	}
	while(GetTimer() < TFTP_TIMEOUT);

	printf("\e[u\e[0Ktimeout %i", timeouts);
	timeouts++;
	if(timeouts > TFTP_MAX_TIMEOUTS)
	{
		THROW("Transfer timed out");
	}
	return -1;
}

void TftpServer::SendDataMsg(void* buffer, int length)
{
	int count = sendto(
		sock,
		buffer,
		length,
		0,
		(struct sockaddr *)&remote,
		sizeof(remote));
	if(count == -1) { THROW_ERRNO("sendto"); }
}

void TftpServer::SendAck(int block)
{
	TftpMsgAck ackMsg;
	ackMsg.op = htons(TFTP_MSG_ACK);
	ackMsg.block = htons(block);
	
	int count = sendto(sock, &ackMsg, sizeof(ackMsg), 0, (struct sockaddr *)&remote, sizeof(remote));
	if(count == -1) { THROW_ERRNO("sendto"); }
}

void TftpServer::SendError(const char* error)
{
	int length = TFTP_HEADERSIZE + strlen(error) + 1;
	TftpMsgError* errMsg = (TftpMsgError*)new char[length];
	
	errMsg->op = htons(TFTP_MSG_ERROR);
	// TODO: curl ignores the message, so we should set the correct error code
	errMsg->error = htons(TFTP_EACCESS);
	strcpy(errMsg->message, error);

	sendto(sock, errMsg, length, 0, (struct sockaddr *)&remote, sizeof(remote));

	delete errMsg;
}

void TftpServer::ParseOptions(const char* options, int length)
{
	const char* ptr = options;
	const char* end = ptr + length;
	filename = ptr;
	ptr += strlen(filename) + 1;
	mode = ptr;
	ptr += strlen(mode) + 1;
	blocksize = -1;
	while(ptr < end)
	{
		const char* option = ptr;
		ptr += strlen(option) + 1;
		const char* value = ptr;
		ptr += strlen(value) + 1;
		printf("option: %s=%s\n", option, value);
		if(strcmp(option, "blksize") == 0)
		{
			sscanf(value, "%i", &blocksize);
		}
	}
}

void TftpServer::SendOAck()
{
	char buffer[1024];
	TftpMsgOAck* msg = (TftpMsgOAck*)buffer;
	msg->op = htons(TFTP_MSG_OACK);
	strcpy(msg->options, "blksize");
	char str[1024];
	sprintf(str, "%i", blocksize);
	strcpy(msg->options + strlen("blksize") + 1, str);
	int count = sendto(
		sock,
		buffer,
		2 + strlen("blksize") + 1 + strlen(str) + 1,
		0,
		(struct sockaddr *)&remote,
		sizeof(remote));
	if(count == -1) { THROW_ERRNO("sendto"); }
}

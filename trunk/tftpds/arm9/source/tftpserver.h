#pragma once

#include <sys/socket.h>
#include <netinet/in.h>

class TftpServer
{
public:
	TftpServer();
	~TftpServer();

	bool Poll();

private:
	void ReceiveFile();
	void SendFile();
	int ReceiveMsg(void* buffer);
	void SendDataMsg(void* buffer, int length);
	void SendAck(int block);
	void SendError(const char* error);
	void ParseOptions(const char* options, int length);
	void SendOAck();
	
	int sock;
	struct sockaddr_in remote;
	int timeouts;
	const char* filename;
	const char* mode;
	int blocksize;
};

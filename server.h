#include <sys/types.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstring>
#include <algorithm>
#include <iostream>
#include <vector>
#include <ctime>

constexpr int maxEvents = 100; // в случае надобности увеличить
constexpr int tcpPort = 10001;
constexpr int udpPort = 10002;
constexpr int maxPayloadChunk = 2048;

using FileDiscriptor = int;
using IPv4Addr = struct sockaddr_in;
using SocketLen = socklen_t;
using GenericAddr = struct sockaddr;

struct ServerStats 
{
    int totalClients = 0;
    int connectedClients = 0;
};

class Server
{
public:
	Server() noexcept = default;
	~Server();

	void start();

private:
	FileDiscriptor tcpListenSock = -1;
	FileDiscriptor udpSock = -1;
	FileDiscriptor epoll = -1;
	std::vector<FileDiscriptor> clientsSockets;
	bool isServerRunning = true;
	ServerStats serverStats;

	bool setNonblocking(FileDiscriptor socket);
	bool intializeTCP();
	bool initializeUDP();

	bool createEpoll();
	bool addSocketToEpoll(FileDiscriptor sockToAdd);

	void processSocketTCP(FileDiscriptor &tcpListenSock);
	void processClientsSockets(FileDiscriptor &sock);
	void processClientsTCP(FileDiscriptor &sock);
	void processClientsUDP(FileDiscriptor &sock);

	void messageHandler(FileDiscriptor sock, char* buf, size_t len, bool isUdp = false,
                    GenericAddr* udpAddr = nullptr, SocketLen udpLen = 0);
	void sendMessage(FileDiscriptor sock, const char* msg, size_t len, bool isUdp,
                  GenericAddr* udpAddr = nullptr, SocketLen udpLen = 0);

	void closeInstance(int &fdToClose);
};
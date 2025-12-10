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
	int tcpListenSock = -1;
	int udpSock = -1;
	int epoll_fd = -1;
	bool isServerRunning = true;
	std::vector<int> clientsSockets; // TODO - всем дескрипторам сделать псевдоним через using
	ServerStats serverStats;

	bool setNonblocking(int socket);
	bool intializeTCP();
	bool initializeUDP();

	bool createEpoll();
	bool addSocketToEpoll(int sockToAdd);

	void processSocketTCP(int current_fd);
	void processClientsSockets(int current_fd);
	void messageHandler(int sock, char* buf, size_t len, bool is_udp = false,
                    struct sockaddr_in* udp_addr = nullptr, socklen_t udp_len = 0);
	void sendMessage(int sock, const char* msg, size_t len, bool is_udp,
                  struct sockaddr_in* udp_addr = nullptr, socklen_t udp_len = 0);

	void closeInstance(int &fdToClose);
};
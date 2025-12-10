#include "server.h"

bool Server::setNonblocking(int socket) 
{
    int flags = fcntl(socket, F_GETFL, 0);
    if(flags == -1) 
    	return false;

    if(fcntl(socket, F_SETFL, flags | O_NONBLOCK) == -1)
    	return false;

    return true;
}

bool Server::intializeTCP()
{
    struct sockaddr_in server_addr;

    tcpListenSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if(tcpListenSock == -1)
    	return false; // TODO - писать в поток лога

    int optval = 1;
    if(setsockopt(tcpListenSock, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) == -1)
    	return false; // TODO - писать в поток лога

    // memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(tcpPort);

    if(bind(tcpListenSock, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1)
    {
        closeInstance(tcpListenSock);
        tcpListenSock = -1;
        return false;
    }

    if(listen(tcpListenSock, SOMAXCONN) == -1) 
    {
        closeInstance(tcpListenSock);
        tcpListenSock = -1;
        return false;
    }

    return setNonblocking(tcpListenSock);
}

bool Server::initializeUDP()
{
    struct sockaddr_in server_addr;

    udpSock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if(udpSock == -1)
        return false; // TODO - писать в поток лога

    int optval = 1;
    if(setsockopt(udpSock, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) == -1)
    {
        closeInstance(udpSock);
        return false;
    }

    // memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(udpPort);

    if(bind(udpSock, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1)
    {
        closeInstance(udpSock);
        return false;
    }

    return setNonblocking(udpSock);
}

bool Server::createEpoll()
{
	epoll_fd = epoll_create1(0);
    if(epoll_fd == -1) 
        return false;

    return true;
}

bool Server::addSocketToEpoll(int sockToAdd)
{
	if(sockToAdd == -1)
		return false;

	struct epoll_event event;
    event.events = EPOLLIN | EPOLLET;
    event.data.fd = sockToAdd;
    if(epoll_ctl(epoll_fd, EPOLL_CTL_ADD, sockToAdd, &event) == -1)
        return false;

    return true;
}

void Server::start()
{
	bool tcpOk = intializeTCP();
	bool udpOk = initializeUDP();

	if(!tcpOk || !udpOk)
	{
		std::cout << "can`t create/bind sockets" << std::endl;
		return;
	}

	if(!createEpoll())
	{
		std::cout << "can`t create epoll instance" << std::endl;
		return;
	}

	if(!addSocketToEpoll(tcpListenSock) || !addSocketToEpoll(udpSock))
	{
		std::cout << "can`t add sockets to epoll instance" << std::endl;
		return;
	}

	struct epoll_event events[maxEvents];
	while(isServerRunning) // главный цикл сервера
	{
        int num_ready = epoll_wait(epoll_fd, events, maxEvents, -1);
        if(num_ready == -1)
        {
        	std::cout << "error in main loop. Exiting..." << std::endl;
        	return;
        }

        for (int i = 0; i < num_ready; ++i) 
        {
            int current_fd = events[i].data.fd;

            if(current_fd == tcpListenSock) 
            	processSocketTCP(current_fd);
            else 
            	processClientsSockets(current_fd); //обработка клиентов и udp сокета
        }
    }

}

void Server::processSocketTCP(int current_fd)
{
    while(true) 
    {   
    	// Принимаем запрос на подключение
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_sock = accept(tcpListenSock, (struct sockaddr*)&client_addr, &client_len);
        if(client_sock == -1)
        {
            if(errno == EAGAIN || errno == EWOULDBLOCK) 
                break; // больше нет подключений
            else 
            {
                std::cout << "error is ocured, while accepting connection. skipping..." << std::endl;
                break;
            }
        }

        if(!setNonblocking(client_sock) || !addSocketToEpoll(client_sock))
        {
        	std::cout << "can`t create client`s socket" << std::endl;
        	closeInstance(client_sock);
        }

        clientsSockets.push_back(client_sock);
        serverStats.totalClients += 1;
        serverStats.connectedClients += 1;
    }
}

void Server::processClientsSockets(int current_fd)
{
    char buf[maxPayloadChunk];
    if(current_fd != udpSock) 
    {
        while(true) 
        {
            int bytes = recv(current_fd, buf, sizeof(buf), 0);

            if(bytes > 0) 
                messageHandler(current_fd, buf, bytes); // обрабатываем команды или зеркалим сообщение

            else if(bytes == 0) // клиент закрыл соединение
            {
                closeInstance(current_fd);
                
                clientsSockets.erase(std::remove(clientsSockets.begin(), clientsSockets.end(),
                 current_fd), clientsSockets.end());

                serverStats.connectedClients -= 1;
                break;
            }
            else 
            {
                if(errno == EAGAIN || errno == EWOULDBLOCK) 
                    break; // данные закончились

                else // ошибка сокета
                {
                    
                    std::cout << "socket error, closing socket..." << std::endl;
                    closeInstance(current_fd);
                	clientsSockets.erase(std::remove(clientsSockets.begin(), clientsSockets.end(),
                 	current_fd), clientsSockets.end());

                    serverStats.connectedClients -= 1;
                    break;
                }
            }
        }
    } 
    else // обработка UDP сокета
    {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);

        int bytes = recvfrom(udpSock, buf, sizeof(buf), 0,
                             (struct sockaddr*)&client_addr, &addr_len);

        if(bytes > 0)
            messageHandler(udpSock, buf, bytes, true, &client_addr, addr_len);
    }
}

void Server::messageHandler(int sock, char* buf, size_t len, bool is_udp,
                    struct sockaddr_in* udp_addr, socklen_t udp_len)
{
    if(buf[0] == '/') 
    {
        std::string response;

        if(strncmp(buf, "/time", 5) == 0) 
        {
            std::time_t now = std::time(nullptr);
            char time_buf[64];
            std::strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", std::localtime(&now));
            response = time_buf;

            sendMessage(sock, response.c_str(), response.size(), is_udp, udp_addr, udp_len);

        } 
        else if(strncmp(buf, "/stats", 6) == 0) 
        {
            response = "Total clients: " + std::to_string(serverStats.totalClients) +
                       ", Connected now: " + std::to_string(serverStats.connectedClients);

            sendMessage(sock, response.c_str(), response.size(), is_udp, udp_addr, udp_len);

        } 
        else if(strncmp(buf, "/shutdown", 9) == 0) 
        {
            std::cout << "Shutdown command received! server shutting down..." << std::endl;
			isServerRunning = false;
        } 
        else 
        	std::cout << "unknown command. skipping..." << std::endl;

    }
    else 
        sendMessage(sock, buf, len, is_udp, udp_addr, udp_len); // зеркалим сообщение
}

void Server::closeInstance(int &fdToClose)
{
	if(fdToClose >= 0)
	{
		close(fdToClose);
		fdToClose = -1;
	}
}

Server::~Server()
{
	closeInstance(tcpListenSock);
	closeInstance(udpSock);

	for(auto clientSock : clientsSockets)
		closeInstance(clientSock);

	closeInstance(epoll_fd);
}

void Server::sendMessage(int sock, const char* msg, size_t len, bool is_udp,
                  struct sockaddr_in* udp_addr, socklen_t udp_len) 
{
    if(is_udp && udp_addr)
        sendto(sock, msg, len, 0, (struct sockaddr*)udp_addr, udp_len);
    else 
        send(sock, msg, len, 0);
    
}
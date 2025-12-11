#include "server.h"

bool Server::setNonblocking(FileDiscriptor socket) 
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
    IPv4Addr serverAddr;

    tcpListenSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if(tcpListenSock == -1)
    	return false; // TODO - писать в поток лога

    int optval = 1;
    if(setsockopt(tcpListenSock, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) == -1)
    	return false; // TODO - писать в поток лога

    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(tcpPort);

    if(bind(tcpListenSock, reinterpret_cast<GenericAddr*>(&serverAddr), sizeof(serverAddr)) == -1)
    {
        closeInstance(tcpListenSock);
        return false;
    }

    if(listen(tcpListenSock, SOMAXCONN) == -1) 
    {
        closeInstance(tcpListenSock);
        return false;
    }

    return setNonblocking(tcpListenSock);
}

bool Server::initializeUDP()
{
    IPv4Addr serverAddr;

    udpSock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if(udpSock == -1)
        return false; // TODO - писать в поток лога

    int optval = 1;
    if(setsockopt(udpSock, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) == -1)
    {
        closeInstance(udpSock);
        return false;
    }

    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(udpPort);

    if(bind(udpSock, (GenericAddr*)&serverAddr, sizeof(serverAddr)) == -1)
    {
        closeInstance(udpSock);
        return false;
    }

    return setNonblocking(udpSock);
}

bool Server::createEpoll()
{
	epoll = epoll_create1(0);
    if(epoll == -1) 
        return false;

    return true;
}

bool Server::addSocketToEpoll(FileDiscriptor sockToAdd)
{
	if(sockToAdd == -1)
		return false;

	struct epoll_event event;
    event.events = EPOLLIN | EPOLLET;
    event.data.fd = sockToAdd;
    if(epoll_ctl(epoll, EPOLL_CTL_ADD, sockToAdd, &event) == -1)
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
        int numReady = epoll_wait(epoll, events, maxEvents, -1);
        if(numReady == -1)
        {
        	std::cout << "error in main loop. Exiting..." << std::endl;
        	return;
        }

        for (int i = 0; i < numReady; ++i) 
        {
            int sock = events[i].data.fd;

            if(sock == tcpListenSock) 
            	processSocketTCP(sock);
            else 
            	processClientsSockets(sock); //обработка клиентов и udp сокета
        }
    }

}

void Server::processSocketTCP(FileDiscriptor &tcpListenSock)
{
    while(true) 
    {   
    	// Принимаем запрос на подключение
        IPv4Addr clientAddr;
        SocketLen clientLen = sizeof(clientAddr);

        FileDiscriptor clientSock = accept(tcpListenSock, reinterpret_cast<GenericAddr*>(&clientAddr), &clientLen);
        if(clientSock == -1)
        {
            if(errno == EAGAIN || errno == EWOULDBLOCK) 
                break; // больше нет подключений
            else 
            {
                std::cout << "error is ocured, while accepting connection. skipping..." << std::endl;
                break;
            }
        }

        if(!setNonblocking(clientSock) || !addSocketToEpoll(clientSock))
        {
        	std::cout << "can`t create client`s socket" << std::endl;
        	closeInstance(clientSock);
        }

        clientsSockets.push_back(clientSock);
        serverStats.totalClients += 1;
        serverStats.connectedClients += 1;
    }
}

void Server::processClientsSockets(FileDiscriptor &sock)
{
    if(sock != udpSock) 
    	processClientsTCP(sock);
    else
    	processClientsUDP(sock);
}

void Server::processClientsTCP(FileDiscriptor &sock)
{
	char buf[maxPayloadChunk];
    while(true) 
    {
        int bytes = recv(sock, buf, sizeof(buf), 0);

        if(bytes > 0) 
            messageHandler(sock, buf, bytes); // обрабатываем команды или зеркалим сообщение

        else if(bytes == 0) // клиент закрыл соединение
        {
            closeInstance(sock);
            
            clientsSockets.erase(std::remove(clientsSockets.begin(), clientsSockets.end(),
             sock), clientsSockets.end());

            serverStats.connectedClients -= 1;
            break;
        }
        else if(bytes == -1)
        {
            if(errno == EAGAIN || errno == EWOULDBLOCK) 
                break; // данные закончились

            else // ошибка сокета
            {
                
                std::cout << "socket error, closing socket..." << std::endl;
                closeInstance(sock);
            	clientsSockets.erase(std::remove(clientsSockets.begin(), clientsSockets.end(),
             	sock), clientsSockets.end());

                serverStats.connectedClients -= 1;
                break;
            }
        }
    }
}

void Server::processClientsUDP(FileDiscriptor &sock)
{
	char buf[maxPayloadChunk];
    while(true) 
    {
	    IPv4Addr clientAddr;
	    SocketLen addrLen = sizeof(clientAddr);

	    int bytes = recvfrom(udpSock, buf, sizeof(buf), 0,
	                         reinterpret_cast<GenericAddr*>(&clientAddr), &addrLen);

	    if(bytes > 0)
	        messageHandler(udpSock, buf, bytes, true, reinterpret_cast<GenericAddr*>(&clientAddr), addrLen);
	    else
	    {
	    	if((errno == EAGAIN || errno == EWOULDBLOCK)) 
        		break; // данные закончились
        	else
        	{
        		std::cout << "problem has ocured on udp socket. skipping...";
        		break;
        	}
	    }
	}
}

void Server::messageHandler(FileDiscriptor sock, char* buf, size_t len, bool isUdp,
                    GenericAddr* udpAddr, socklen_t udpLen)
{
    if(buf[0] == '/') 
    {
        std::string response;

        if(strncmp(buf, "/time", 5) == 0 && len == 5) 
        {
            std::time_t now = std::time(nullptr);
            char timeBuf[64];
            std::strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d %H:%M:%S", std::localtime(&now));
            response = timeBuf;

            sendMessage(sock, response.c_str(), response.size(), isUdp, udpAddr, udpLen);

        } 
        else if(strncmp(buf, "/stats", 6) == 0 && len == 6) 
        {
            response = "Total clients: " + std::to_string(serverStats.totalClients) +
                       ", Connected now: " + std::to_string(serverStats.connectedClients);

            sendMessage(sock, response.c_str(), response.size(), isUdp, udpAddr, udpLen);

        } 
        else if(strncmp(buf, "/shutdown", 9) == 0 && len == 9) 
        {
            std::cout << "Shutdown command received! server shutting down..." << std::endl;
			isServerRunning = false;
			closeInstance(epoll);
        } 
        else 
        	std::cout << "unknown command. skipping..." << std::endl;

    }
    else 
        sendMessage(sock, buf, len, isUdp, udpAddr, udpLen); // зеркалим сообщение
}

void Server::closeInstance(FileDiscriptor &fdToClose)
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

	closeInstance(epoll);
}

void Server::sendMessage(FileDiscriptor sock, const char* msg, size_t len, bool isUdp,
                  GenericAddr* udpAddr, socklen_t udpLen) 
{
    if(isUdp && udpAddr)
        sendto(sock, msg, len, 0, udpAddr, udpLen);
    else 
        send(sock, msg, len, 0);
    
}
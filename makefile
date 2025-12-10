server: main.o server.o
	g++ -std=c++17 -Wall -O2 -o server main.o server.o

main.o: main.cpp server.h
	g++ -std=c++17 -Wall -O2 -c main.cpp

server.o: server.cpp server.h
	g++ -std=c++17 -Wall -O2 -c server.cpp

clean:
	rm -f *.o server
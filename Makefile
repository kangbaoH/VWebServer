server:	main.cpp ./Connection/Connection.cpp ./ThreadPool/ThreadPool.cpp
		g++ -std=c++14 -O2 -g -pthread main.cpp ./Connection/Connection.cpp ./ThreadPool/ThreadPool.cpp -o server

clean:
	rm -f *.o server

server:	main.cpp ./Connection/Connection.cpp ./ThreadPool/ThreadPool.cpp ./TimerWheel/TimerWheel.cpp ./WebServer/WebServer.cpp ./Logger/Logger.cpp
		g++ -std=c++14 -O2 -g -pthread \
		main.cpp                       \
		./Connection/Connection.cpp    \
		./ThreadPool/ThreadPool.cpp    \
		./TimerWheel/TimerWheel.cpp    \
		./WebServer/WebServer.cpp      \
		./Logger/Logger.cpp            \
		-o server

clean:
	rm -f *.o server

clean-logs:
	rm -f Logs/*.log

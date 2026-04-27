server:	main.cpp ./Connection/Connection.cpp ./ThreadPool/ThreadPool.cpp ./TimerWheel/TimerWheel.cpp
		g++ -std=c++14 -O2 -g -pthread \
		main.cpp \
		./Connection/Connection.cpp \
		./ThreadPool/ThreadPool.cpp \
		./TimerWheel/TimerWheel.cpp \
		-o server

clean:
	rm -f *.o server

CXX ?= g++

DEBUG ?= 1
ifeq ($(DEBUG), 1)
    CXXFLAGS += -g
else
    CXXFLAGS += -O2

endif

server: main.cpp  ./timer/lst_timer.cc ./http/http_conn.cc ./log/log.cc ./CGImysql/sql_connection_pool.cc  ./webserver/webserver.cpp ./config/config.cpp
	$(CXX) -o server.exe  $^ $(CXXFLAGS) -lpthread -lmysqlclient

clean:
	rm  -r server

CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -pthread -O2
LDFLAGS = -pthread
WX_FLAGS = $(shell wx-config --cxxflags --libs)

all: server client

server: server_main.cpp
	$(CXX) $(CXXFLAGS) server_main.cpp -o hangman_server

client: gui_client.cpp
	$(CXX) $(CXXFLAGS) gui_client.cpp -o hangman_client $(WX_FLAGS)

clean:
	rm -f hangman_server hangman_client

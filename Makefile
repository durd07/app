CC = gcc
CXX = g++
CFLAGS = -Wall -Wextra -pedantic -std=c99 -D_POSIX_C_SOURCE=200809L
CXXFLAGS = -Wall -Wextra -pedantic -std=c++11
LDFLAGS = -lgd -lzbar -I `pkg-config --libs opencv`

# C source files
CSOURCES = gpio.c daemon.c
COBJECTS = $(CSOURCES:.c=.o)

# C++ source files
CXXSOURCES = qr_scanner.cpp
CXXOBJECTS = $(CXXSOURCES:.cpp=.o)

STATICLIBS = fswebcam/fswebcam.a

# Output executable
TARGET = app

all: $(TARGET)

$(TARGET): $(COBJECTS) $(CXXOBJECTS) $(STATICLIBS)
	$(CXX) $(LDFLAGS) $^ -o $@

$(COBJECTS): %.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

$(CXXOBJECTS): %.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(STATICLIBS):
	$(MAKE) -C fswebcam

clean:
	rm -f $(TARGET) $(COBJECTS) $(CXXOBJECTS) $(STATICLIBS)


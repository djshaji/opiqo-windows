.PHONY: all clean

OPUS = -I/usr/x86_64-w64-mingw32/sys-root/mingw/include/opus -lopus
OGG = -I/usr/x86_64-w64-mingw32/sys-root/mingw/include/ogg -logg
VORBIS = -I/usr/x86_64-w64-mingw32/sys-root/mingw/include/vorbis -lvorbis -lvorbisfile
FLAC = -I/usr/x86_64-w64-mingw32/sys-root/mingw/include/flac -lFLAC
FFTW = -I/usr/x86_64-w64-mingw32/sys-root/mingw/include/fftw3 -lfftw3
DLFCN = -ldl
MINGW_LIB_DIR = /usr/x86_64-w64-mingw32/sys-root/mingw/lib

CC = x86_64-w64-mingw32-gcc
CXX = x86_64-w64-mingw32-g++
AR = x86_64-w64-mingw32-ar
RANLIB = x86_64-w64-mingw32-ranlib

INCLUDES = -I./src -I./include -I./include/lilv-0 -I./include/serd-0 -I./include/sord-0 -I./include/sratom-0 -I./include/zix-0

CFLAGS = -Wall -Wextra -O2 $(INCLUDES)
CXXFLAGS = -Wall -Wextra -O2 $(INCLUDES) -w
LDFLAGS = -L./libs
CORE_STATIC_LIBS = ./libs/liblilv-0.a ./libs/libmp3lame.a
STATIC_LIBS = $(wildcard ./libs/*dll.a)
EXTRA_STATIC_LIBS = $(filter-out $(CORE_STATIC_LIBS),$(STATIC_LIBS))

TARGET = bin/opiqo.exe
OBJS = LiveEffectEngine.o FileWriter.o main.o LockFreeQueue.o
LIBS = $(CORE_STATIC_LIBS) $(EXTRA_STATIC_LIBS) -L$(MINGW_LIB_DIR) -lopus -lvorbisenc -lvorbis -lvorbisfile -logg -lFLAC -lfftw3 -lm $(DLFCN)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS) $(LIBS)

LockFreeQueue.o: src/LockFreeQueue.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

LiveEffectEngine.o: src/LiveEffectEngine.cpp LockFreeQueue.o
	$(CXX) $(CXXFLAGS) $(OPUS) $(OGG) $(VORBIS) $(FLAC) $(FFTW) -c $< -o $@

FileWriter.o: src/FileWriter.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@ $(OPUS) $(OGG) $(VORBIS) $(FLAC) $(FFTW)

main.o: src/main.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@ $(OPUS) $(OGG) $(VORBIS) $(FLAC) $(FFTW)

clean:
	rm -f $(OBJS) $(TARGET)

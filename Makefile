all: release debug microterm microtermd chat chatd

release: dashel.h dashel-private.h dashel-posix.cpp
	g++ -O3 `pkg-config --cflags hal` -c dashel-posix.cpp -o dashel-posix.o
	g++ -O3 `pkg-config --cflags hal` -c dashel-common.cpp -o dashel-common.o
	ar rcs libdashel.a dashel-posix.o dashel-common.o

debug: dashel.h dashel-private.h dashel-posix.cpp
	g++ -g `pkg-config --cflags hal` -c dashel-posix.cpp -o dashel-posix.o
	g++ -g `pkg-config --cflags hal` -c dashel-common.cpp -o dashel-common.o
	ar rcs libdasheld.a dashel-posix.o dashel-common.o

microterm: release
	g++ -O3 microterm.cpp `pkg-config --libs hal` libdashel.a -o microterm

microtermd: debug
	g++ -g microterm.cpp `pkg-config --libs hal` libdasheld.a -o microtermd

chat: release
	g++ -O3 chat.cpp `pkg-config --libs hal` libdashel.a -o chat

chatd: debug
	g++ -g chat.cpp `pkg-config --libs hal` libdasheld.a -o chatd


clean:
	rm -f dashel-posix.o dashel-common.o *~

distclean: clean
	rm -f libdashel.a libdasheld.a microterm microtermd chat chatd

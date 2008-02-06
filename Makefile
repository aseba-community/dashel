all: release debug microterm microtermd

release: dashel.h dashel-private.h dashel-posix.cpp
	g++ -O3 `pkg-config --cflags hal` -c dashel-posix.cpp -o dashel-posix.o
	ar rcs libdashel.a dashel-posix.o

debug: dashel.h dashel-private.h dashel-posix.cpp
	g++ -g `pkg-config --cflags hal` -c dashel-posix.cpp -o dashel-posix.o
	ar rcs libdasheld.a dashel-posix.o

microterm: release
	g++ -O3 microterm.cpp `pkg-config --libs hal` libdashel.a -o microterm

microtermd: debug
	g++ -g microterm.cpp `pkg-config --libs hal` libdasheld.a -o microtermd

clean:
	rm -f dashel-posix.o *~

distclean: clean
	rm -f libdashel.a libdasheld.a microterm microtermd

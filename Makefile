all: release debug

release: streams.h streams.cpp
	g++ -O3 `pkg-config --cflags hal` -c streams.cpp -o streams.o
	ar rcs libdashel.a streams.o

debug: streams.h streams.cpp
	g++ -g `pkg-config --cflags hal` -c streams.cpp -o streams.o
	ar rcs libdasheld.a streams.o

microterm: release
	g++ -O3 microterm.cpp `pkg-config --libs hal` libdashel.a -o microterm

microtermd: debug
	g++ -g microterm.cpp `pkg-config --libs hal` libdasheld.a -o microtermd

clean:
	rm -f streams.o *~

distclean: clean
	rm -f libdashel.a libdasheld.a microterm microtermd

all: release debug

release: streams.h streams.cpp
	g++ -O3 -c streams.cpp -o streams.o
	ar rcs libdashel.a streams.o

debug: streams.h streams.cpp
	g++ -g -c streams.cpp -o streams.o
	ar rcs libdasheld.a streams.o

clean:
	rm -f streams.o *~

distclean: clean
	rm -f libdashel.a libdasheld.a
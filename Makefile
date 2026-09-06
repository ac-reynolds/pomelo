.PHONY: all clean run

all:
	cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
	cmake --build build -j

clean:
	cmake --build build --target clean

run: all
	./build/pomelo

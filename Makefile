.PHONY: all debug release clean run test

all: debug

debug:
	cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
	cmake --build build -j

release:
	cmake -S . -B build/release -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
	cmake --build build/release -j

clean:
	cmake --build build --target clean

run: debug
	./build/pomelo

test: debug
	ctest --test-dir build --output-on-failure

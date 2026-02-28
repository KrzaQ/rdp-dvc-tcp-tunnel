BUILD_TYPE ?= Release

.PHONY: all conan configure build clean

all: build

conan:
	conan install . --output-folder=build --build=missing -pr:h mingw64 -pr:b default -s build_type=$(BUILD_TYPE)

configure: conan
	cmake -S . -B build/Windows \
		-DCMAKE_TOOLCHAIN_FILE=$(PWD)/build/conan_toolchain.cmake \
		-DCMAKE_BUILD_TYPE=$(BUILD_TYPE) \
		-G Ninja

build: configure
	cmake --build build/Windows

clean:
	rm -rf build

-include Makefile.local

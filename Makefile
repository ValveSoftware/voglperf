#
# Simple makefile which simply launches cmake in the appropriate directories and then launches make.
#
#   make
#   make voglperf32
#   make voglperf64
#   make clean
#

all: voglperf32 voglperf64

voglperf64:
	@mkdir -p build64; cd build64; cmake -DCMAKE_BUILD_TYPE=Release -DBUILD_X64=True ../src; make

voglperf32:
	@mkdir -p build32; cd build32; cmake -DCMAKE_BUILD_TYPE=Release -DBUILD_X64=False ../src; make

clean:
	@rm -rf build64
	@rm -rf build32
	@rm -rf bin/libvoglperf* bin/voglperfrun*

.PHONY: all voglperf32 voglperf64 clean


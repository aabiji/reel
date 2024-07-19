!#/bin/sh

# Generate ninja build files 
if [ ! -d "bin" ]; then
	mkdir bin
	cmake -G Ninja -B bin
fi

# Build
cd bin && ninja && ./showtime && cd ..

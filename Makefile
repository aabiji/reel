
compile_flags = -Wall -Werror -Wextra -Wno-unused -g
linker_flags = -lavformat -lavcodec -lavutil -lavfilter -lswscale -lswresample -lavdevice -lSDL2
outdir = bin
output = showtime
files = src/player.cc src/decode.cc src/main.cc

build:
	mkdir -p ${outdir}
	g++ ${files} ${compile_flags} ${linker_flags} -o ${outdir}/${output}

run: build
	./${outdir}/${output}

debug: build
	gdb ./${outdir}/${output} -ex="set confirm off" -ex="run"
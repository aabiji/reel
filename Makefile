
compile_flags = -Wall -Werror -Wextra -Wno-unused -g
linker_flags = -lavformat -lavcodec -lavutil -lavfilter -lswscale -lSDL2
outdir = bin
output = showtime
files = src/main.c

build:
	mkdir -p ${outdir}
	gcc ${files} ${compile_flags} ${linker_flags} -o ${outdir}/${output}

run: build
	./${outdir}/${output} ~/Videos/rat.webm
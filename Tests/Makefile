CC = ~/build/bin/clang
CCFLAGS = -target tricore-unknown-linux-gnu

#all: 00.null_ret.clang.s  02.mul_test.clang.s 01.hello_test.clang.s
all: 02.mul_test.clang.s
%.clang.s: %.c
	${CC} ${CCFLAGS} -S $< -o $@

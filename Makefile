CC = cc
LFS = -lm
bitonic: bitonic.c
	$(CC) -o $@ $^ $(LFS)

.PHONY: clean
clean:
	rm bitonic

.PHONY: run
run: bitonic
	mpiexec -n 4 ./bitonic

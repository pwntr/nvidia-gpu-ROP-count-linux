all: rop ropmulti ropnvml

rop:
	gcc -o bin/rop src/rop.c

ropmulti:
	gcc -o bin/ropmulti src/ropmulti.c

ropnvml:
	gcc -o bin/ropnvml src/ropnvml.c -lnvidia-ml -I/usr/local/cuda/include

# build the builder image
image: Dockerfile
	docker build -t nvml-builder .

# run the builder image
docker:
	docker run --user "$$(id -u)":"$$(id -u)" -v .:/src --rm nvml-builder make all

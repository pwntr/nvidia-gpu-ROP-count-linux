all: rop ropmulti ropnvml

rop:
	gcc -o bin/rop -Os -flto src/rop.c

ropmulti:
	gcc -o bin/ropmulti -Os -flto src/ropmulti.c

ropnvml:
	gcc -o bin/ropnvml -Os -flto src/ropnvml.c -lnvidia-ml -I/usr/local/cuda/include

# build the builder image
image: Dockerfile
	docker build -t nvml-builder .

# run the builder image
docker:
	docker run --user "$$(id -u)":"$$(id -u)" -v .:/src --rm nvml-builder make all

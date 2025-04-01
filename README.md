# nvidia-gpu-ROP-count-linux
Counts the [ROPs](https://en.wikipedia.org/wiki/Render_output_unit) (render output units) of Nvidia GPUs under Linux. Useful to verify the integrity of your Blackwell (50 series) GPUs, which are known to have some defective units shipped.

## Desired ROP counts:

* **RTX 5090**: 176
* **RTX 5080**: 112
* **RTX 5070 TI**: 96
* **RTX 5070**: 80

see [nvidia-rtx-blackwell-gpu-architecture.pdf](https://images.nvidia.com/aem-dam/Solutions/geforce/blackwell/nvidia-rtx-blackwell-gpu-architecture.pdf) for more details

# Quickstart
Download the latest release from the [release page](https://github.com/pwntr/nvidia-gpu-ROP-count-linux/releases), unpack it and run the desired binary (see below for differences)!

## Prereqs
Make sure you have the latest Nvidia driver installed, and verify that `nvidia-smi` is also working.

## Which binary does what?
* `rop`, the simplest check, outputs the ROP count for a single Nvidia GPU:
    ```
    $ bin/rop
    ROP unit count: 12
    ROP operations factor: 8
    ROP operations count: 96
    ```
* `ropmulti` outputs the ROP count for all the Nvidia GPUs in the system:
    ```
    $ bin/ropmulti
    --- Processing GPU 0 /dev/nvidia0 ---
    GPU 0 ROP unit count: 12
    GPU 0 ROP operations factor: 8
    GPU 0 ROP operations count: 96
    Found 1 NVIDIA device(s).
    ```
* `ropnvml` additionally outputs the friendly name of the GPUs in the system:
    ```
    $ bin/ropnvml
    --- Processing GPU 0 /dev/nvidia0 ---
    Name: NVIDIA GeForce RTX 5070 Ti
    ROP unit count: 12
    ROP operations factor: 8
    ROP operations count: 96
    Found 1 NVIDIA device(s).
    ```

# Build
## Prereqs
Local builds requires `gcc` and `make`. For `ropnvml` you need to have `libnvidia-ml-dev` (on Ubuntu, `libnvidia-ml` for Fedora, from the CUDA repo) or the [CUDA Toolkit](https://developer.nvidia.com/cuda-toolkit) installed when building locally. For using the container image based builds, you need to have Docker or Podman installed.

## Make
The provided `Makefile` contains some simple targets:

* `make all` build all the binaries locally
* `make rop / ropmulti / ropnvml`: locally builds only the given target binary
* `make image` builds the Docker image based on the `Dockerfile`, which includes `libnvidia-ml-dev`, `gcc`, and `make`
* `make docker` uses the newly built image from above to run the build process and locally save all binaries into the `bin` directory, which we volume mount as part of this source dir into the running container

# Credit

All the C code was taken from [this Nvidia Developer Forum thread](https://forums.developer.nvidia.com/t/check-the-rop-unit-count-under-linux-affects-all-rtx-50xx-cards/324769/93). I just bundled it up and made it available together with some build scripts and helpers to make it easy to build & run.

## C file authors
* [Miro256](https://forums.developer.nvidia.com/t/check-the-rop-unit-count-under-linux-affects-all-rtx-50xx-cards/324769/58) for `rop.c`
* [bonez](https://forums.developer.nvidia.com/t/check-the-rop-unit-count-under-linux-affects-all-rtx-50xx-cards/324769/95) for `ropmulti.c` and `ropnvml.c`
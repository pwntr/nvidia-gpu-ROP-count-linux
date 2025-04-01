FROM nvidia/cuda:12.8.1-devel-ubuntu24.04

# Install build tools
RUN apt-get update && apt-get install -y build-essential

# Set working directory
WORKDIR /src

ENTRYPOINT []
CMD ["gcc", "--version"]

FROM ubuntu:24.04

# Install build tools
RUN apt-get update && apt-get install -y gcc make libnvidia-ml-dev && apt-get clean

# Set working directory
WORKDIR /src

CMD ["gcc", "--version"]

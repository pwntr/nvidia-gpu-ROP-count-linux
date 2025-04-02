FROM ubuntu:24.04

# Install build tools and libraries and remove apt cache (apt-get clean is automatically run)
RUN apt-get update && apt-get install -y gcc make libnvidia-ml-dev && rm -rf /var/lib/apt/lists/*

# Set working directory
WORKDIR /src

CMD ["gcc", "--version"]

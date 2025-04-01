FROM ubuntu:24.04

# Install build tools
RUN apt-get update && apt-get install -y build-essential libnvidia-ml-dev

# Set working directory
WORKDIR /src

ENTRYPOINT []
CMD ["gcc", "--version"]

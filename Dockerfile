# Use the official GCC image as the base
FROM gcc:latest

# Set the working directory inside the container
WORKDIR /usr/src/app

# Copy all repository files into the container
COPY . .

# Create the data directory, initialize the DB files, and compile the code
RUN make init_db && make

# Expose the port the server runs on
EXPOSE 8085

# Set the default command to start the server
CMD ["./bin/server"]
# gcc image as base
FROM gcc:latest

# set workdir in container
WORKDIR /usr/src/app

# copy c++ in workdir
COPY myserver.cpp .

# compile it
RUN g++ -o myserver myserver.cpp

# port of server
EXPOSE 8080

# run c++ myserver
CMD ["./myserver"]

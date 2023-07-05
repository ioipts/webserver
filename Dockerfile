FROM alpine:3.14
RUN apk --no-cache add gcc g++ make
EXPOSE 8027
CMD ["./webserver", "8027","/data"]
RUN mkdir -p /src
RUN mkdir -p /src/sample
RUN mkdir -p /src/sample/file-folder
RUN mkdir -p /data
WORKDIR /src

COPY axishttpsock.* /src
COPY ./sample/mainwebserver.cpp /src/sample
COPY ./sample/makefile /src/sample
COPY ./sample/file-folder/axisfile.* /src/sample/file-folder
WORKDIR /src/sample
RUN make


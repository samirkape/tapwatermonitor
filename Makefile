export GOOS=linux
export GOARCH=amd64

all: build

build:
	go build -o bootstrap -ldflags="-s -w" ./...

zip: build
	zip deployment.zip bootstrap


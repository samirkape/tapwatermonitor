all: build

build:
	GOOS=linux GOARCH=amd64 go build -v -o bootstrap -ldflags="-s -w" ./...

zip: build
	zip deployment.zip bootstrap

clean:
	rm -f bootstrap deployment.zip
all: build

build:
	env GOOS=linux GOARCH=arm64 go build -v -o bootstrap -ldflags="-s -w" ./...

zip: clean build
	zip deployment.zip bootstrap

clean:
	rm -f bootstrap deployment.zip
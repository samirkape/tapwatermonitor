all: zip

build:
	env GOOS=linux GOARCH=arm64 go build -v -o bootstrap -ldflags="-s -w" ./...

zip: clean build
	zip deployment.zip bootstrap main.go

clean:
	rm -f bootstrap deployment.zip
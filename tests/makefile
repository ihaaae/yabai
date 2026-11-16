.PHONY: clean build run all

all: clean build run

clean:
	rm -rf ./bin

build:
	mkdir -p ./bin
	clang ./src/tests.m -o ./bin/tests -DTESTS -DPROFILE=1 -F/System/Library/PrivateFrameworks -framework Carbon -framework Cocoa -framework CoreServices -framework CoreVideo -framework SkyLight
	clang ./src/mission_control.m -o ./bin/mission_control -framework Cocoa -framework ApplicationServices

run:
	./bin/tests
	./bin/mission_control

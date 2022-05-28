BUILD := build
PORT := /dev/ttyUSB0
SRCS := $(wildcard *.ino)

all: compile upload

compile_commands.json:
	arduino-cli compile                   \
		-b arduino:avr:uno                \
		--build-path $(BUILD)             \
		--build-cache-path $(BUILD)/cache \
		-v --only-compilation-database
	cp $(BUILD)/$@ .

compile: $(SRCS) | $(BUILD)
	arduino-cli compile                   \
		-b arduino:avr:uno                \
		--build-path $(BUILD)             \
		--build-cache-path $(BUILD)/cache \
		-v

upload: compile
	arduino-cli upload                    \
		-b arduino:avr:uno                \
		-p $(PORT)                        \
		--input-dir $(BUILD)              \
		-v

monitor:
	arduino-cli monitor -p $(PORT)

$(BUILD):
	mkdir $@

clean:
	rm -rf $(BUILD)

#
# ffdump makefile
#
# $Id$
#

.PHONY: all clean install

BUILD_DIR = build
OBJECTS = $(addprefix $(BUILD_DIR)/, ffdump.o average_color.o content_sync_detector.o ffdump_opt.o cmdutils.o compare_pictures.o)

INCLUDES = -I..

FFMPEG_LIBS = avformat avcodec avfilter avdevice avutil swresample swscale
BUILT_FFMPEG_LIBS = $(foreach lib, $(FFMPEG_LIBS), -L ../lib$(lib) -l$(lib))

ifeq ($(OTHER_LIBS),)
OTHER_LIBS = -lx264 -lmp3lame -lz -lm
endif

CFLAGS += -std=c99 $(INCLUDES) -Werror

#
# Targets
#

all: ffdump

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

clean:
	rm -rf $(BUILD_DIR)

install:
	cp ffdump $(INSTALL_DIR)

ffdump: $(OBJECTS)
	gcc $(BUILD_DIR)/*.o $(LDFLAGS) $(BUILT_FFMPEG_LIBS) $(OTHER_LIBS) -o ffdump

$(BUILD_DIR)/cmdutils.o: ../cmdutils.c ../cmdutils.h
	gcc -c $< $(CFLAGS) -o $@

$(BUILD_DIR)/%.o: %.c %.h $(BUILD_DIR)
	gcc -c $< $(CFLAGS) -o $@

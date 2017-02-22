# ffmpeg test
NAME = ffmpeg_tests

# Common C/C++ defines
DEFINES  = -D__STDC_CONSTANT_MACROS
# Library specific defines
LIB_DEFINES = -DHAVE_SYSLOG
# C++ flags
CXXFLAGS = -std=c++11 -Wall -Wextra -Werror
# Extra libraries
LIBS = -lavutil -lpthread -lz -lm -lgtest -lgtest_main

ifneq ($(FFMPEG_SRC_DIR),)
# use libs and headers from the build tree
INCLUDES += -I$(FFMPEG_SRC_DIR)

LDFLAGS += -L$(FFMPEG_SRC_DIR)/libavutil
endif

ifneq ($(GTEST_SRC_DIR),)
INCLUDES += -I$(GTEST_SRC_DIR)/include
LDFLAGS += -L$(GTEST_SRC_DIR)
endif

ifneq ($(GTEST_LIB_DIR),)
LDFLAGS += -L$(GTEST_LIB_DIR)
endif

TESTS_DIR = $(FFMPEG_SRC_DIR)/epiphan_tests

TEST_SRCS =  audio_level.cpp circular_buffer.cpp rate_counter.cpp bitrate_calculation.cpp
bin_srcs = $(addprefix $(TESTS_DIR)/src/, $(TEST_SRCS))


.PHONY: clean all

all: tests

tests: $(TESTS_DIR)/$(NAME)

$(TESTS_DIR)/$(NAME): $(bin_srcs)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(DEFINES) $(LDFLAGS) -o $@  $^ $(LIBS)

clean: 
	rm $(TESTS_DIR)/main

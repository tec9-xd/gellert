CC = g++
MAKEFLAGS := --jobs=$(shell nproc)

CFLAGS  = -shared -fPIC -g -std=c++17 -fno-gnu-unique
CFLAGS += -I. -Isrc -Iimgui -Ithirdparty/SDL/include
CFLAGS += -Ifunchook -Ifunchook/include -Ilibs/funchook/include -Ilibsigscan
CFLAGS += -Wno-unused-result -Wno-write-strings

LDFLAGS  = -static-libstdc++ -static-libgcc -lvulkan -ldl -lpthread -g
LDFLAGS += libs/funchook/build/libfunchook.a libs/funchook/build/libdistorm.a

SRC_CPP := $(wildcard src/*.cpp) \
           $(wildcard src/core/*.cpp) \
           $(wildcard src/hooks/*.cpp) \
           $(wildcard src/features/*.cpp) \
           $(wildcard src/gui/*.cpp)

IMGUI_CPP := imgui/imgui.cpp imgui/imgui_draw.cpp imgui/imgui_tables.cpp \
             imgui/imgui_widgets.cpp imgui/imgui_demo.cpp imgui/imgui_stdlib.cpp \
             imgui/imgui_impl_sdl3.cpp imgui/imgui_impl_vulkan.cpp

OBJ  := $(addprefix obj/, $(SRC_CPP:.cpp=.cpp.o))
OBJ  += $(addprefix obj/, $(IMGUI_CPP:.cpp=.cpp.o))
OBJ  += obj/libsigscan/libsigscan.c.o

BIN = cs2.so

.PHONY: all clean

all: $(BIN)

clean:
	rm -rf obj $(BIN)

$(BIN): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

obj/%.cpp.o: %.cpp
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

obj/%.c.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

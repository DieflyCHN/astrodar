CXX ?= c++
CXXFLAGS ?= -O2 -std=c++17 -Wall -Wextra -Wpedantic
LDFLAGS ?=
LDLIBS ?= -pthread
FTXUI_PREFIX ?= $(shell brew --prefix ftxui 2>/dev/null)
ifneq ($(strip $(FTXUI_PREFIX)),)
CXXFLAGS += -I$(FTXUI_PREFIX)/include
LDFLAGS += -L$(FTXUI_PREFIX)/lib -Wl,-rpath,$(FTXUI_PREFIX)/lib
endif
LDLIBS += -lftxui-component -lftxui-dom -lftxui-screen

TARGET := astrodar
SOURCES := linux-astrodar.cpp localization.cpp solar_model.cpp solar_time.cpp solar_events.cpp moon_model.cpp solar_terms.cpp time_scales.cpp

.PHONY: all run clean

all: $(TARGET)

$(TARGET): $(SOURCES) vsop87d_earth.inc
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $(SOURCES) $(LDLIBS)

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)

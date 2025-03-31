BUILD_DIR := ./build
INCLUDE_DIRS := . $(shell find /usr/local/include/*/third_party -type d) # BSONCXX header files are located in weird paths by default
INCLUDE := $(addprefix -I,$(INCLUDE_DIRS))
LIBRARIES := -lcurl-impersonate-chrome -lfmt -llexbor -lmongocxx -lbsoncxx -lbuxtehude -levent_core -levent_pthreads
CXXFLAGS := -std=c++20 -Wall
CPPFLAGS := $(INCLUDE) -MMD -MP
LDFLAGS := $(LIBRARIES) -rpath /usr/local/lib

# Webscraper building

FITSCH_WEBSCRAPER_TARGET := fitsch-webscraper
FITSCH_WEBSCRAPER_SOURCE := $(wildcard webscraper/*.cpp) common/product.cpp common/util.cpp
FITSCH_WEBSCRAPER_OBJECTS := $(FITSCH_WEBSCRAPER_SOURCE:%.cpp=$(BUILD_DIR)/%.o)
FITSCH_WEBSCRAPER_DEPENDENCIES := $(FITSCH_WEBSCRAPER_OBJECTS:%.o=%.d)

$(FITSCH_WEBSCRAPER_TARGET): $(FITSCH_WEBSCRAPER_OBJECTS)
	$(CXX) $(LDFLAGS) $^ -o $@

# Terminal building

FITSCH_TERMINAL_TARGET := fitsch-term
FITSCH_TERMINAL_SOURCE := $(wildcard terminal/*.cpp)
FITSCH_TERMINAL_OBJECTS := $(FITSCH_TERMINAL_SOURCE:%.cpp=$(BUILD_DIR)/%.o)
FITSCH_TERMINAL_DEPENDENCIES := $(FITSCH_TERMINAL_OBJECTS:%.o=%.d)
FITSCH_TERMINAL_LDFLAGS := -lbuxtehude -lfmt -rpath /usr/local/lib

$(FITSCH_TERMINAL_TARGET): $(FITSCH_TERMINAL_OBJECTS)
	$(CXX) $(FITSCH_TERMINAL_LDFLAGS) $^ -o $@

# Webserver building

FITSCH_WEBSERVER_TARGET := fitsch-webserver
FITSCH_WEBSERVER_SOURCE := $(wildcard webserver/*.cpp common/product.cpp)
FITSCH_WEBSERVER_OBJECTS := $(FITSCH_WEBSERVER_SOURCE:%.cpp=$(BUILD_DIR)/%.o)
FITSCH_WEBSERVER_DEPENDENCIES := $(FITSCH_WEBSERVER_OBJECTS:%.o=%.d)
FITSCH_WEBSERVER_LDFLAGS := -lbuxtehude -lfmt -lcurl -rpath /usr/local/lib

$(FITSCH_WEBSERVER_TARGET): $(FITSCH_WEBSERVER_OBJECTS)
	$(CXX) $(FITSCH_WEBSERVER_LDFLAGS) $^ -o $@

# All

all: $(FITSCH_WEBSCRAPER_TARGET) $(FITSCH_TERMINAL_TARGET) $(FITSCH_WEBSERVER_TARGET)

# Generic source building rules

$(BUILD_DIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

.PHONY: clean
clean:
	rm -r $(BUILD_DIR)

-include $(FITSCH_WEBSCRAPER_DEPENDENCIES)
-include $(FITSCH_TERMINAL_DEPENDENCIES)
-include $(FITSCH_WEBSERVER_DEPENDENCIES)

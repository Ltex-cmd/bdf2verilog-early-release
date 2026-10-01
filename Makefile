CXX ?= c++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Wpedantic
CPPFLAGS := -Iinclude -DBDF_CONTEXT_TAPE
SOURCES := src/parser.cpp src/schematic.cpp src/signal_name.cpp src/lpm.cpp src/verilog.cpp src/project.cpp src/bdf_tape.cpp src/bdf_span.cpp src/project_context.cpp
.PHONY: all test test-coordinates test-span test-net-checks test-bus-contacts clean
all: build/bdf-tool build/benchmark-core
build:
	mkdir -p build
build/bdf-tool: $(SOURCES) src/main.cpp $(wildcard include/*.hpp) | build
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) $(SOURCES) src/main.cpp -o $@
build/benchmark-core: $(SOURCES) tools/benchmark_core.cpp $(wildcard include/*.hpp) | build
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) $(SOURCES) tools/benchmark_core.cpp -o $@
build/test-parser: $(SOURCES) tests/test_parser.cpp $(wildcard include/*.hpp) | build
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) $(SOURCES) tests/test_parser.cpp -o $@
build/test-phase2: $(SOURCES) tests/test_phase2.cpp $(wildcard include/*.hpp) | build
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) $(SOURCES) tests/test_phase2.cpp -o $@
build/test-allocation: $(SOURCES) tests/allocation_order.cpp $(wildcard include/*.hpp) | build
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) $(SOURCES) tests/allocation_order.cpp -o $@
build/test-coordinate-numbers: src/parser.cpp src/bdf_tape.cpp tests/test_coordinate_numbers.cpp $(wildcard include/*.hpp) | build
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) src/parser.cpp src/bdf_tape.cpp tests/test_coordinate_numbers.cpp -o $@
test-coordinates: build/test-coordinate-numbers
	./build/test-coordinate-numbers
build/test-name-scope: $(SOURCES) tests/test_name_scope.cpp $(wildcard include/*.hpp) | build
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) $(SOURCES) tests/test_name_scope.cpp -o $@
build/test-span-parser: src/parser.cpp src/schematic.cpp src/bdf_tape.cpp src/bdf_span.cpp tests/test_span_parser.cpp $(wildcard include/*.hpp) | build
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) src/parser.cpp src/schematic.cpp src/bdf_tape.cpp src/bdf_span.cpp tests/test_span_parser.cpp -o $@
test-span: build/test-span-parser
	./build/test-span-parser
build/test-net-checks: $(SOURCES) tests/test_net_checks.cpp $(wildcard include/*.hpp) | build
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) $(SOURCES) tests/test_net_checks.cpp -o $@
test-net-checks: build/test-net-checks
	./build/test-net-checks
build/test-bus-contacts: $(SOURCES) tests/test_bus_contacts.cpp $(wildcard include/*.hpp) | build
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) $(SOURCES) tests/test_bus_contacts.cpp -o $@
test-bus-contacts: build/test-bus-contacts
	./build/test-bus-contacts
test: build/test-parser build/test-phase2 build/test-allocation test-coordinates build/test-name-scope test-span test-net-checks test-bus-contacts
	./build/test-parser
	./build/test-phase2
	./build/test-allocation tests/minimal.bdf build/allocation-minimal
	./build/test-allocation tests/minimal.bdf build/allocation-minimal-repeat
	./build/test-name-scope
clean:
	rm -rf build

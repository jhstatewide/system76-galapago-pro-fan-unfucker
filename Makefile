vpath %.c ../src

CC = gcc
CFLAGS = -c -Wall -std=gnu99 -DHAVE_LIBCAP
LDFLAGS = -lcap

DSTDIR := /usr/local
OPT_DIR := /opt/galago-pro-fan-control-daemon
OBJDIR := obj
SRCDIR := src

# Original monolithic source files
SRC = clevo-indicator.c privilege_manager.c
DAEMON_SRC = clevo-daemon.c clevo-daemon-socket.c clevo-daemon-dbus.c privilege_manager.c logging.c
CLIENT_SRC = clevo-client.c
DBUS_CLIENT_SRC = clevo-dbus-client.c
DIAG_SRC = ec_diagnostic.c
TEST_SRC = test_settings.c

# Modular source files
MODULAR_SRCS = main_new.c \
               utils.c \
               logging.c \
               config.c \
               ec_interface.c \
               temperature_monitor.c \
               pid_controller.c \
               fan_health.c \
               daemon.c \
               live_stats.c \
               clevo-daemon-socket.c \
               privilege_manager.c

# Object files
OBJ = $(patsubst %.c,$(OBJDIR)/%.o,$(SRC))
DAEMON_OBJ = $(patsubst %.c,$(OBJDIR)/%.o,$(DAEMON_SRC))
CLIENT_OBJ = $(patsubst %.c,$(OBJDIR)/%.o,$(CLIENT_SRC))
DBUS_CLIENT_OBJ = $(patsubst %.c,$(OBJDIR)/%.o,$(DBUS_CLIENT_SRC))
DIAG_OBJ = $(patsubst %.c,$(OBJDIR)/%.o,$(DIAG_SRC))
TEST_OBJ = $(patsubst %.c,$(OBJDIR)/%.o,$(TEST_SRC))
MODULAR_OBJ = $(patsubst %.c,$(OBJDIR)/%.o,$(MODULAR_SRCS))

# Targets
TARGET = bin/clevo-indicator
DAEMON_TARGET = bin/clevo-daemon
CLIENT_TARGET = bin/clevo-client
DBUS_CLIENT_TARGET = bin/clevo-dbus-client
DIAG_TARGET = bin/ec_diagnostic
TEST_TARGET = bin/test_settings
MODULAR_TARGET = bin/clevo-daemon-modular

PKG_CONFIG ?= pkg-config

# Check for libcap - try pkg-config first, then check for library directly
HAVE_LIBCAP := $(shell $(PKG_CONFIG) --exists libcap && echo 1)
ifeq ($(HAVE_LIBCAP),)
HAVE_LIBCAP := $(shell test -f /usr/lib/x86_64-linux-gnu/libcap.so && echo 1)
endif

ifeq ($(HAVE_LIBCAP),1)
CFLAGS += -DHAVE_LIBCAP
LDFLAGS += -lcap
endif

# UI-specific flags (only for the main target)
UI_CFLAGS = `pkg-config --cflags ayatana-appindicator3-0.1`
UI_LDFLAGS = `pkg-config --libs ayatana-appindicator3-0.1`

# Default target builds both original and modular versions
all: $(TARGET) $(DAEMON_TARGET) $(CLIENT_TARGET) bin/clevo-dbus-client bin/clevo-client-dbus

# Original targets (for backward compatibility)
original: $(TARGET) $(DAEMON_TARGET) $(CLIENT_TARGET)

# Modular target (optional - may have compilation issues)
modular: $(MODULAR_TARGET)

install: $(TARGET) $(DAEMON_TARGET) $(CLIENT_TARGET) bin/clevo-dbus-client bin/clevo-client-dbus
	@echo Install to ${DSTDIR}/bin/
	@sudo install -m 4750 -g adm $(TARGET) ${DSTDIR}/bin/
	@sudo install -m 4750 -g adm $(DAEMON_TARGET) ${DSTDIR}/bin/
	@sudo install -m 755 $(CLIENT_TARGET) ${DSTDIR}/bin/
	@sudo install -m 755 bin/clevo-dbus-client ${DSTDIR}/bin/
	@sudo install -m 755 bin/clevo-client-dbus ${DSTDIR}/bin/

install-opt: $(TARGET) $(DAEMON_TARGET) $(CLIENT_TARGET)
	@echo Installing to ${OPT_DIR}/
	@sudo mkdir -p ${OPT_DIR}/bin
	@sudo mkdir -p ${OPT_DIR}/etc
	@sudo mkdir -p ${OPT_DIR}/systemd
	@sudo mkdir -p ${OPT_DIR}/polkit
	@sudo mkdir -p ${OPT_DIR}/docs
	@sudo install -m 755 $(TARGET) ${OPT_DIR}/bin/
	@sudo install -m 755 $(DAEMON_TARGET) ${OPT_DIR}/bin/
	@sudo install -m 755 $(CLIENT_TARGET) ${OPT_DIR}/bin/
	@sudo install -m 644 systemd/clevo-daemon.service ${OPT_DIR}/systemd/
	@sudo install -m 644 systemd/clevo-indicator.service ${OPT_DIR}/systemd/
	@sudo install -m 644 polkit/org.freedesktop.policykit.clevo-indicator.policy ${OPT_DIR}/polkit/
	@sudo install -m 644 README.md ${OPT_DIR}/docs/
	@sudo install -m 644 LICENSE ${OPT_DIR}/docs/
	@echo "Installed to ${OPT_DIR}/"
	@echo "To enable systemd service: sudo systemctl enable ${OPT_DIR}/systemd/clevo-daemon.service"

install-capabilities: $(TARGET) $(DAEMON_TARGET) $(MODULAR_TARGET)
	@echo Installing with capabilities...
	@sudo install -m 755 $(TARGET) ${DSTDIR}/bin/
	@sudo install -m 755 $(DAEMON_TARGET) ${DSTDIR}/bin/
	@sudo install -m 755 $(MODULAR_TARGET) ${DSTDIR}/bin/
	@sudo setcap cap_sys_rawio+ep ${DSTDIR}/bin/$(notdir $(TARGET))
	@sudo setcap cap_sys_rawio+ep ${DSTDIR}/bin/$(notdir $(DAEMON_TARGET))
	@sudo setcap cap_sys_rawio+ep ${DSTDIR}/bin/$(notdir $(MODULAR_TARGET))
	@echo "Installed with SYS_RAWIO capability"

install-systemd: $(TARGET) $(DAEMON_TARGET) $(MODULAR_TARGET)
	@echo Installing systemd services...
	@sudo install -m 755 $(TARGET) ${DSTDIR}/bin/
	@sudo install -m 755 $(DAEMON_TARGET) ${DSTDIR}/bin/
	@sudo install -m 755 $(MODULAR_TARGET) ${DSTDIR}/bin/
	@sudo install -m 644 systemd/clevo-indicator.service /etc/systemd/user/
	@sudo install -m 644 systemd/clevo-daemon.service /etc/systemd/system/
	@echo "Installed systemd services. Run: systemctl --user enable clevo-indicator.service"
	@echo "For daemon: sudo systemctl enable clevo-daemon.service"

install-polkit: $(TARGET) $(DAEMON_TARGET) $(MODULAR_TARGET)
	@echo Installing polkit policy...
	@sudo install -m 755 $(TARGET) ${DSTDIR}/bin/
	@sudo install -m 755 $(DAEMON_TARGET) ${DSTDIR}/bin/
	@sudo install -m 755 $(MODULAR_TARGET) ${DSTDIR}/bin/
	@sudo install -m 644 polkit/org.freedesktop.policykit.clevo-indicator.policy /usr/share/polkit-1/actions/
	@echo "Installed polkit policy"

test: $(TARGET) $(DAEMON_TARGET) $(MODULAR_TARGET)
	@echo "Running unit tests..."
	@chmod +x tests/run_tests.sh
	@./tests/run_tests.sh

test-permissions: $(TARGET) $(DAEMON_TARGET) $(MODULAR_TARGET)
	@sudo chown root $(TARGET) $(DAEMON_TARGET) $(MODULAR_TARGET)
	@sudo chgrp adm $(TARGET) $(DAEMON_TARGET) $(MODULAR_TARGET)
	@sudo chmod 4750 $(TARGET) $(DAEMON_TARGET) $(MODULAR_TARGET)

# Build rules for original targets
$(TARGET): $(OBJ) Makefile
	@mkdir -p bin
	@echo linking $(TARGET) from $(OBJ)
	@$(CC) $(OBJ) -o $(TARGET) $(LDFLAGS) $(UI_LDFLAGS) -lm

$(DAEMON_TARGET): $(DAEMON_OBJ) Makefile
	@mkdir -p bin
	@echo linking $(DAEMON_TARGET) from $(DAEMON_OBJ)
	@$(CC) $(DAEMON_OBJ) -o $(DAEMON_TARGET) $(LDFLAGS) -lm -lpthread -lncurses -ldbus-1

$(CLIENT_TARGET): $(CLIENT_OBJ) Makefile
	@mkdir -p bin
	@echo linking $(CLIENT_TARGET) from $(CLIENT_OBJ)
	@$(CC) $(CLIENT_OBJ) -o $(CLIENT_TARGET) $(LDFLAGS) -lm -lncurses

$(DBUS_CLIENT_TARGET): $(DBUS_CLIENT_OBJ) Makefile
	@mkdir -p bin
	@echo linking $(DBUS_CLIENT_TARGET) from $(DBUS_CLIENT_OBJ)
	@$(CC) $(DBUS_CLIENT_OBJ) -o $(DBUS_CLIENT_TARGET) $(LDFLAGS) -ldbus-1 -lncurses

# Build the new DBus client separately
bin/clevo-client-dbus: obj/clevo-client-dbus.o Makefile
	@mkdir -p bin
	@echo linking clevo-client-dbus from obj/clevo-client-dbus.o
	@$(CC) obj/clevo-client-dbus.o -o bin/clevo-client-dbus $(LDFLAGS) -ldbus-1 -lncurses

# Build the original DBus client separately
bin/clevo-dbus-client: obj/clevo-dbus-client.o Makefile
	@mkdir -p bin
	@echo linking clevo-dbus-client from obj/clevo-dbus-client.o
	@$(CC) obj/clevo-dbus-client.o -o bin/clevo-dbus-client $(LDFLAGS) -ldbus-1

$(DIAG_TARGET): $(DIAG_OBJ) Makefile
	@mkdir -p bin
	@echo linking $(DIAG_TARGET) from $(DIAG_OBJ)
	@$(CC) $(DIAG_OBJ) -o $(DIAG_TARGET) $(LDFLAGS) -lm

$(TEST_TARGET): $(TEST_OBJ) Makefile
	@mkdir -p bin
	@echo linking $(TEST_TARGET) from $(TEST_OBJ)
	@$(CC) $(TEST_OBJ) -o $(TEST_TARGET) $(LDFLAGS) -lm

# Build rule for modular target
$(MODULAR_TARGET): $(MODULAR_OBJ) Makefile
	@mkdir -p bin
	@echo linking $(MODULAR_TARGET) from $(MODULAR_OBJ)
	@$(CC) $(MODULAR_OBJ) -o $(MODULAR_TARGET) $(LDFLAGS) -lm -lpthread -lncurses

clean:
	rm -f $(OBJ) $(DAEMON_OBJ) $(CLIENT_OBJ) $(DBUS_CLIENT_OBJ) $(DIAG_OBJ) $(TEST_OBJ) $(MODULAR_OBJ) $(TARGET) $(DAEMON_TARGET) $(CLIENT_TARGET) $(DBUS_CLIENT_TARGET) $(DIAG_TARGET) $(TEST_TARGET) $(MODULAR_TARGET)

# Compilation rules for original files
$(OBJDIR)/%.o : $(SRCDIR)/%.c Makefile
	@echo compiling $< 
	@mkdir -p obj
	@$(CC) $(CFLAGS) $(UI_CFLAGS) -c $< -o $@

$(OBJDIR)/clevo-daemon.o : $(SRCDIR)/clevo-daemon.c Makefile
	@echo compiling $< 
	@mkdir -p obj
	@$(CC) $(CFLAGS) -c $< -o $@

# Compilation rules for modular files
$(OBJDIR)/main_new.o: $(SRCDIR)/main_new.c Makefile
	@echo compiling $< 
	@mkdir -p obj
	@$(CC) $(CFLAGS) -c $< -o $@

$(OBJDIR)/utils.o: $(SRCDIR)/utils.c Makefile
	@echo compiling $< 
	@mkdir -p obj
	@$(CC) $(CFLAGS) -c $< -o $@

$(OBJDIR)/logging.o: $(SRCDIR)/logging.c Makefile
	@echo compiling $< 
	@mkdir -p obj
	@$(CC) $(CFLAGS) -c $< -o $@

$(OBJDIR)/config.o: $(SRCDIR)/config.c Makefile
	@echo compiling $< 
	@mkdir -p obj
	@$(CC) $(CFLAGS) -c $< -o $@

$(OBJDIR)/ec_interface.o: $(SRCDIR)/ec_interface.c Makefile
	@echo compiling $< 
	@mkdir -p obj
	@$(CC) $(CFLAGS) -c $< -o $@

$(OBJDIR)/temperature_monitor.o: $(SRCDIR)/temperature_monitor.c Makefile
	@echo compiling $< 
	@mkdir -p obj
	@$(CC) $(CFLAGS) -c $< -o $@

$(OBJDIR)/pid_controller.o: $(SRCDIR)/pid_controller.c Makefile
	@echo compiling $< 
	@mkdir -p obj
	@$(CC) $(CFLAGS) -c $< -o $@

$(OBJDIR)/fan_health.o: $(SRCDIR)/fan_health.c Makefile
	@echo compiling $< 
	@mkdir -p obj
	@$(CC) $(CFLAGS) -c $< -o $@

$(OBJDIR)/daemon.o: $(SRCDIR)/daemon.c Makefile
	@echo compiling $< 
	@mkdir -p obj
	@$(CC) $(CFLAGS) -c $< -o $@

$(OBJDIR)/live_stats.o: $(SRCDIR)/live_stats.c Makefile
	@echo compiling $< 
	@mkdir -p obj
	@$(CC) $(CFLAGS) -c $< -o $@

# Additional targets for modular development
debug: CFLAGS += -DDEBUG -g3
debug: all

release: CFLAGS += -DNDEBUG -O3
release: clean all

# Show module dependencies
deps:
	@echo "Module Dependencies:"
	@echo "main_new.c -> all modules"
	@echo "utils.c -> (no dependencies)"
	@echo "logging.c -> (no dependencies)"
	@echo "config.c -> logging.h"
	@echo "ec_interface.c -> logging.h"
	@echo "temperature_monitor.c -> ec_interface.h, logging.h"
	@echo "pid_controller.c -> logging.h"
	@echo "fan_health.c -> ec_interface.h, logging.h"
	@echo "daemon.c -> logging.h, utils.h"
	@echo "live_stats.c -> logging.h, utils.h"

# Show module sizes
sizes: $(MODULAR_OBJ)
	@echo "Module sizes:"
	@for obj in $(MODULAR_OBJ); do \
		size=$$(stat -c%s $$obj 2>/dev/null || stat -f%z $$obj 2>/dev/null); \
		echo "  $$(basename $$obj .o): $$size bytes"; \
	done

.PHONY: all original modular install install-opt install-capabilities install-systemd install-polkit test test-permissions clean debug release deps sizes

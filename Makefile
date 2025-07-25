vpath %.c ../src

CC = gcc
CFLAGS = -c -Wall -std=gnu99 -DHAVE_LIBCAP
LDFLAGS = -lcap

DSTDIR := /usr/local
OPT_DIR := /opt/galago-pro-fan-control-daemon
OBJDIR := obj
SRCDIR := src

SRC = clevo-indicator.c privilege_manager.c
DAEMON_SRC = clevo-daemon.c clevo-daemon-socket.c privilege_manager.c
CLIENT_SRC = clevo-client.c
DIAG_SRC = ec_diagnostic.c
TEST_SRC = test_settings.c
OBJ = $(patsubst %.c,$(OBJDIR)/%.o,$(SRC))
DAEMON_OBJ = $(patsubst %.c,$(OBJDIR)/%.o,$(DAEMON_SRC))
CLIENT_OBJ = $(patsubst %.c,$(OBJDIR)/%.o,$(CLIENT_SRC))
DIAG_OBJ = $(patsubst %.c,$(OBJDIR)/%.o,$(DIAG_SRC))
TEST_OBJ = $(patsubst %.c,$(OBJDIR)/%.o,$(TEST_SRC))

TARGET = bin/clevo-indicator
DAEMON_TARGET = bin/clevo-daemon
CLIENT_TARGET = bin/clevo-client
DIAG_TARGET = bin/ec_diagnostic
TEST_TARGET = bin/test_settings

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

all: $(TARGET) $(DAEMON_TARGET) $(CLIENT_TARGET)

install: $(TARGET) $(DAEMON_TARGET) $(CLIENT_TARGET)
	@echo Install to ${DSTDIR}/bin/
	@sudo install -m 4750 -g adm $(TARGET) ${DSTDIR}/bin/
	@sudo install -m 4750 -g adm $(DAEMON_TARGET) ${DSTDIR}/bin/
	@sudo install -m 755 $(CLIENT_TARGET) ${DSTDIR}/bin/

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

install-capabilities: $(TARGET) $(DAEMON_TARGET)
	@echo Installing with capabilities...
	@sudo install -m 755 $(TARGET) ${DSTDIR}/bin/
	@sudo install -m 755 $(DAEMON_TARGET) ${DSTDIR}/bin/
	@sudo setcap cap_sys_rawio+ep ${DSTDIR}/bin/$(notdir $(TARGET))
	@sudo setcap cap_sys_rawio+ep ${DSTDIR}/bin/$(notdir $(DAEMON_TARGET))
	@echo "Installed with SYS_RAWIO capability"

install-systemd: $(TARGET) $(DAEMON_TARGET)
	@echo Installing systemd services...
	@sudo install -m 755 $(TARGET) ${DSTDIR}/bin/
	@sudo install -m 755 $(DAEMON_TARGET) ${DSTDIR}/bin/
	@sudo install -m 644 systemd/clevo-indicator.service /etc/systemd/user/
	@sudo install -m 644 systemd/clevo-daemon.service /etc/systemd/system/
	@echo "Installed systemd services. Run: systemctl --user enable clevo-indicator.service"
	@echo "For daemon: sudo systemctl enable clevo-daemon.service"

install-polkit: $(TARGET) $(DAEMON_TARGET)
	@echo Installing polkit policy...
	@sudo install -m 755 $(TARGET) ${DSTDIR}/bin/
	@sudo install -m 755 $(DAEMON_TARGET) ${DSTDIR}/bin/
	@sudo install -m 644 polkit/org.freedesktop.policykit.clevo-indicator.policy /usr/share/polkit-1/actions/
	@echo "Installed polkit policy"

test: $(TARGET) $(DAEMON_TARGET)
	@echo "Running unit tests..."
	@chmod +x tests/run_tests.sh
	@./tests/run_tests.sh

test-permissions: $(TARGET) $(DAEMON_TARGET)
	@sudo chown root $(TARGET) $(DAEMON_TARGET)
	@sudo chgrp adm $(TARGET) $(DAEMON_TARGET)
	@sudo chmod 4750 $(TARGET) $(DAEMON_TARGET)

$(TARGET): $(OBJ) Makefile
	@mkdir -p bin
	@echo linking $(TARGET) from $(OBJ)
	@$(CC) $(OBJ) -o $(TARGET) $(LDFLAGS) $(UI_LDFLAGS) -lm

$(DAEMON_TARGET): $(DAEMON_OBJ) Makefile
	@mkdir -p bin
	@echo linking $(DAEMON_TARGET) from $(DAEMON_OBJ)
	@$(CC) $(DAEMON_OBJ) -o $(DAEMON_TARGET) $(LDFLAGS) -lm -lpthread -lncurses

$(CLIENT_TARGET): $(CLIENT_OBJ) Makefile
	@mkdir -p bin
	@echo linking $(CLIENT_TARGET) from $(CLIENT_OBJ)
	@$(CC) $(CLIENT_OBJ) -o $(CLIENT_TARGET) $(LDFLAGS) -lm -lncurses

$(DIAG_TARGET): $(DIAG_OBJ) Makefile
	@mkdir -p bin
	@echo linking $(DIAG_TARGET) from $(DIAG_OBJ)
	@$(CC) $(DIAG_OBJ) -o $(DIAG_TARGET) $(LDFLAGS) -lm

$(TEST_TARGET): $(TEST_OBJ) Makefile
	@mkdir -p bin
	@echo linking $(TEST_TARGET) from $(TEST_OBJ)
	@$(CC) $(TEST_OBJ) -o $(TEST_TARGET) $(LDFLAGS) -lm

clean:
	rm -f $(OBJ) $(DAEMON_OBJ) $(CLIENT_OBJ) $(DIAG_OBJ) $(TEST_OBJ) $(TARGET) $(DAEMON_TARGET) $(CLIENT_TARGET) $(DIAG_TARGET) $(TEST_TARGET)

$(OBJDIR)/%.o : $(SRCDIR)/%.c Makefile
	@echo compiling $< 
	@mkdir -p obj
	@$(CC) $(CFLAGS) $(UI_CFLAGS) -c $< -o $@

$(OBJDIR)/clevo-daemon.o : $(SRCDIR)/clevo-daemon.c Makefile
	@echo compiling $< 
	@mkdir -p obj
	@$(CC) $(CFLAGS) -c $< -o $@

#$(OBJECTS): | obj

#obj:
#	@mkdir -p $@

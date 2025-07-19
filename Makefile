vpath %.c ../src

CC = gcc
CFLAGS = -c -Wall -std=gnu99 -DHAVE_LIBCAP
LDFLAGS = -lcap

DSTDIR := /usr/local
OBJDIR := obj
SRCDIR := src

SRC = clevo-indicator.c privilege_manager.c
DAEMON_SRC = clevo-daemon.c privilege_manager.c
OBJ = $(patsubst %.c,$(OBJDIR)/%.o,$(SRC))
DAEMON_OBJ = $(patsubst %.c,$(OBJDIR)/%.o,$(DAEMON_SRC))

TARGET = bin/clevo-indicator
DAEMON_TARGET = bin/clevo-daemon

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

all: $(TARGET) $(DAEMON_TARGET)

install: $(TARGET) $(DAEMON_TARGET)
	@echo Install to ${DSTDIR}/bin/
	@sudo install -m 4750 -g adm $(TARGET) ${DSTDIR}/bin/
	@sudo install -m 4750 -g adm $(DAEMON_TARGET) ${DSTDIR}/bin/

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
	@$(CC) $(DAEMON_OBJ) -o $(DAEMON_TARGET) $(LDFLAGS) -lm

clean:
	rm -f $(OBJ) $(DAEMON_OBJ) $(TARGET) $(DAEMON_TARGET)

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

vpath %.c ../src

CC = gcc
CFLAGS = -c -Wall -std=gnu99 -DHAVE_LIBCAP
LDFLAGS = -lcap

DSTDIR := /usr/local
OBJDIR := obj
SRCDIR := src

SRC = clevo-indicator.c privilege_manager.c
OBJ = $(patsubst %.c,$(OBJDIR)/%.o,$(SRC)) 

TARGET = bin/clevo-indicator

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

CFLAGS += `pkg-config --cflags ayatana-appindicator3-0.1`
LDFLAGS += `pkg-config --libs ayatana-appindicator3-0.1`

all: $(TARGET)

install: $(TARGET)
	@echo Install to ${DSTDIR}/bin/
	@sudo install -m 4750 -g adm $(TARGET) ${DSTDIR}/bin/

install-capabilities: $(TARGET)
	@echo Installing with capabilities...
	@sudo install -m 755 $(TARGET) ${DSTDIR}/bin/
	@sudo setcap cap_sys_rawio+ep ${DSTDIR}/bin/$(notdir $(TARGET))
	@echo "Installed with SYS_RAWIO capability"

install-systemd: $(TARGET)
	@echo Installing systemd service...
	@sudo install -m 755 $(TARGET) ${DSTDIR}/bin/
	@sudo install -m 644 systemd/clevo-indicator.service /etc/systemd/user/
	@echo "Installed systemd service. Run: systemctl --user enable clevo-indicator.service"

install-polkit: $(TARGET)
	@echo Installing polkit policy...
	@sudo install -m 755 $(TARGET) ${DSTDIR}/bin/
	@sudo install -m 644 polkit/org.freedesktop.policykit.clevo-indicator.policy /usr/share/polkit-1/actions/
	@echo "Installed polkit policy"

test: $(TARGET)
	@echo "Running unit tests..."
	@chmod +x tests/run_tests.sh
	@./tests/run_tests.sh

test-permissions: $(TARGET)
	@sudo chown root $(TARGET)
	@sudo chgrp adm  $(TARGET)
	@sudo chmod 4750 $(TARGET)

$(TARGET): $(OBJ) Makefile
	@mkdir -p bin
	@echo linking $(TARGET) from $(OBJ)
	@$(CC) $(OBJ) -o $(TARGET) $(LDFLAGS) -lm

clean:
	rm -f $(OBJ) $(TARGET)

$(OBJDIR)/%.o : $(SRCDIR)/%.c Makefile
	@echo compiling $< 
	@mkdir -p obj
	@$(CC) $(CFLAGS) -c $< -o $@

#$(OBJECTS): | obj

#obj:
#	@mkdir -p $@

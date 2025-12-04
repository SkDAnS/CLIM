CC		= gcc
CFLAGS	= -Wall -Wextra -std=c11 -O2 -Iinclude

SRCDIR	= src
INCDIR	= include
OBJDIR	= obj
BINDIR	= bin

SOURCES	= $(SRCDIR)/ServeurISY.c $(SRCDIR)/GroupeISY.c \
          $(SRCDIR)/ClientISY.c $(SRCDIR)/AffichageISY.c
OBJECTS	= $(SOURCES:$(SRCDIR)/%.c=$(OBJDIR)/%.o)

TARGETS	= $(BINDIR)/ServeurISY $(BINDIR)/GroupeISY \
          $(BINDIR)/ClientISY $(BINDIR)/AffichageISY

all: $(BINDIR) $(OBJDIR) $(TARGETS)

$(BINDIR):
	mkdir -p $(BINDIR)

$(OBJDIR):
	mkdir -p $(OBJDIR)

# Compilation des .o
$(OBJDIR)/%.o: $(SRCDIR)/%.c $(INCDIR)/Commun.h
	$(CC) $(CFLAGS) -c $< -o $@

# Liens vers bin/
$(BINDIR)/ServeurISY: $(OBJDIR)/ServeurISY.o
	$(CC) $^ -o $@

$(BINDIR)/GroupeISY: $(OBJDIR)/GroupeISY.o
	$(CC) $^ -o $@

$(BINDIR)/ClientISY: $(OBJDIR)/ClientISY.o
	$(CC) $^ -o $@

$(BINDIR)/AffichageISY: $(OBJDIR)/AffichageISY.o
	$(CC) $^ -o $@

clean:
	rm -rf $(OBJDIR) $(BINDIR)

.PHONY: all clean

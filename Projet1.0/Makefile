CC      = gcc
CFLAGS  = -Wall -Wextra -std=c11 -O2 -Iinclude
LDFLAGS = 

SRCDIR  = src
INCDIR  = include
OBJDIR  = obj

SOURCES = $(SRCDIR)/ServeurISY.c $(SRCDIR)/GroupeISY.c \
          $(SRCDIR)/ClientISY.c $(SRCDIR)/AffichageISY.c
OBJECTS = $(SOURCES:$(SRCDIR)/%.c=$(OBJDIR)/%.o)

TARGETS = ServeurISY GroupeISY ClientISY AffichageISY

all: $(TARGETS)

$(OBJDIR)/%.o: $(SRCDIR)/%.c $(INCDIR)/Commun.h
	@mkdir -p $(OBJDIR)
	$(CC) $(CFLAGS) -c $< -o $@

ServeurISY: $(OBJDIR)/ServeurISY.o
	$(CC) $^ -o $@ $(LDFLAGS)

GroupeISY: $(OBJDIR)/GroupeISY.o
	$(CC) $^ -o $@ $(LDFLAGS)

ClientISY: $(OBJDIR)/ClientISY.o
	$(CC) $^ -o $@ $(LDFLAGS)

AffichageISY: $(OBJDIR)/AffichageISY.o
	$(CC) $^ -o $@ $(LDFLAGS)

clean:
	rm -rf $(OBJDIR) $(TARGETS)

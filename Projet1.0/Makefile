CC      = gcc
CFLAGS  = -Wall -Wextra -std=c11 -O2
LDFLAGS = 

SRCDIR  = src
BINDIR  = .
OBJDIR  = obj

SOURCES = $(SRCDIR)/ServeurISY.c $(SRCDIR)/GroupeISY.c \
          $(SRCDIR)/ClientISY.c $(SRCDIR)/AffichageISY.c
OBJECTS = $(SOURCES:$(SRCDIR)/%.c=$(OBJDIR)/%.o)

TARGETS = ServeurISY GroupeISY ClientISY AffichageISY

all: $(TARGETS)

$(OBJDIR)/%.o: $(SRCDIR)/%.c $(SRCDIR)/Commun.h
	@mkdir -p $(OBJDIR)
	$(CC) $(CFLAGS) -c $< -o $@

ServeurISY: $(OBJDIR)/ServeurISY.o $(OBJDIR)/Commun.o
	$(CC) $^ -o $@ $(LDFLAGS)

GroupeISY: $(OBJDIR)/GroupeISY.o $(OBJDIR)/Commun.o
	$(CC) $^ -o $@ $(LDFLAGS)

ClientISY: $(OBJDIR)/ClientISY.o $(OBJDIR)/Commun.o
	$(CC) $^ -o $@ $(LDFLAGS)

AffichageISY: $(OBJDIR)/AffichageISY.o $(OBJDIR)/Commun.o
	$(CC) $^ -o $@ $(LDFLAGS)

# Petit "truc" : compiler Commun.h comme s’il y avait Commun.c
$(OBJDIR)/Commun.o: $(SRCDIR)/Commun.h
	@mkdir -p $(OBJDIR)
	$(CC) $(CFLAGS) -c -x c $(SRCDIR)/Commun.h -o $@

clean:
	rm -rf $(OBJDIR) $(TARGETS)

.PHONY: all clean

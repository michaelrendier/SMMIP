# PtolC/Makefile — C Monad build

CC      = gcc
CFLAGS  = -O2 -Wall -Wextra -std=c11 -D_POSIX_C_SOURCE=200809L
LDFLAGS = -lm

# ── Optional: libxml2 (HTML/XML extraction) ───────────────────────────────────
HAVE_XML := $(shell pkg-config --exists libxml-2.0 2>/dev/null && echo yes)
ifeq ($(HAVE_XML),yes)
    CFLAGS  += -DHAVE_LIBXML2 $(shell pkg-config --cflags libxml-2.0)
    LDFLAGS += $(shell pkg-config --libs libxml-2.0)
endif

TARGET  = ptolemy
SRCS    = main.c monad.c checkpoint.c tokenizer.c filter.c ingest.c log.c daemon.c
OBJS    = $(SRCS:.c=.o)

PREFIX     ?= /usr/local
MANDIR     ?= $(PREFIX)/share/man

.PHONY: all clean export corpus full expand ingest install install-man install-systemd

all: $(TARGET)

# ── Corpus: download WordNet + ingest → monad_wordnet.bin ───────────────────
# Requires Python 3 + NLTK.  Runs in ~6 seconds.
# Usage: make corpus
corpus: $(TARGET)
	@echo "[corpus] checking NLTK wordnet…"
	@python3 -m nltk.downloader -q wordnet 2>/dev/null || \
	    (pip install --quiet nltk && python3 -m nltk.downloader -q wordnet)
	@echo "[corpus] extracting lemmas + definitions (313K lines)…"
	@python3 tools/dump_wordnet.py | ./$(TARGET) -i -l -
	@echo "[corpus] done → monad_wordnet.bin"

# ── Full: binary + corpus in one step ───────────────────────────────────────
full: $(TARGET) corpus

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

# Export Python checkpoint to binary (run from Ptolemy3/ root)
export:
	python3 ../Callimachus/checkpoint_export.py

# Expand checkpoint zero field — usage: make expand N=255400
expand: tools/checkpoint_expand
	./tools/checkpoint_expand monad_wordnet.bin $(N)

tools/checkpoint_expand: tools/checkpoint_expand.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

# Full system ingest — usage: make ingest  (or: make ingest FIRST=/SystemTree.txt)
FIRST ?= /SystemTree.txt
ingest: $(TARGET)
	python3 tools/ingest_system.py --root / --first $(FIRST)

# ── Install ──────────────────────────────────────────────────────────────────
install: $(TARGET) install-man
	install -Dm755 $(TARGET) $(DESTDIR)$(PREFIX)/bin/ptolemy
	install -Dm755 tools/checkpoint_expand $(DESTDIR)$(PREFIX)/bin/checkpoint_expand

install-systemd:
	install -Dm644 systemd/ptolemy.socket \
	    $(DESTDIR)$(PREFIX)/lib/systemd/user/ptolemy.socket
	install -Dm644 systemd/ptolemy.service \
	    $(DESTDIR)$(PREFIX)/lib/systemd/user/ptolemy.service
	@echo "Enable with: systemctl --user enable --now ptolemy.socket"

install-man:
	install -Dm644 man/ptolemy.1 $(DESTDIR)$(MANDIR)/man1/ptolemy.1
	install -Dm644 man/ptolemyignore.5 $(DESTDIR)$(MANDIR)/man5/ptolemyignore.5
	gzip -f $(DESTDIR)$(MANDIR)/man1/ptolemy.1
	gzip -f $(DESTDIR)$(MANDIR)/man5/ptolemyignore.5

# ── Remove build artifacts ────────────────────────────────────────────────────
clean:
	rm -f $(OBJS) $(TARGET) tools/checkpoint_expand
	rm -f ~/.ptolemy/logs/ptolemy_test_*.log 2>/dev/null || true

# ── Dependencies ────────────────────────────────────────────────────────────
main.o:        main.c ptolemy.h monad.h checkpoint.h tokenizer.h ingest.h daemon.h log.h
monad.o:       monad.c ptolemy.h monad.h filter.h tokenizer.h
checkpoint.o:  checkpoint.c ptolemy.h checkpoint.h monad.h
tokenizer.o:   tokenizer.c tokenizer.h
filter.o:      filter.c filter.h
ingest.o:      ingest.c monad.h filter.h checkpoint.h ingest.h log.h
log.o:         log.c log.h
daemon.o:      daemon.c ptolemy.h monad.h checkpoint.h daemon.h log.h

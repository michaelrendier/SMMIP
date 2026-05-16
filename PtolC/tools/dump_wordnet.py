#!/usr/bin/env python3
"""
PtolC/tools/dump_wordnet.py — stream WordNet to stdout for ptolemy -l -.

Streams: all lemma names (147K) + synset definitions + examples (166K lines).
Total: ~313K lines, ~11MB uncompressed. Piped directly to ptolemy's stdin.

Usage:
    python3 tools/dump_wordnet.py | ./ptolemy -i -l -
    make corpus   (runs the above automatically)
"""
import sys
from nltk.corpus import wordnet as wn

# Pass 1: lemma names — one per line, underscores → spaces
for lemma in wn.all_lemma_names():
    sys.stdout.write(lemma.replace('_', ' ') + '\n')

# Pass 2: synset definitions + examples
for ss in wn.all_synsets():
    definition = ss.definition()
    if definition:
        sys.stdout.write(definition + '\n')
    for example in ss.examples():
        sys.stdout.write(example + '\n')

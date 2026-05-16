#!/usr/bin/env python3
"""
PtolC/tools/ingest_system.py — full filesystem ingest for ptolemy (Python3)

Recursively visits every readable file from ROOT, extracts text,
feeds it to ptolemy -l - in batches.  State saved to JSON every
BATCH_DIRS directories — safe to kill and resume at any time.
Run as root — no sudo required anywhere in this script.

DEPTH LAYERS  (--depth N, default 1)
─────────────────────────────────────────────────────────────────
  0   Plain text only   (text/*)
  1   + PDF             (pdfminer / pdftotext / OCR fallback)     ← default
  2   + Archives + HTML (gzip/bzip2/xz decompressed, html/gettext .po)
  3   + All document formats  (EPUB, DOCX, ODT, RTF, Excel, images-OCR)
  4   + Binary image mounting (walk-the-hallway: losetup → blkid → mount)

Higher depth = more extraction, longer runtime.
Start at depth 1 overnight.  Bump to depth 3 when deps are installed.
Enable depth 4 only when binary images are supplied via --images.

SETUP (run once):
    cd PtolC
    make
    bash tools/install_ingest_deps.sh
    ./tools/checkpoint_expand monad_wordnet.bin 255400
    ./ptolemy -s

RUN:
    python3 tools/ingest_system.py --root / --first /SystemTree.txt
    python3 tools/ingest_system.py --root / --first /SystemTree.txt --depth 3
    # overnight:
    nohup python3 tools/ingest_system.py --root / --first /SystemTree.txt \\
        --depth 3  2>&1 | tee ingest_py.log &

RESUME:
    python3 tools/ingest_system.py --root / --first /SystemTree.txt --depth 3
    # Already-completed directories are skipped automatically.

BINARY IMAGE MOUNTING (depth 4):
    python3 tools/ingest_system.py --depth 4 \\
        --images /path/to/image.bin --mount-base /mnt/hallway
    Provide --fstab /path/to/fstab from the image when available —
    it drives partition type selection for Android images.
    ADB shell (rooted phone, no mount needed):
        adb shell find / -readable -type f 2>/dev/null \\
            | while read f; do adb shell cat "$f" 2>/dev/null; done \\
            | ./ptolemy -l -

GOOGLE DRIVE (GNOME GVFS):
    Detected automatically at /run/user/<uid>/gvfs/
    Mount in Nautilus first — appears as google-drive:host=… subdirectory.

EXTERNAL DRIVES:
    /media/ scanned automatically (all users, all volumes).
"""

import argparse
import json
import logging
import math
import os
import shutil
import subprocess
import sys
import time

# ── Optional library imports ───────────────────────────────────────────────────
try:
    import magic as libmagic
    _HAVE_MAGIC = True
except ImportError:
    _HAVE_MAGIC = False

try:
    import chardet
    _HAVE_CHARDET = True
except ImportError:
    _HAVE_CHARDET = False

try:
    from pdfminer.high_level import extract_text as pdf_extract
    _HAVE_PDFMINER = True
except ImportError:
    _HAVE_PDFMINER = False

try:
    import docx as python_docx
    _HAVE_DOCX = True
except ImportError:
    _HAVE_DOCX = False

try:
    import ebooklib
    from ebooklib import epub as epub_lib
    import re as _re
    _HAVE_EPUB = True
except ImportError:
    _HAVE_EPUB = False

try:
    from odf import teletype
    from odf.opendocument import load as odf_load
    _HAVE_ODF = True
except ImportError:
    _HAVE_ODF = False

try:
    from striprtf.striprtf import rtf_to_text
    _HAVE_RTF = True
except ImportError:
    _HAVE_RTF = False

try:
    from bs4 import BeautifulSoup
    _HAVE_BS4 = True
except ImportError:
    _HAVE_BS4 = False

try:
    import pytesseract
    from PIL import Image
    _HAVE_OCR = True
except ImportError:
    _HAVE_OCR = False

# ── Constants ──────────────────────────────────────────────────────────────────
CHUNK_LINES   = 50_000
DEFAULT_BATCH = 10
STATE_FILE    = os.path.join(os.path.expanduser("~"), ".ptolemy", "ingest_state.json")
PTOLEMY       = shutil.which("ptolemy") or "/usr/bin/ptolemy"

# Global depth — set once from --depth arg, read anywhere in the module.
# Determines which extraction layers are active (see docstring).
DEPTH: int = 1

PRUNE_DIRS = frozenset([
    "/proc", "/sys", "/dev", "/run/lock",
    "/snap", "/var/run", "/tmp",
    # AppArmor kernel interface — death trap.
    # Reads block indefinitely under MAC mediation. Belt and suspenders
    # since /sys already covers the runtime mount.
    "/sys/kernel/security",
    "/run/apparmor",
])

# Partition filesystem types never mounted in walk-the-hallway.
# efivars / EFI System Partition (vfat) are WANTED — not listed here.
_SKIP_FSTYPES = frozenset([
    "swap",        # raw swap — no text
    "BitLocker",   # encrypted
    "crypto_LUKS", # encrypted
    "securityfs",  # AppArmor kernel security interface — death trap.
                   # Never mount. Never traverse.
])

# Partition labels that indicate AppArmor or other traps.
# EFI/efivars labels intentionally absent — that data is wanted.
_SKIP_LABELS = frozenset(["AppArmor", "apparmor", "security"])

LOG = logging.getLogger("ingest")

# ── Mime detection ─────────────────────────────────────────────────────────────

def mime_type(path: str) -> str:
    if _HAVE_MAGIC:
        try:
            return libmagic.from_file(path, mime=True) or ""
        except Exception:
            pass
    result = subprocess.run(
        ["file", "-b", "--mime-type", path],
        capture_output=True, text=True, timeout=5
    )
    return result.stdout.strip() if result.returncode == 0 else ""

# ── Text extraction (depth-gated) ─────────────────────────────────────────────

def decode_bytes(raw: bytes) -> str:
    if _HAVE_CHARDET:
        det = chardet.detect(raw[:8192])
        enc = det.get("encoding") or "utf-8"
    else:
        enc = "utf-8"
    return raw.decode(enc, errors="replace")


def extract_text(path: str, mime: str) -> str:
    """
    Extract readable text from path.  Which formats are attempted is
    controlled by the global DEPTH:

      0  plain text only
      1  + PDF
      2  + compressed archives, HTML, gettext .po
      3  + EPUB, DOCX, ODT, RTF, Excel, image OCR
      4  (binary images handled separately in walk_hallway_images)
    """
    try:
        # ── DEPTH 0: plain text ───────────────────────────────────────────────
        if mime.startswith("text/"):
            with open(path, "rb") as f:
                return decode_bytes(f.read())

        if DEPTH < 1:
            return ""

        # ── DEPTH 1: PDF ──────────────────────────────────────────────────────
        if mime == "application/pdf":
            if _HAVE_PDFMINER:
                try:
                    return pdf_extract(path) or ""
                except Exception:
                    pass
            result = subprocess.run(
                ["pdftotext", "-q", "-nopgbrk", "-enc", "UTF-8", path, "-"],
                capture_output=True, timeout=60
            )
            if result.returncode == 0:
                return result.stdout.decode("utf-8", errors="replace")
            if _HAVE_OCR:
                try:
                    import pdf2image
                    imgs = pdf2image.convert_from_path(path, dpi=150)
                    return "\n".join(pytesseract.image_to_string(img) for img in imgs)
                except Exception:
                    pass
            return ""

        if DEPTH < 2:
            return ""

        # ── DEPTH 2: compressed archives, HTML, gettext ───────────────────────
        if mime in ("application/gzip", "application/x-gzip"):
            result = subprocess.run(["zcat", path], capture_output=True, timeout=30)
            if result.returncode != 0:
                return ""
            inner = subprocess.run(
                ["file", "-b", "--mime-type", "-"],
                input=result.stdout[:512], capture_output=True, timeout=5
            ).stdout.strip().decode()
            if inner.startswith("text/"):
                col = subprocess.run(
                    ["col", "-b"], input=result.stdout, capture_output=True, timeout=30
                )
                return col.stdout.decode("utf-8", errors="replace")
            return ""

        if mime == "application/x-bzip2":
            result = subprocess.run(["bzcat", path], capture_output=True, timeout=30)
            return result.stdout.decode("utf-8", errors="replace") if result.returncode == 0 else ""

        if mime == "application/x-xz":
            result = subprocess.run(["xzcat", path], capture_output=True, timeout=30)
            return result.stdout.decode("utf-8", errors="replace") if result.returncode == 0 else ""

        if mime in ("text/html", "application/xhtml+xml"):
            with open(path, "rb") as f:
                raw = f.read()
            if _HAVE_BS4:
                return BeautifulSoup(raw, "lxml").get_text(separator="\n")
            result = subprocess.run(["html2text", path], capture_output=True, timeout=30)
            return result.stdout.decode("utf-8", errors="replace") if result.returncode == 0 else ""

        # gettext .po/.pot — all Unicode locale strings (every language on the system)
        if path.endswith((".po", ".pot")):
            with open(path, "rb") as f:
                raw = f.read()
            text = decode_bytes(raw)
            lines = [ln[7:].strip().strip('"') for ln in text.splitlines()
                     if ln.startswith("msgstr") or ln.startswith("msgid")]
            return "\n".join(ln for ln in lines if ln)

        if DEPTH < 3:
            return ""

        # ── DEPTH 3: all document formats, image OCR ──────────────────────────
        if mime == "application/msword":
            result = subprocess.run(["catdoc", path], capture_output=True, timeout=30)
            return result.stdout.decode("utf-8", errors="replace") if result.returncode == 0 else ""

        if mime in ("application/rtf", "text/rtf"):
            if _HAVE_RTF:
                with open(path, "rb") as f:
                    return rtf_to_text(f.read().decode("utf-8", errors="replace"))
            result = subprocess.run(["unrtf", "--text", path], capture_output=True, timeout=30)
            return result.stdout.decode("utf-8", errors="replace") if result.returncode == 0 else ""

        if mime == "application/epub+zip":
            if _HAVE_EPUB:
                try:
                    book = epub_lib.read_epub(path, options={"ignore_ncx": True})
                    parts = []
                    for item in book.get_items_of_type(ebooklib.ITEM_DOCUMENT):
                        if _HAVE_BS4:
                            parts.append(
                                BeautifulSoup(item.get_content(), "lxml").get_text(separator="\n")
                            )
                        else:
                            parts.append(_re.sub(r"<[^>]+>", "",
                                item.get_content().decode("utf-8", errors="replace")))
                    return "\n".join(parts)
                except Exception:
                    pass
            import zipfile, re
            try:
                with zipfile.ZipFile(path) as z:
                    parts = []
                    for n in z.namelist():
                        if n.endswith((".xhtml", ".html", ".htm")):
                            parts.append(re.sub(r"<[^>]+>", "",
                                z.read(n).decode("utf-8", errors="replace")))
                    return "\n".join(parts)
            except Exception:
                return ""

        if mime == "application/vnd.oasis.opendocument.text":
            if _HAVE_ODF:
                try:
                    return teletype.extractText(odf_load(path).text)
                except Exception:
                    pass
            result = subprocess.run(["odt2txt", "--stdout", path], capture_output=True, timeout=30)
            return result.stdout.decode("utf-8", errors="replace") if result.returncode == 0 else ""

        if mime == "application/vnd.openxmlformats-officedocument.wordprocessingml.document":
            if _HAVE_DOCX:
                try:
                    return "\n".join(p.text for p in python_docx.Document(path).paragraphs)
                except Exception:
                    pass
            return ""

        if mime in ("application/vnd.ms-excel",
                    "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet"):
            try:
                import openpyxl
                wb = openpyxl.load_workbook(path, read_only=True, data_only=True)
                rows = []
                for ws in wb.worksheets:
                    for row in ws.iter_rows(values_only=True):
                        rows.append("\t".join(str(c) for c in row if c is not None))
                return "\n".join(rows)
            except Exception:
                return ""

        if mime.startswith("image/") and _HAVE_OCR:
            try:
                return pytesseract.image_to_string(Image.open(path))
            except Exception:
                pass

    except (PermissionError, FileNotFoundError, IsADirectoryError, OSError):
        pass
    except subprocess.TimeoutExpired:
        LOG.warning("timeout: %s", path)
    except Exception as e:
        LOG.debug("extract error %s: %s", path, e)

    return ""

# ── Ptolemy pipe ───────────────────────────────────────────────────────────────

def learn_text(text: str, ptolemy: str) -> bool:
    if not text.strip():
        return True
    try:
        result = subprocess.run(
            [ptolemy, "-l", "-"],
            input=text.encode("utf-8", errors="replace"),
            capture_output=True, timeout=300
        )
        return result.returncode == 0
    except subprocess.TimeoutExpired:
        LOG.warning("ptolemy timeout during learn")
        return False
    except Exception as e:
        LOG.error("ptolemy error: %s", e)
        return False


def learn_chunked(text: str, ptolemy: str, chunk_lines: int = CHUNK_LINES) -> int:
    lines = text.splitlines(keepends=True)
    if not lines:
        return 0
    n_chunks = math.ceil(len(lines) / chunk_lines)
    for i in range(n_chunks):
        chunk = "".join(lines[i * chunk_lines:(i + 1) * chunk_lines])
        if not learn_text(chunk, ptolemy):
            LOG.warning("learn failed on chunk %d/%d", i + 1, n_chunks)
    return n_chunks

# ── State management ───────────────────────────────────────────────────────────

def load_state(state_path: str) -> dict:
    if os.path.exists(state_path):
        try:
            with open(state_path) as f:
                return json.load(f)
        except Exception:
            pass
    return {"done_dirs": [], "files_total": 0, "dirs_total": 0,
            "errors": 0, "started": time.time()}


def save_state(state: dict, state_path: str):
    tmp = state_path + ".tmp"
    with open(tmp, "w") as f:
        json.dump(state, f, indent=2)
    os.replace(tmp, state_path)

# ── Directory discovery ────────────────────────────────────────────────────────

def _check_gvfs_readable(path: str) -> bool:
    """
    Test whether a GVFS FUSE mount is accessible by the current process.

    FUSE restricts access to the mounting user by default.  Running as root
    does NOT bypass this — root still gets EPERM unless 'user_allow_other'
    is in /etc/fuse.conf AND the mount was created with -o allow_other.

    If this returns False, fix with:
        echo user_allow_other >> /etc/fuse.conf
    Then remount the drive in the file manager and re-run.
    """
    try:
        next(iter(os.scandir(path)), None)
        return True
    except PermissionError:
        return False
    except Exception:
        return True   # other errors are not FUSE permission blocks


def auto_extra_roots() -> list[str]:
    """
    Detect GVFS mounts (all user sessions) and external drives.

    Running as root means os.getuid()==0, so /run/user/0/gvfs/ is empty —
    the real GVFS mounts live under the logged-in user's uid (e.g. 1000).
    Scan ALL /run/user/<uid>/gvfs/ directories regardless of current uid.

    Google Drive appears as:
        /run/user/<uid>/gvfs/google-drive:host=gmail.com,user=<name>
    The file manager URI google-drive://user@gmail.com/ maps to exactly
    that path.  os.walk() traverses it like any other directory tree.

    FUSE permission note: if a GVFS path is detected but unreadable as root,
    a clear warning is printed with the fix.  The mount is still added to
    the list so --extra can be used manually after fixing fuse.conf.
    """
    extras = []

    # ── GVFS: scan every active user session ─────────────────────────────────
    run_user = "/run/user"
    if os.path.isdir(run_user):
        for uid_entry in os.scandir(run_user):
            if not uid_entry.is_dir():
                continue
            gvfs = os.path.join(uid_entry.path, "gvfs")
            if not os.path.isdir(gvfs):
                continue
            for entry in os.scandir(gvfs):
                if not entry.is_dir():
                    continue
                name = entry.name
                # Identify and label Google Drive mounts clearly
                if name.startswith("google-drive:"):
                    label = "Google Drive"
                elif name.startswith("smb-share:"):
                    label = "SMB share"
                elif name.startswith("sftp:"):
                    label = "SFTP"
                else:
                    label = "GVFS"
                if _check_gvfs_readable(entry.path):
                    LOG.info("%s: %s", label, entry.path)
                    extras.append(entry.path)
                else:
                    LOG.warning(
                        "%s found but NOT readable as root: %s\n"
                        "  Fix: add 'user_allow_other' to /etc/fuse.conf,\n"
                        "  then remount the drive in your file manager.",
                        label, entry.path
                    )

    # ── External drives: all of /media/ ──────────────────────────────────────
    if os.path.isdir("/media"):
        for user_entry in os.scandir("/media"):
            if not user_entry.is_dir():
                continue
            for vol_entry in os.scandir(user_entry.path):
                if vol_entry.is_dir():
                    LOG.info("External drive: %s", vol_entry.path)
                    extras.append(vol_entry.path)

    return extras


def should_prune(path: str) -> bool:
    return any(path == p or path.startswith(p + "/") for p in PRUNE_DIRS)

# ── Priority file ──────────────────────────────────────────────────────────────

def ingest_priority_file(path: str, ptolemy: str, chunk_lines: int):
    """
    Stream a large file in chunk_lines-line slices without loading it into RAM.
    Each chunk is piped directly to ptolemy -l - and then discarded.
    """
    if not os.path.isfile(path):
        LOG.info("priority file not ready yet: %s", path)
        return
    size = os.path.getsize(path)
    LOG.info("priority file: %s  (%.1f MB) — streaming in %d-line chunks",
             path, size / 1e6, chunk_lines)
    chunk_num  = 0
    buf        = []
    buf_lines  = 0
    try:
        with open(path, "rb") as f:
            for raw_line in f:
                buf.append(raw_line.decode("utf-8", errors="replace"))
                buf_lines += 1
                if buf_lines >= chunk_lines:
                    learn_text("".join(buf), ptolemy)
                    chunk_num += 1
                    LOG.info("  priority chunk %d  (%.1f MB done)",
                             chunk_num, f.tell() / 1e6)
                    buf       = []
                    buf_lines = 0
            if buf:
                learn_text("".join(buf), ptolemy)
                chunk_num += 1
    except Exception as e:
        LOG.error("priority file error: %s", e)
        return
    LOG.info("priority file done: %d chunks", chunk_num)

# ── Main ingest loop ───────────────────────────────────────────────────────────

def ingest_tree(root: str, ptolemy: str, state: dict, state_path: str,
                batch_dirs: int, chunk_lines: int):
    done_set        = set(state["done_dirs"])
    dirs_since_save = 0
    accumulated     = []
    acc_lines       = 0

    def flush():
        nonlocal accumulated, acc_lines
        if accumulated:
            learn_chunked("\n".join(accumulated), ptolemy, chunk_lines)
            accumulated = []
            acc_lines   = 0

    for dirpath, dirnames, filenames in os.walk(root, topdown=True, followlinks=False):
        dirnames[:] = [
            d for d in sorted(dirnames)
            if not should_prune(os.path.join(dirpath, d))
            and not os.path.islink(os.path.join(dirpath, d))
        ]

        if should_prune(dirpath) or dirpath in done_set:
            continue

        LOG.info("[%s] %s  (%d files)", time.strftime("%H:%M:%S"), dirpath, len(filenames))

        for fname in sorted(filenames):
            fpath = os.path.join(dirpath, fname)
            if os.path.islink(fpath):
                continue
            try:
                st = os.stat(fpath)
                if st.st_size == 0 or st.st_size > 512 * 1024 * 1024:
                    continue
            except OSError:
                continue

            try:
                mime = mime_type(fpath)
            except Exception:
                mime = ""

            text = extract_text(fpath, mime)
            if text.strip():
                lines = text.splitlines()
                accumulated.extend(lines)
                acc_lines += len(lines)
                state["files_total"] += 1
                if acc_lines >= chunk_lines:
                    flush()

        state["done_dirs"].append(dirpath)
        done_set.add(dirpath)
        state["dirs_total"] += 1
        dirs_since_save += 1

        if dirs_since_save >= batch_dirs:
            flush()
            save_state(state, state_path)
            elapsed = time.time() - state["started"]
            LOG.info("checkpoint: %d dirs  %d files  %.0fs",
                     state["dirs_total"], state["files_total"], elapsed)
            dirs_since_save = 0

    flush()
    save_state(state, state_path)


# ── DEPTH 4: walk-the-hallway binary image mounting ───────────────────────────
#
# Walk-the-Hallway procedure:
#
#   Phase 1 — The Hallway (probe, zero mount time):
#     losetup --partscan --find --show image.bin  → /dev/loopN
#     Kernel exposes /dev/loopNp1, /dev/loopNp2, … in sysfs (/sys/block/loopN/).
#     lsblk reads that sysfs tree; blkid fills filesystem types.
#     losetup -d — exit the hallway.
#
#   Phase 2 — Open the Doors (mount):
#     losetup --partscan again.
#     For each partition that is not AppArmor/encrypted:
#       mount --mkdir -o ro -t type1,type2 /dev/loopNpM /mnt/hallway/img/pM
#         --mkdir  : util-linux 2.36+ creates mountpoint automatically
#         -t list  : mount(8) "Multiple types can be specified in a
#                    comma-separated list" (no spaces) — tries each in order
#     ingest_tree() over each mountpoint at current DEPTH.
#     umount -l all, losetup -d.
#
#   efivars / EFI System Partition (vfat): WANTED — mounted and ingested.
#   AppArmor / securityfs: DEATH TRAP — never mounted (see _SKIP_FSTYPES).

def _losetup_attach(image_path: str) -> str:
    r = subprocess.run(
        ["losetup", "--partscan", "--find", "--show", image_path],
        capture_output=True, text=True, timeout=30
    )
    if r.returncode != 0:
        raise RuntimeError(f"losetup failed: {r.stderr.strip()}")
    return r.stdout.strip()


def _losetup_detach(loop_dev: str):
    subprocess.run(["losetup", "-d", loop_dev], capture_output=True, timeout=10)


def _probe_partitions(loop_dev: str) -> list[dict]:
    """Query lsblk (reads /sys/block/loopN/ sysfs) for child partitions."""
    r = subprocess.run(
        ["lsblk", "--json", "--output", "NAME,FSTYPE,LABEL,SIZE,PARTTYPE", loop_dev],
        capture_output=True, text=True, timeout=15
    )
    if r.returncode != 0:
        return []
    try:
        tree = json.loads(r.stdout)
        result = []
        for dev in tree.get("blockdevices", []):
            for child in dev.get("children", []):
                result.append({
                    "dev":      f"/dev/{child.get('name', '')}",
                    "fstype":   child.get("fstype")   or "",
                    "label":    child.get("label")    or "",
                    "size":     child.get("size")     or "",
                    "parttype": child.get("parttype") or "",
                })
        return result
    except Exception:
        return []


def _skip_partition(p: dict) -> bool:
    """
    True if this partition must not be mounted.
    AppArmor securityfs = death trap.  Encrypted = unreadable.
    efivars / EFI System Partition intentionally not skipped.
    """
    fstype = (p.get("fstype") or "").lower()
    label  = (p.get("label")  or "").lower()
    if fstype in {f.lower() for f in _SKIP_FSTYPES}:
        return True
    if any(skip.lower() in label for skip in _SKIP_LABELS):
        return True
    return False


def walk_hallway_images(image_paths: list[str], mount_base: str,
                        ptolemy: str, state: dict, state_path: str,
                        batch_dirs: int, chunk_lines: int):
    """
    Depth-4 binary image ingest via walk-the-hallway mount procedure.
    Only called when DEPTH >= 4 and --images are supplied.
    """
    for image_path in image_paths:
        if not os.path.isfile(image_path):
            LOG.warning("image not found: %s", image_path)
            continue

        LOG.info("walk-the-hallway: %s", image_path)
        loop_dev = None
        mounted  = []

        try:
            # ── Phase 1: the hallway (probe via sysfs) ────────────────────────
            loop_dev = _losetup_attach(image_path)
            LOG.info("  loop: %s  (partition scan active)", loop_dev)
            time.sleep(1)

            partitions = _probe_partitions(loop_dev)
            if not partitions:
                LOG.warning("  no partitions in %s", image_path)
                _losetup_detach(loop_dev); loop_dev = None
                continue

            all_types    = [p["fstype"] for p in partitions
                            if p["fstype"] and not _skip_partition(p)]
            unique_types = list(dict.fromkeys(all_types))
            combined_t   = ",".join(unique_types) if unique_types else "auto"
            LOG.info("  partitions: %d  combined -t: %s", len(partitions), combined_t)

            for p in partitions:
                status = "SKIP apparmor/encrypted" if _skip_partition(p) else "ok"
                LOG.info("  [%s] %s  fstype=%s  size=%s  label=%s",
                         status, p["dev"], p["fstype"], p["size"], p["label"])

            _losetup_detach(loop_dev); loop_dev = None
            LOG.info("  probe done — exiting hallway")

            # ── Phase 2: open the doors (mount each partition) ─────────────────
            loop_dev = _losetup_attach(image_path)
            time.sleep(1)
            img_stem = os.path.splitext(os.path.basename(image_path))[0]

            for i, p in enumerate(partitions):
                if _skip_partition(p):
                    LOG.info("  skip apparmor/encrypted: %s (%s)", p["dev"], p["fstype"])
                    continue

                mountpoint = os.path.join(mount_base, img_stem, f"p{i+1}")
                fstype_arg = p["fstype"] if p["fstype"] else combined_t

                LOG.info("  mount %s → %s  -t %s", p["dev"], mountpoint, fstype_arg)
                r = subprocess.run(
                    ["mount", "--mkdir", "-o", "ro", "-t", fstype_arg,
                     p["dev"], mountpoint],
                    capture_output=True, text=True, timeout=30
                )
                if r.returncode != 0:
                    LOG.warning("  mount failed: %s  (%s)", p["dev"], r.stderr.strip())
                    continue

                mounted.append(mountpoint)
                ingest_tree(mountpoint, ptolemy, state, state_path,
                            batch_dirs, chunk_lines)

        except Exception as e:
            LOG.error("walk-the-hallway error %s: %s", image_path, e)

        finally:
            for mp in reversed(mounted):
                subprocess.run(["umount", "-l", mp], capture_output=True, timeout=30)
                LOG.info("  umounted %s", mp)
            if loop_dev:
                _losetup_detach(loop_dev)
                LOG.info("  detached %s", loop_dev)


# ── Entry point ────────────────────────────────────────────────────────────────

def main():
    global DEPTH

    ap = argparse.ArgumentParser(
        description="ptolemy full-system ingest (Python3, resumable, run as root)"
    )
    ap.add_argument("--root",        default="/",         help="root directory")
    ap.add_argument("--first",       default="/SystemTree.txt", help="priority file, streamed first")
    ap.add_argument("--ptolemy",     default=PTOLEMY,     help="path to ptolemy binary")
    ap.add_argument("--state",       default=STATE_FILE,  help="JSON state file")
    ap.add_argument("--batch-dirs",  type=int, default=DEFAULT_BATCH, help="checkpoint every N dirs")
    ap.add_argument("--chunk-lines", type=int, default=CHUNK_LINES,   help="max lines per learn call")
    ap.add_argument("--extra",       action="append", default=[],     help="extra root (repeatable)")
    ap.add_argument("--no-auto-extra", action="store_true",           help="skip GVFS/media detection")
    ap.add_argument("--depth", type=int, default=1, metavar="N",
                    help="extraction depth: 0=text 1=+pdf 2=+archives 3=+docs 4=+images (default: 1)")
    ap.add_argument("--images",     action="append", default=[],      help="binary image to mount+ingest (depth 4)")
    ap.add_argument("--mount-base", default="/mnt/hallway",           help="base dir for hallway mounts")
    ap.add_argument("-v", "--verbose", action="store_true")
    args = ap.parse_args()

    DEPTH = args.depth

    logging.basicConfig(
        level=logging.DEBUG if args.verbose else logging.INFO,
        format="%(asctime)s  %(levelname)-5s  %(message)s",
        datefmt="%H:%M:%S",
    )

    LOG.info("depth=%d  (0=text 1=+pdf 2=+archives 3=+docs 4=+images)", DEPTH)

    if not os.path.isfile(args.ptolemy) or not os.access(args.ptolemy, os.X_OK):
        LOG.error("ptolemy not found: %s  (build with: cd PtolC && make)", args.ptolemy)
        sys.exit(1)

    r = subprocess.run([args.ptolemy, "-s"], capture_output=True, timeout=10)
    if r.returncode == 0:
        LOG.info("ptolemy status:\n%s", r.stdout.decode("utf-8", errors="replace").strip())
    else:
        LOG.warning("ptolemy -s non-zero — continuing anyway")

    state = load_state(args.state)
    if state["dirs_total"] > 0:
        LOG.info("RESUMING: %d dirs  %d files already done",
                 state["dirs_total"], state["files_total"])

    # ── Priority file first ───────────────────────────────────────────────────
    if args.first and args.first not in state.get("done_dirs", []):
        LOG.info("=== Priority file ===")
        ingest_priority_file(args.first, args.ptolemy, args.chunk_lines)
        state["done_dirs"].append(args.first)
        save_state(state, args.state)

    # ── Auto-detect GVFS / external drives ───────────────────────────────────
    extra_list = list(args.extra)
    if not args.no_auto_extra:
        extra_list += auto_extra_roots()

    # ── Depth 4: binary image mounting ───────────────────────────────────────
    if DEPTH >= 4 and args.images:
        LOG.info("=== Walk-the-Hallway (depth 4) ===")
        walk_hallway_images(args.images, args.mount_base,
                            args.ptolemy, state, args.state,
                            args.batch_dirs, args.chunk_lines)

    # ── Main filesystem traversal ─────────────────────────────────────────────
    for root in [args.root] + extra_list:
        if not os.path.isdir(root):
            LOG.warning("root not accessible: %s", root)
            continue
        LOG.info("=== Traversing: %s  (depth=%d) ===", root, DEPTH)
        ingest_tree(root, args.ptolemy, state, args.state,
                    args.batch_dirs, args.chunk_lines)

    # ── Final report ──────────────────────────────────────────────────────────
    elapsed = time.time() - state["started"]
    LOG.info("════════════════════════════════════════")
    LOG.info("INGEST COMPLETE  depth=%d", DEPTH)
    LOG.info("  dirs  visited : %d", state["dirs_total"])
    LOG.info("  files learned : %d", state["files_total"])
    LOG.info("  elapsed       : %.0f s  (%.1f h)", elapsed, elapsed / 3600)
    LOG.info("════════════════════════════════════════")

    r = subprocess.run([args.ptolemy, "-s"], capture_output=True, timeout=10)
    if r.returncode == 0:
        print(r.stdout.decode("utf-8", errors="replace"))


if __name__ == "__main__":
    main()

# Evenly's listener: puts the words of a song on the time they are really sung in the very recording that plays.
#
# The lyrics databases are timed on some cut of the song (NetEase: every word, LRCLIB: every line) and the player may play another
# (a clip with an intro, an edit, a live take), or the database is simply off. So: find the same recording on YouTube (its title and
# length, within 1.5 s, as the player reports them), take its sound, let Whisper hear every word with its time, and line the words of
# the lyrics up with what was heard (a sequence alignment that forgives misheard words: "doors" for "dosed"). Words heard surely are
# placed where they were heard; the rest are put between their neighbours (with the database's own spacing when it has word times).
# The result is a NetEase-style "yrc" on the clock of what plays, which Evenly then shows as it is.
#
# python evenly_listen.py --artist A --title T --raw "player title" --duration S --lyrics cached.yrc|.lrc --out key.heard.yrc
# Exit 0 with --out written; otherwise --out + ".none" holds the reason (not tried again for that song).
import argparse, bisect, json, os, re, statistics, sys, tempfile, time, traceback

# the voice separator on the graphics card: its own Python environment (torch-directml/onnxruntime-directml must not touch the
# packages Whisper uses), C:\dev\dmlenv with "audio-separator"; models in C:\dev\sepmodels
GPU_SEPARATOR = r"C:\dev\dmlenv\Scripts\audio-separator.exe"
SEP_MODELS = r"C:\dev\sepmodels"

ap = argparse.ArgumentParser()
ap.add_argument("--artist", default="")
ap.add_argument("--title", required=True)
ap.add_argument("--raw", default="")
ap.add_argument("--duration", type=float, default=0.0)
ap.add_argument("--lyrics", required=True)
ap.add_argument("--out", required=True)
ap.add_argument("--audio", default="")          # a sound file to use instead of downloading (tests)
ap.add_argument("--keep", action="store_true")
args = ap.parse_args()
log = open(args.out + ".log", "w", encoding="utf-8")
def say(*a):
    print(*a, file=log, flush=True)

def give_up(why):
    say("no:", why)
    open(args.out + ".none", "w", encoding="utf-8").write(why + "\n")
    sys.exit(1)

def norm(w):
    w = w.lower().replace("ё", "е")
    return re.sub(r"[^0-9a-zа-я]", "", w)

# ---- the lyrics as pieces: (line, text as shown, normalised, source time or None) ------------------------------------------------
src = open(args.lyrics, encoding="utf-8").read()
lines = []           # [{t, pieces:[{text, n, t, len}]}]
if args.lyrics.endswith(".yrc"):
    for raw in src.splitlines():
        m = re.match(r"^\[(\d+),(\d+)\](.*)$", raw.strip())
        if not m: continue
        ps = [{"text": t, "n": norm(t), "t": int(a) / 1000, "len": int(b) / 1000} for a, b, t in re.findall(r"\((\d+),(\d+),\d+\)((?:(?!\(\d+,\d+,\d+\)).)*)", m.group(3))]
        for p in ps: p["text"] = p["text"].replace("\\", "")      # NetEase escapes quotes, even as a piece of its own: You \ 're
        ps = [p for p in ps if p["text"]]
        if ps and any(p["n"] for p in ps): lines.append({"t": int(m.group(1)) / 1000, "pieces": ps})
    wordTimes = True
else:
    for raw in src.splitlines():
        m = re.match(r"^\[(\d+):(\d+(?:\.\d+)?)\](.*)$", raw.strip())
        if not m: continue
        text = m.group(3).strip()
        if not norm(text): continue
        ws = text.split()
        lines.append({"t": int(m.group(1)) * 60 + float(m.group(2)),
                      "pieces": [{"text": w + (" " if i + 1 < len(ws) else ""), "n": norm(w), "t": None, "len": None} for i, w in enumerate(ws)]})
    wordTimes = False
if len(lines) < 3: give_up("lyrics too short")
cyr = bool(re.search(r"[а-яА-Я]", src))
say("lyrics: %d lines, word times %s, %s" % (len(lines), wordTimes, "ru" if cyr else "en"))

# ---- the same recording, from YouTube --------------------------------------------------------------------------------------------
workDir = tempfile.mkdtemp(prefix="evenly_")
audio = args.audio
if not audio:
    import yt_dlp
    want = args.duration
    queries = []
    if args.raw: queries.append(args.raw + (" " + args.artist if args.artist and args.artist.lower() not in args.raw.lower() else ""))
    queries.append(args.artist + " " + args.title)
    best = None
    with yt_dlp.YoutubeDL({"quiet": True, "no_warnings": True, "extract_flat": True, "skip_download": True}) as y:
        for q in queries:
            try:
                res = y.extract_info("ytsearch8:" + q, download=False)
            except Exception as e:
                say("search failed:", e); continue
            for e in res.get("entries") or []:
                d = e.get("duration") or 0
                gap = abs(d - want) if want > 1 and d else 99
                titleHit = args.raw and norm(e.get("title", "")) == norm(args.raw)
                score = gap - (2.0 if titleHit else 0.0)
                say("  candidate %.1f s gap %.1f %s %s" % (d, gap, "TITLE" if titleHit else "", e.get("title", "")[:70]))
                if gap <= 1.5 and (best is None or score < best[0]): best = (score, e)
            if best: break
    if not best: give_up("no recording of the same length on YouTube")
    url = "https://www.youtube.com/watch?v=" + best[1]["id"]
    say("using", url, best[1].get("title"))
    opts = {"quiet": True, "no_warnings": True, "format": "bestaudio[ext=m4a]/bestaudio", "outtmpl": os.path.join(workDir, "a.%(ext)s")}
    with yt_dlp.YoutubeDL(opts) as y:
        info = y.extract_info(url, download=True)
    audio = [os.path.join(workDir, f) for f in os.listdir(workDir)][0]

# ---- hear every word -------------------------------------------------------------------------------------------------------------
t0 = time.time()
from faster_whisper import WhisperModel
model = WhisperModel("small" if cyr else "small.en", device="cpu", compute_type="int8", cpu_threads=max(1, (os.cpu_count() or 2) - 1))
# the words of the song as a prompt: Whisper hears sung words far better when it knows what to expect
prompt = " ".join(" ".join(p["text"].strip() for p in l["pieces"]) for l in lines)[:600]
segs, info = model.transcribe(audio, language="ru" if cyr else "en", word_timestamps=True, vad_filter=False,
                              condition_on_previous_text=False, initial_prompt=prompt, beam_size=5)
_simCache = {}
def fail(why):
    raise ValueError(why)

def build(heard, final):
    # ---- line the lyrics up with what was heard (Needleman-Wunsch, similar words count as matches) -----------------------------------
    flat = [(li, pi, p) for li, l in enumerate(lines) for pi, p in enumerate(l["pieces"]) if p["n"]]
    A = [p["n"] for _, _, p in flat]
    B = [h[2] for h in heard]
    def sim(a, b):
        if a == b: return 1.0
        if len(a) < 2 or len(b) < 2 or abs(len(a) - len(b)) > max(len(a), len(b)) // 2: return 0.0
        key = (a, b)
        if key not in _simCache:
            import difflib
            _simCache[key] = difflib.SequenceMatcher(None, a, b, autojunk=False).ratio()
        return _simCache[key]
    n, m = len(A), len(B)
    GAP = -0.4
    score = [[0.0] * (m + 1) for _ in range(n + 1)]
    back = [[0] * (m + 1) for _ in range(n + 1)]
    for i in range(1, n + 1): score[i][0] = score[i - 1][0] + GAP; back[i][0] = 1
    for j in range(1, m + 1): score[0][j] = 0.0; back[0][j] = 2           # talk or noise before the first word: free
    for i in range(1, n + 1):
        ai = A[i - 1]
        for j in range(1, m + 1):
            s = sim(ai, B[j - 1])
            d = score[i - 1][j - 1] + (s if s >= 0.6 else -0.6)
            u = score[i - 1][j] + GAP
            l = score[i][j - 1] + GAP
            if d >= u and d >= l: score[i][j] = d; back[i][j] = 0
            elif u >= l: score[i][j] = u; back[i][j] = 1
            else: score[i][j] = l; back[i][j] = 2
    # the lyrics after what has been heard so far are simply not reached: the alignment may end at any word (free end gap),
    # otherwise the last heard words got pulled onto the last lines of the song
    pairs = []
    i, j = max(range(n + 1), key=lambda r: score[r][m]), m
    while i > 0 and j > 0:
        if back[i][j] == 0:
            if sim(A[i - 1], B[j - 1]) >= 0.6 and heard[j - 1][3] >= 0.25: pairs.append((i - 1, j - 1))
            i -= 1; j -= 1
        elif back[i][j] == 1: i -= 1
        else: j -= 1
    pairs.reverse()
    say("matched %d of %d lyric words" % (len(pairs), n))
    if len(pairs) < (max(12, 0.25 * n) if final else 8): fail("too few words matched (%d of %d)" % (len(pairs), n))

    # anchors that disagree with their neighbourhood are chance matches (a chorus word heard in another chorus): dropped
    def srcTime(k):
        li, pi, p = flat[k]
        return p["t"] if p["t"] is not None else lines[li]["t"]
    offs = [heard[j][0] - srcTime(k) for k, j in pairs]
    if not wordTimes:     # a word later in its line is later by the singing: measure the line by its first heard word
        firstHeard = {}
        for q, (k, j) in enumerate(pairs): firstHeard.setdefault(flat[k][0], offs[q])
        offs = [firstHeard[flat[k][0]] for k, j in pairs]
    good = []
    for q, (k, j) in enumerate(pairs):
        lo, hi = max(0, q - 4), min(len(pairs), q + 5)
        med = statistics.median(offs[lo:hi])
        tol = 0.6 if wordTimes else 4.0
        if abs(offs[q] - med) <= tol: good.append((k, j))
    say("kept %d anchors; offset median %+.2f s (first %+.2f, last %+.2f)" % (len(good), statistics.median(offs), offs[0], offs[-1]))
    if len(good) < (max(10, 0.2 * n) if final else 6): fail("anchors disagree")

    # ---- every word gets its time ------------------------------------------------------------------------------------------------------
    T = [None] * n          # start, in the clock of the recording
    L = [None] * n
    for k, j in good:
        T[k] = heard[j][0]
        srcLen = flat[k][2]["len"]
        L[k] = srcLen if srcLen else max(0.08, heard[j][1] - heard[j][0])
    anchorK = [k for k, _ in good]
    # how far the source is off around a word. With line times only, a word's own source time is its line's start, so only words that
    # START a line say how far the line is off (a word later in the line is later by the singing, not by the source)
    firstOfLine = set()
    for k in range(n):
        if k == 0 or flat[k][0] != flat[k - 1][0]: firstOfLine.add(k)
    offK = anchorK if wordTimes else ([k for k in anchorK if k in firstOfLine] or anchorK)
    def localOffset(k):
        q = bisect.bisect_left(offK, k)
        near = [offK[x] for x in range(max(0, q - 3), min(len(offK), q + 3))]
        return statistics.median([T[x] - srcTime(x) for x in near])
    for k in range(n):
        if T[k] is not None: continue
        q = bisect.bisect_left(anchorK, k)
        prevK = anchorK[q - 1] if q > 0 else None
        nextK = anchorK[q] if q < len(anchorK) else None
        if wordTimes:
            T[k] = srcTime(k) + localOffset(k)                  # the database's own spacing, moved like its neighbours
            L[k] = flat[k][2]["len"] or 0.2
        elif prevK is not None and nextK is not None and flat[prevK][0] == flat[k][0] == flat[nextK][0]:
            # inside a line: spread by letters between the heard neighbours
            span = [x for x in range(prevK + 1, nextK)]
            chars = [len(A[x]) + 1 for x in span]
            t0_, t1_ = T[prevK] + L[prevK], T[nextK]
            acc = sum(chars[:span.index(k)])
            T[k] = t0_ + (t1_ - t0_) * acc / max(1, sum(chars)); L[k] = (t1_ - t0_) * chars[span.index(k)] / max(1, sum(chars))
        elif nextK is not None and flat[nextK][0] == flat[k][0]:
            # before the first word heard in its line: back from it, 0.3 s a word
            T[k] = T[nextK] - 0.3 * (nextK - k); L[k] = 0.25
        elif prevK is not None and flat[prevK][0] == flat[k][0]:
            # after the last word heard in its line: on from it
            T[k] = T[prevK] + L[prevK] + 0.3 * (k - prevK - 1); L[k] = 0.25
        else:
            # nothing heard nearby in the line: the line's start moved like its neighbours, words 0.3 s apart
            li = flat[k][0]
            first = next(x for x in range(n) if flat[x][0] == li)
            T[k] = lines[li]["t"] + localOffset(k) + 0.3 * (k - first); L[k] = 0.25
    # With word times from a person (NetEase), every word keeps them, moved by how far its stretch of the song is off (the median of
    # the heard words around it): Whisper says reliably WHERE the song is, a person said better how the words sit in it.
    if wordTimes:
        shift = [localOffset(k) for k in range(n)]
        for k in range(n):
            T[k] = srcTime(k) + shift[k]
            L[k] = flat[k][2]["len"] or L[k] or 0.2
    # in order, and a word ends where the next begins at the latest
    for k in range(1, n):
        if T[k] < T[k - 1] + 0.02: T[k] = T[k - 1] + 0.02
    for k in range(n - 1):
        L[k] = max(0.06, min(L[k], T[k + 1] - T[k]))

    # ---- written as yrc (pieces without letters, like "’", ride on the piece before) -------------------------------------------------
    out = []
    dur = args.duration
    if dur > 1: out.append("[length:%.1f]" % dur)
    idx = {(li, pi): k for k, (li, pi, _) in enumerate(flat)}
    for li, l in enumerate(lines):
        parts = []
        last = None
        for pi, p in enumerate(l["pieces"]):
            k = idx.get((li, pi))
            if k is None:
                if parts: parts[-1][2] += p["text"]
                continue
            parts.append([T[k], L[k], p["text"]])
        if not parts: continue
        st, en = parts[0][0], parts[-1][0] + parts[-1][1]
        out.append("[%d,%d]" % (round(st * 1000), round((en - st) * 1000)) + "".join("(%d,%d,0)%s" % (round(a * 1000), round(b * 1000), t) for a, b, t in parts))
    open(args.out + ".tmp", "w", encoding="utf-8").write("\n".join(out) + "\n")
    os.replace(args.out + ".tmp", args.out)          # whole or not at all: Evenly may read it any moment
    say("written", args.out)

# ---- hear the song in order; every ~30 s of it the words are lined up again and written (Evenly takes each new version) ---------
heard = []
lastBuild = 0.0
done = False
for sg in segs:
    for w in sg.words or []:
        nn = norm(w.word)
        if nn: heard.append((w.start, w.end, nn, w.probability))
    if heard and heard[-1][1] - lastBuild >= 15.0:
        lastBuild = heard[-1][1]
        try:
            build(heard, False); say("partial up to %.0f s (%.0f s in)" % (lastBuild, time.time() - t0))
        except ValueError as e:
            say("partial skipped:", e)
say("heard %d words in %.0f s" % (len(heard), time.time() - t0))
try:
    build(heard, True)
except ValueError as e:
    give_up(str(e))

# ---- second pass, in the background of the song: the voice alone (Demucs), to put every word's start on the moment the voice comes
# in and its end where the note really stops. Whisper's word ends are its weakest part; a held "di-i-ied" filled too fast or too slow.
# Only where the database has no word lengths of its own (NetEase's are timed by a person). ~1.3x the song's length on 4 cores.
def refine_with_voice():
    import subprocess, numpy as np, soundfile as sf
    sepDir = os.path.join(workDir, "sep")
    quiet = dict(check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
    # on the graphics card first (MDX-Net "Kim_Vocal_2" through DirectML, any DX12 card incl. AMD: ~8x the song's speed on a
    # Radeon RX Vega), else Demucs on the processor (~0.8x). Demucs itself cannot run on DirectML (complex numbers).
    t1 = time.time()
    try:
        if not os.path.exists(GPU_SEPARATOR): raise FileNotFoundError(GPU_SEPARATOR)
        subprocess.run([GPU_SEPARATOR, audio, "-m", "Kim_Vocal_2.onnx", "--model_file_dir", SEP_MODELS, "--output_dir", sepDir,
                        "--single_stem", "Vocals", "--use_directml", "--log_level", "error"], **quiet)
        voc = [os.path.join(sepDir, f) for f in os.listdir(sepDir) if "(Vocals)" in f][0]
        say("voice separated on the graphics card in %.0f s" % (time.time() - t1))
    except Exception as e:
        say("graphics card separation unavailable (%s), using the processor" % e)
        subprocess.run([sys.executable, "-m", "demucs", "--two-stems", "vocals", "-n", "htdemucs", "-j", str(max(1, (os.cpu_count() or 2) - 1)),
                        "-o", sepDir, audio], **quiet)
        voc = [os.path.join(dp, f) for dp, _, fs in os.walk(sepDir) for f in fs if f == "vocals.wav"][0]
        say("voice separated on the processor in %.0f s" % (time.time() - t1))
    x, sr = sf.read(voc, always_2d=True)
    x = x.mean(axis=1)
    hop = int(sr * 0.01)
    env = np.sqrt(np.convolve(x[: len(x) // hop * hop].reshape(-1, hop).__pow__(2).mean(axis=1), np.ones(3) / 3, "same"))   # 10 ms RMS
    db = 20 * np.log10(env + 1e-6)
    rows = open(args.out, encoding="utf-8").read().splitlines()
    words = []
    for r in rows:
        m = re.match(r"^\[(\d+),(\d+)\](.*)$", r)
        if m: words += [[int(a) / 1000, int(b) / 1000, t] for a, b, t in re.findall(r"\((\d+),(\d+),\d+\)((?:(?!\(\d+,\d+,\d+\)).)*)", m.group(3))]
    starts = [w[0] for w in words]
    moved = 0
    for k, w in enumerate(words):
        i0 = int(w[0] * 100)
        nxt = int(starts[k + 1] * 100) if k + 1 < len(words) else len(db) - 1
        if i0 >= len(db) - 5: continue
        # the start: the steepest rise of the voice within 0.15 s of where Whisper put it
        a, b = max(1, i0 - 15), min(len(db) - 1, i0 + 15)
        rise = db[a:b] - db[a - 1:b - 1]
        if rise.max() > 3.0:
            ns = (a + int(rise.argmax())) / 100
            if k == 0 or ns > words[k - 1][0] + 0.03: w[0] = ns; moved += 1
        # the end: the voice falls 12 dB under this word's peak (or silence) and stays there 60 ms, at the latest the next word
        s0 = int(w[0] * 100)
        seg = db[s0:max(s0 + 1, nxt)]
        if len(seg) < 3: continue
        # the word lasts until the voice has really gone (the LAST moment it still sounds before the next word): a consonant or a
        # breath inside the word dips the voice for a moment, the first dip cut "dosed" to 90 ms where it is held 0.9 s
        peak = seg.max()
        on = np.where(seg >= max(peak - 15.0, -45.0))[0]
        end = int(on[-1]) + 1 if len(on) else len(seg)
        w[1] = max(0.08, end / 100)
    say("voice pass: %d starts moved, ends from the voice" % moved)
    # written back in the same lines
    out, k = [], 0
    for r in rows:
        m = re.match(r"^\[(\d+),(\d+)\](.*)$", r)
        if not m: out.append(r); continue
        cnt = len(re.findall(r"\((\d+),(\d+),\d+\)", m.group(3)))
        ws = words[k:k + cnt]; k += cnt
        st, en = ws[0][0], ws[-1][0] + ws[-1][1]
        out.append("[%d,%d]" % (round(st * 1000), round((en - st) * 1000)) + "".join("(%d,%d,0)%s" % (round(a * 1000), round(b * 1000), t) for a, b, t in ws))
    open(args.out + ".tmp", "w", encoding="utf-8").write("\n".join(out) + "\n")
    os.replace(args.out + ".tmp", args.out)

if not wordTimes:
    try:
        refine_with_voice()
    except Exception as e:
        say("voice pass failed:", e)
if not args.keep and not args.audio:
    import shutil
    shutil.rmtree(workDir, ignore_errors=True)

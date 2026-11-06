#!/usr/bin/env python3
"""Make the HEVC test clips for venus-hevc.sh, with the md5 of a software decode.

usage: venus-hevc-clips.py OUTDIR

Needs ffmpeg with libx265. All clips are synthetic, so they can be shared. Main 10
goes to OUTDIR/main10, which venus-hevc.sh leaves alone unless asked.
"""
import hashlib
import os
import subprocess
import sys

OUT = sys.argv[1]
os.makedirs(os.path.join(OUT, 'main10'), exist_ok=True)


def run(*args):
    subprocess.run(args, check=True, stdin=subprocess.DEVNULL)


def encode(path, w, h, fps, secs, x265='', pix='yuv420p', tag=True):
    """Encode testsrc2 with a little noise, so it doesn't compress to nothing."""
    params = 'log-level=error:keyint=%d:min-keyint=%d' % (2 * fps, fps)
    if x265:
        params += ':' + x265
    cmd = ['ffmpeg', '-y', '-hide_banner', '-loglevel', 'error',
           '-f', 'lavfi', '-i', 'testsrc2=s=%dx%d:r=%d:d=%d' % (w, h, fps, secs),
           '-vf', 'noise=alls=8:allf=t', '-pix_fmt', pix,
           '-c:v', 'libx265', '-preset', 'fast', '-crf', '26', '-x265-params', params]
    if path.endswith('.mp4') and tag:
        cmd += ['-tag:v', 'hvc1']
    run(*cmd, path)


def soft_md5(path, pix='yuv420p'):
    """md5 and size of the raw frames from a software decode, without rescaling."""
    p = subprocess.Popen(['ffmpeg', '-hide_banner', '-loglevel', 'error', '-i', path,
                          '-noautoscale', '-f', 'rawvideo', '-pix_fmt', pix, '-'],
                         stdout=subprocess.PIPE, stdin=subprocess.DEVNULL)
    h, n = hashlib.md5(), 0
    while chunk := p.stdout.read(1 << 20):
        h.update(chunk)
        n += len(chunk)
    if p.wait():
        sys.exit('software decode failed: ' + path)
    return h.hexdigest(), n


CLIPS = [
    # name, w, h, fps, seconds, x265 options, what it tests
    ('480p30-crop', 854, 480, 30, 10, '', 'width not a multiple of 16'),
    ('720p30', 1280, 720, 30, 10, '', 'the earlier test size'),
    ('768p30-crop', 1366, 768, 30, 10, '', 'width and height cropped'),
    ('1080p30', 1920, 1080, 30, 20, '', 'common case, 1088 rows coded'),
    ('1080p60', 1920, 1080, 60, 10, '', 'real time at the common top rate'),
    ('1080p30-nowpp', 1920, 1080, 30, 10, 'no-wpp=1', 'no wavefront entry points'),
    ('1080p30-refs', 1920, 1080, 30, 10, 'ref=5:bframes=8:b-pyramid=1', 'large DPB'),
    ('1080p30-nob', 1920, 1080, 30, 10, 'bframes=0', 'no reordering'),
    ('1080p30-cra', 1920, 1080, 30, 10, 'open-gop=1', 'open GOP'),
    ('1080p30-idr', 1920, 1080, 30, 10, 'open-gop=0', 'closed GOP'),
    ('1536p30-panel', 2048, 1536, 30, 10, '', 'the panel size'),
    ('2160p30', 3840, 2160, 30, 5, '', 'the top of the stock range'),
]

rows = []
for name, w, h, fps, secs, x265, why in CLIPS:
    path = os.path.join(OUT, 'hevc-%s.mp4' % name)
    if not os.path.exists(path):
        encode(path, w, h, fps, secs, x265)
    md5, size = soft_md5(path)
    rows.append((name, os.path.basename(path), 'mp4', w, h, size // (w * h * 3 // 2), size, md5))
    print(name, why, flush=True)

# A resolution change inside one stream, in Annex B. Sizes differ, so frames is left out
parts = []
for w, h in ((1280, 720), (1920, 1080), (1280, 720)):
    part = os.path.join(OUT, 'part-%dx%d.hevc' % (w, h))
    if not os.path.exists(part):
        encode(part, w, h, 30, 3, 'open-gop=0')
    parts.append(part)
path = os.path.join(OUT, 'hevc-drc.hevc')
with open(path, 'wb') as out:
    for part in parts:
        out.write(open(part, 'rb').read())
for part in set(parts):
    os.unlink(part)
md5, size = soft_md5(path)
rows.append(('drc', os.path.basename(path), 'raw', 0, 0, 0, size, md5))
print('drc resolution change', flush=True)

# hev1 instead of hvc1: parameter sets in the stream, not only in the container
path = os.path.join(OUT, 'hevc-720p30-hev1.mp4')
if not os.path.exists(path):
    encode(path, 1280, 720, 30, 5, 'repeat-headers=1', tag=False)
md5, size = soft_md5(path)
rows.append(('720p30-hev1', os.path.basename(path), 'mp4', 1280, 720,
             size // (1280 * 720 * 3 // 2), size, md5))

with open(os.path.join(OUT, 'clips.txt'), 'w') as f:
    for r in rows:
        f.write(' '.join(str(x) for x in r) + '\n')

path = os.path.join(OUT, 'main10', 'hevc-1080p30-main10.mp4')
if not os.path.exists(path):
    encode(path, 1920, 1080, 30, 5, 'profile=main10', pix='yuv420p10le')
print('done:', len(rows), 'clips, and Main 10 kept apart')

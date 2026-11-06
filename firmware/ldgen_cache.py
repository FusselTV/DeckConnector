"""Run ESP-IDF's ldgen only when its result can change.

The build reruns ldgen (5 s) after every edit, because its rule depends on every library. Its output only reads
the libraries a linker fragment names, so the key leaves the others out.
Usage: ldgen_cache.py PYTHON ldgen.py ARGS... (ldgen_launch.sh routes the ldgen command here).
"""

import hashlib
import os
import runpy
import shutil
import sys
from pathlib import Path

ldgen, args = sys.argv[2], sys.argv[3:]
opt = dict(zip(args, args[1:]))
fragments = opt['--fragments-list'].split(';')
text = b''.join(Path(f).read_bytes() for f in fragments)

h = hashlib.sha256('\0'.join(args).encode())
for f in [opt['--config'], opt['--input'], opt['--kconfig'], opt['--env-file'], opt['--libraries-file'], *fragments,
          *sorted(Path(ldgen).parent.rglob('*.py'))]:
    h.update(Path(f).read_bytes())
for lib in Path(opt['--libraries-file']).read_text().split():
    if os.path.basename(lib).encode() in text:
        h.update(Path(lib).read_bytes())

cache = Path(os.environ.get('XDG_CACHE_HOME', Path.home() / '.cache')) / 'deckbridge/ldgen'
hit = cache / h.hexdigest()
if hit.exists():
    shutil.copyfile(hit, opt['--output'])
    sys.exit()

sys.argv = [ldgen, *args]
sys.path.insert(0, os.path.dirname(ldgen))
runpy.run_path(ldgen, run_name='__main__')
cache.mkdir(parents=True, exist_ok=True)
tmp = hit.with_suffix(f'.{os.getpid()}')
shutil.copyfile(opt['--output'], tmp)
tmp.replace(hit)
for old in sorted(cache.iterdir(), key=lambda p: p.stat().st_mtime)[:-20]:
    old.unlink()

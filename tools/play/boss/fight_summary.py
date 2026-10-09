#!/usr/bin/env python3
"""docs/32 9i: one line per boss run (build/boss/bs_NAME.log from runrec.sh): the fight's timing from the harness's L B
lines (READY onwards: avg vblanks a frame x100, worst vblanks, worst CPU hblanks; at the last tick of the fight's
phases), the hits, the guards of every L AT, and whether the next scene loaded.
Usage: fight_summary.py NAME... (logs in build/boss, BOSS_WORK= to move)"""
import os, re, sys

S = os.environ.get('BOSS_WORK') or os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', '..', 'build', 'boss')
for name in sys.argv[1:]:
    path = os.path.join(S, 'bs_%s.log' % name)
    if not os.path.exists(path):
        print('%-9s no log' % name)
        continue
    text = open(path, errors='replace').read()
    # the fight's ticks: from READY to the first scene load after it (a load's frames would count as the fight's)
    fight, ready = [], False
    for l in text.splitlines():
        if l.startswith('L READY'):
            ready = True
        elif ready and l.startswith('L SCENE'):
            break
        elif ready and l.startswith('L B ') and re.search(r' st=1 ', l):
            fight.append(l)
    last = fight[-1] if fight else ''
    g = lambda k, l: (re.search(r'\b%s=(\d+)' % k, l) or [None, '?'])[1]
    guards = 'clean'
    for l in text.splitlines():
        if l.startswith('L AT '):
            bad = [k for k in ('oom', 'missing', 'spriteMissing', 'relocations', 'ghost', 'stgFail') if g(k, l) not in ('0', '?')]
            if bad:
                guards = ' '.join('%s=%s' % (k, g(k, l)) for k in bad)
    scenes = re.findall(r'^L SCENE .*?folder=(\S+)', text, re.M)
    hits = len(re.findall(r'^L HIT ', text, re.M))
    end = 'next scene' if len(scenes) > 1 else ('TIMEOUT' if 'L TIMEOUT' in text else ('STUCK' if 'L STUCK' in text else 'no exit'))
    print('%-9s avg=%s worstV=%s maxHbl=%s hits=%d guards=%s %s (%s)' % (
        name, g('avg', last), g('wv', last), g('mh', last), hits, guards, end, ' > '.join(scenes)))

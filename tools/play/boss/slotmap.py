# slotmap.py FOLDER SCENEID NAME... (from the repo root): runtime slots of the named objects' entities (stock InitObjects
# compaction: a record passes Mania mode when its filter & 3 (no filter variable: 0xFF); slot = 64 + passing records before it)
import sys; sys.path.insert(0, 'tools'); import rsdk5_formats as F
folder, sid, names = sys.argv[1], sys.argv[2], sys.argv[3:]
g = F.read_gameconfig('../SonicMania_Extracted/Data/Game/GameConfig.bin')
sc = F.read_stageconfig('../SonicMania_Extracted/Data/Stages/%s/StageConfig.bin' % folder)
s = F.read_scene('../SonicMania_Extracted/Data/Stages/%s/Scene%s.bin' % (folder, sid), g['objects'] + sc['objects'])
recs = sorted((e['slot'], o['name'], e) for o in s['objects'] for e in o['entities'])
run = 64
for slot, name, e in recs:
    ok = (e['filter'] & 3) != 0
    if name in names:
        print(name, 'scene', slot, 'runtime', run if ok else None, 'x', e['x'] >> 16, 'y', e['y'] >> 16)
    if ok:
        run += 1

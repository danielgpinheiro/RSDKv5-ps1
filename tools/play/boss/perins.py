import sys
p, q = sys.argv[1], sys.argv[2]
s = open(p).read().replace('@PER@', open(q).read().rstrip())
open(p, 'w').write(s)

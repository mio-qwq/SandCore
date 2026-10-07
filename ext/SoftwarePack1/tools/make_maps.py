"""六张手工房间蓝图；路径、钥匙和门的位置固定，生成时检查尺寸。

四区按西南出生 -> 西北红钥匙 -> 东南蓝钥匙 -> 东北出口相连。
关内隔墙和遭遇是逐关设计；不使用随机迷宫或不可达的装饰物。
"""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
NAMES = ['Dock breach', 'Foundry', 'Relay station', 'Cryo vault', 'Reactor', 'Citadel']
BRIEFS = ['Secure the dock. Find red, then blue access keys.',
          'Arm up in the foundry. Riflemen cover the passages.',
          'Drones patrol the relay. Use cover and reload safely.',
          'The vault has heavy guards. Collect armor before pushing.',
          'Shut down the reactor route. Both keycards are required.',
          'Defeat the Warden, then activate the extraction switch.']
WALLS = [
    [(6,15,1,3),(16,15,3,1),(3,6,4,1),(16,6,1,3)],
    [(3,16,5,1),(7,3,1,5),(16,15,1,4),(15,4,4,1)],
    [(6,14,1,5),(3,6,5,1),(17,15,3,1),(16,5,1,4)],
    [(3,15,4,1),(7,4,1,4),(15,17,4,1),(17,4,1,5)],
    [(7,15,1,4),(4,4,1,4),(16,15,1,3),(15,6,4,1)],
    [(5,14,4,1),(7,5,1,4),(15,16,4,1),(17,5,1,4)],
]


def main():
    levels = []
    for level in range(6):
        g = [['.' for x in range(24)] for y in range(24)]
        for k in range(24):
            g[0][k] = g[23][k] = g[k][0] = g[k][23] = '#'
        material = ['#','B','W','W','B','W'][level]
        for y in range(1,23): g[y][11] = material
        for x in range(1,23): g[11][x] = material
        for x,y,w,h in WALLS[level]:
            for yy in range(y,y+h):
                for xx in range(x,x+w): g[yy][xx] = material
        placements = {
            (3,20):'P', (5,11):'d', (11,18):'r', (18,11):'b',
            (3,3):'1', (20,20):'2', (21,3):'E',
            (4,20):'A', (3,18):'R', (4,17):'G', (5,19):'S',
            (3,13):'M', (8,8):'M', (14,21):'A', (21,14):'M',
            (15,8):'R', (20,6):'S', (18,20):'C', (9,2):'T', (13,2):'T',
            (6,20):'g', (8,13):'g', (3,8):'s', (8,3):'s',
            (15,20):'h', (20,16):'s', (15,3):'h', (20,8):'s'
        }
        if level>=2: placements.update({(9,17):'v',(14,13):'v',(19,3):'v'})
        if level>=3: placements.update({(9,6):'h',(21,18):'h'})
        if level==5: placements[(19,5)]='o'
        for (x,y),ch in placements.items():
            if ch not in 'drb' and g[y][x] != '.':
                raise ValueError((level,x,y,'placement intersects a wall'))
            g[y][x]=ch
        rows=[''.join(row) for row in g]
        assert len(rows)==24 and all(len(row)==24 for row in rows)
        levels.append(rows)
    text='/* 固定房间、钥匙链与六关遭遇，由 tools/make_maps.py 生成。 */\n'
    text+='static const char *rd_maps[RD_LEVELS][RD_MH]={\n'
    text+=',\n'.join('{\n'+',\n'.join(' "'+row+'"' for row in rows)+'\n}' for rows in levels)+'\n};\n'
    text+='static const char *rd_level_names[RD_LEVELS]={'+','.join('"'+s+'"' for s in NAMES)+'};\n'
    text+='static const char *rd_briefs[RD_LEVELS]={'+','.join('"'+s+'"' for s in BRIEFS)+'};\n'
    (ROOT/'raider/raider_maps.inc').write_text(text,encoding='utf-8')
    print('Generated six authored 24 x 24 levels')


if __name__=='__main__': main()

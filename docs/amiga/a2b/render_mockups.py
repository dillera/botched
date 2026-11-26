#!/usr/bin/env python3
"""Design illustrations only. Pillow, no game/runtime changes or generated assets.
Run from any directory. All mockups use 320x256, one fixed OCS 16-colour palette.
Map geometry is read from shipped resources; scenario entities are illustrative.
"""
from pathlib import Path
import json
from PIL import Image, ImageDraw
HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
PAL = ['#111122','#222233','#334455','#556677','#889999','#eeeecc',
       '#ff7766','#66ccff','#88dd77','#cc99ff','#eeaa44','#996633',
       '#ff4433','#448888','#bbbbbb','#ffdd88']
P = [tuple(bytes.fromhex(c[1:])) for c in PAL]
PC = [0,6,7,8,9]
# Original 5x7 bitmap lettering; fixed six-pixel advance, no antialiasing.
DATA = {
'A':'01110/10001/10001/11111/10001/10001/10001','B':'11110/10001/10001/11110/10001/10001/11110',
'C':'01111/10000/10000/10000/10000/10000/01111','D':'11110/10001/10001/10001/10001/10001/11110',
'E':'11111/10000/10000/11110/10000/10000/11111','F':'11111/10000/10000/11110/10000/10000/10000',
'G':'01111/10000/10000/10111/10001/10001/01111','H':'10001/10001/10001/11111/10001/10001/10001',
'I':'111/010/010/010/010/010/111','J':'00111/00010/00010/00010/00010/10010/01100',
'K':'10001/10010/10100/11000/10100/10010/10001','L':'10000/10000/10000/10000/10000/10000/11111',
'M':'10001/11011/10101/10101/10001/10001/10001','N':'10001/11001/10101/10011/10001/10001/10001',
'O':'01110/10001/10001/10001/10001/10001/01110','P':'11110/10001/10001/11110/10000/10000/10000',
'Q':'01110/10001/10001/10001/10101/10010/01101','R':'11110/10001/10001/11110/10100/10010/10001',
'S':'01111/10000/10000/01110/00001/00001/11110','T':'11111/00100/00100/00100/00100/00100/00100',
'U':'10001/10001/10001/10001/10001/10001/01110','V':'10001/10001/10001/10001/10001/01010/00100',
'W':'10001/10001/10001/10101/10101/10101/01010','X':'10001/10001/01010/00100/01010/10001/10001',
'Y':'10001/10001/01010/00100/00100/00100/00100','Z':'11111/00001/00010/00100/01000/10000/11111',
'0':'01110/10001/10011/10101/11001/10001/01110','1':'00100/01100/00100/00100/00100/00100/01110',
'2':'01110/10001/00001/00010/00100/01000/11111','3':'11110/00001/00001/01110/00001/00001/11110',
'4':'00010/00110/01010/10010/11111/00010/00010','5':'11111/10000/10000/11110/00001/00001/11110',
'6':'01110/10000/10000/11110/10001/10001/01110','7':'11111/00001/00010/00100/01000/01000/01000',
'8':'01110/10001/10001/01110/10001/10001/01110','9':'01110/10001/10001/01111/00001/00001/01110',
':':'0/1/0/0/1/0/0','.':'0/0/0/0/0/1/1',',':'00/00/00/00/01/01/10',
'-':'00000/00000/00000/11111/00000/00000/00000','+':'00000/00100/00100/11111/00100/00100/00000',
'/':'00001/00001/00010/00100/01000/10000/10000','>':'100/010/001/010/100','<':'001/010/100/010/001',
'!':'1/1/1/1/1/0/1','?':'01110/10001/00001/00010/00100/00000/00100',
'(':'01/10/10/10/10/10/01',')':'10/01/01/01/01/01/10','=':'00000/11111/00000/11111',
';':'00/01/00/00/01/01/10','~':'00000/00000/01001/10110/00000/00000/00000','[':'11/10/10/10/10/10/11',']':'11/01/01/01/01/01/11',"'":'1/1',
}
DIG = ['111/101/101/101/111','010/110/010/010/111','110/001/010/100/111','110/001/010/001/110','101/101/111/001/001']

def new(w=320,h=256):
    im=Image.new('P',(w,h),0);im.putpalette(sum((list(c) for c in P),[])+[0]*720);return im

def text(im,x,y,s,c=5):
    assert x >= 0 and y >= 0 and x+len(s)*6-1 <= im.width, (x,y,s)
    assert y+7 <= im.height
    d=ImageDraw.Draw(im)
    for ch in s.upper():
        if ch!=' ':
            for j,row in enumerate(DATA[ch].split('/')):
                for i,v in enumerate(row):
                    if v=='1':d.point((x+i,y+j),c)
        x+=6

def tiny(im,x,y,n,c=5):
    d=ImageDraw.Draw(im)
    for j,row in enumerate(DIG[n].split('/')):
        for i,v in enumerate(row):
            if v=='1':d.point((x+i,y+j),c)

def box(im,b,fill=1,edge=3):ImageDraw.Draw(im).rectangle(b,fill=fill,outline=edge)
def line(im,pts,c=5,w=1):ImageDraw.Draw(im).line(pts,fill=c,width=w)
def arrow(im,x,y,heading='E',c=5):
    pts=[(2,5),(9,5),(9,2),(14,7),(9,12),(9,9),(2,9)]
    turns={'E':0,'S':1,'W':2,'N':3}[heading]
    for _ in range(turns):pts=[(15-b,a) for a,b in pts]
    ImageDraw.Draw(im).polygon([(x+a,y+b) for a,b in pts],fill=c)

def tile(tok):
    im=new(16,16);d=ImageDraw.Draw(im);d.rectangle((0,0,15,15),fill=1)
    if tok=='#':
        box(im,(0,0,15,15),2,3);line(im,[(0,7),(15,7)],1);line(im,[(7,0),(7,6)],1);line(im,[(3,8),(3,15)],1)
    elif tok=='O':
        d.ellipse((1,1,14,14),fill=3);d.ellipse((2,3,13,15),fill=0);line(im,[(3,2),(11,2)],4)
    elif tok.startswith('S'):
        n=int(tok[1]);d.ellipse((2,2,13,13),outline=PC[n]);line(im,[(0,7),(4,7)],PC[n]);line(im,[(11,7),(15,7)],PC[n]);tiny(im,6,5,n,PC[n])
    elif tok.startswith('D'):
        n=int(tok[1]);line(im,[(1,3),(1,13),(14,13),(14,3)],PC[n],2);line(im,[(5,3),(8,6),(11,3)],5);tiny(im,7,8,n)
    elif tok.startswith('G'):
        box(im,(1,2,14,14),3,14);box(im,(3,4,12,10),0,0);line(im,[(0,14),(15,14)],14);line(im,[(4,1),(11,1)],14)
    elif tok.startswith('C'):
        d.rectangle((0,0,15,15),fill=13);direction={'^':'N','>':'E','v':'S','<':'W'}[tok[1]]
        arrow(im,0,0,direction);line(im,[(0,0),(15,0)],3);line(im,[(0,15),(15,15)],3)
    elif tok.startswith('T') or tok.startswith('K'):
        trap=tok.startswith('K');box(im,(2,2,13,12),3,14);box(im,(4,4,11,8),12 if trap else 13,0)
        line(im,[(3,14),(12,14)],14);d.point((10,10),5)
        if trap:
            d.ellipse((4,3,11,10),fill=12,outline=5)
            line(im,[(7,4),(7,8)],0);d.point((7,9),0)
        else:
            line(im,[(5,6),(10,6),(8,4)],5)
    elif tok.startswith('L'):
        box(im,(3,3,12,12),3,14);arrow(im,0,0,{'^':'N','>':'E','v':'S','<':'W'}[tok[1]],12)
    elif tok.startswith('X'):
        for a,b in [(6,0),(6,12),(0,6),(12,6)]:d.rectangle((a,b,a+3,b+3),fill=14)
        d.ellipse((2,2,13,13),fill=3,outline=14);d.ellipse((5,5,10,10),fill=12);d.point((6,5),15)
    elif tok=='A':
        d.ellipse((3,1,12,5),fill=14);box(im,(3,3,12,12),3,14);d.ellipse((3,10,12,14),fill=0,outline=14)
        line(im,[(6,5),(6,9),(9,9)],0,2)
    elif tok.startswith('Y'):
        d.ellipse((1,3,14,15),fill=3,outline=14);box(im,(6,0,9,8),14,5);d.rectangle((3,12,5,13),fill=10);d.rectangle((9,12,11,13),fill=10)
    elif tok.startswith('P'):
        d.polygon([(7,1),(14,7),(7,14),(1,7)],outline=14);d.rectangle((5,5,10,10),outline=14)
    return im

def crate(im,x,y):
    box(im,(x+3,y+3,x+12,y+12),11,15);box(im,(x+4,y+4,x+11,y+11),10,10)
    line(im,[(x+5,y+5),(x+10,y+10)],11);line(im,[(x+10,y+5),(x+5,y+10)],11)

def robot(im,x,y,p,heading='E',carry=False,active=False):
    d=ImageDraw.Draw(im)
    if active:
        for a,b,sx,sy in [(0,0,1,1),(15,0,-1,1),(0,15,1,-1),(15,15,-1,-1)]:
            line(im,[(x+a+3*sx,y+b),(x+a,y+b),(x+a,y+b+3*sy)],5)
    d.rectangle((x+1,y+4,x+3,y+12),fill=0);d.rectangle((x+12,y+4,x+14,y+12),fill=0)
    line(im,[(x+2,y+5),(x+2,y+11)],14);line(im,[(x+13,y+5),(x+13,y+11)],14)
    d.rounded_rectangle((x+4,y+3,x+11,y+12),radius=2,fill=PC[p],outline=0)
    box(im,(x+5,y+5,x+10,y+11),5,5);tiny(im,x+6,y+6,p,0)
    pts={'E':[(12,6),(15,8),(12,10)],'N':[(6,3),(8,0),(10,3)],'S':[(6,13),(8,15),(10,13)],'W':[(3,6),(0,8),(3,10)]}[heading]
    d.polygon([(x+a,y+b) for a,b in pts],fill=PC[p])
    if carry:box(im,(x+11,y+12,x+14,y+15),10,15)

def load_map(name):
    rows = [s.split() for s in (ROOT/'resources/maps'/f'{name}.map').read_text().splitlines() if s.strip() and not s.startswith(';')]
    assert 1 <= len(rows) <= 16 and 1 <= len(rows[0]) <= 20
    assert all(len(row) == len(rows[0]) for row in rows)
    return rows

def board(im,name='training-01',overview=False,scenario=False,step=0):
    rows=load_map(name);w,h=len(rows[0]),len(rows);scale=8 if overview else 16
    ox=4+max(0,(192-w*scale)//2);oy=40+max(0,(160-h*scale)//2)
    # Camera at top-left; clipped cells use the same 16px art, never squash.
    layer=new(w*16,h*16)
    for y,row in enumerate(rows):
        for x,t in enumerate(row):
            layer.paste(tile(t),(x*16,y*16))
    for y,row in enumerate(rows):
        for x,t in enumerate(row):
            if t=='A':
                for dy in (-1,0,1):
                    for dx in (-1,0,1):
                        if (dx or dy) and 0<=x+dx<w and 0<=y+dy<h and rows[y+dy][x+dx]!='#':
                            for i in (3,10):line(layer,[((x+dx)*16+i,(y+dy)*16+13),((x+dx)*16+i+2,(y+dy)*16+13)],4)
    for y,row in enumerate(rows):
        for x,t in enumerate(row):
            if t.startswith('G'):crate(layer,x*16,y*16)
            if t.startswith('S'):
                p=int(t[1]);
                if not scenario or p>2:robot(layer,x*16,y*16,p,'E' if p%2 else 'W',active=p==1)
    if scenario:
        # Legal illustrative push on Foundry row 5; not a recorded match.
        robot(layer,(3+(step==2))*16,5*16,1,'E',active=True)
        robot(layer,(4+(step>=1))*16,5*16,2,'N')
        if step==1:arrow(layer,4*16,5*16,'E',15)
    if overview:
        # Schematic overview: explicit 8px tokens, NOT downsampled detailed art.
        small=new(w*8,h*8);d=ImageDraw.Draw(small)
        for y,row in enumerate(rows):
            for x,t in enumerate(row):
                c=2 if t=='#' else 0 if t=='O' else 13 if t.startswith('C') else 3 if t!='.' else 1
                d.rectangle((x*8,y*8,x*8+7,y*8+7),fill=c)
                if t.startswith('D'):d.rectangle((x*8+1,y*8+1,x*8+6,y*8+6),outline=PC[int(t[1])])
                if t.startswith('G'):d.rectangle((x*8+2,y*8+2,x*8+5,y*8+5),fill=10)
                if t.startswith('S'):
                    p=int(t[1]);box(small,(x*8,y*8,x*8+7,y*8+7),PC[p],PC[p]);tiny(small,x*8+2,y*8+1,p,0)
                if t.startswith(('X','L','Y')):d.point((x*8+3,y*8+3),12)
        im.paste(small,(ox,oy));box(im,(ox,oy,ox+min(w,12)*8-1,oy+min(h,10)*8-1),None,5)
    else:
        im.paste(layer.crop((0,0,min(w,12)*16,min(h,10)*16)),(ox,oy))
    box(im,(3,39,196,200),None,3)
    if h>10 and not overview:
        line(im,[(94,201),(98,203),(102,201)],15)
    if w>12 and not overview:line(im,[(197,115),(199,119),(197,123)],15)

ORDERS=['FORWARD 1','FORWARD 2','BACK 1','TURN LEFT','TURN RIGHT','TURN 180','DROP','TOGGLE','WAIT']
def order_icon(im,x,y,i,c=5):
    if i<3:
        arrow(im,x,y,'S' if i==2 else 'N',c)
        if i==1:line(im,[(x+2,y+12),(x+2,y+4)],c)
    elif i<6:
        if i==4:
            line(im,[(x+3,y+12),(x+3,y+4),(x+12,y+4)],c,2)
            line(im,[(x+9,y+1),(x+12,y+4),(x+9,y+7)],c)
        else:
            line(im,[(x+12,y+12),(x+12,y+4),(x+3,y+4)],c,2)
            if i==3:line(im,[(x+6,y+1),(x+3,y+4),(x+6,y+7)],c)
            if i==5:line(im,[(x+3,y+4),(x+3,y+12),(x+1,y+10)],c)
    elif i==6:
        box(im,(x+4,y+1,x+10,y+7),10,15);line(im,[(x+7,y+8),(x+7,y+12),(x+4,y+10)],c)
    elif i==7:
        line(im,[(x+3,y+12),(x+12,y+12)],c,2);line(im,[(x+7,y+11),(x+11,y+3)],c,2)
    else:
        box(im,(x+4,y+3,x+6,y+12),c,c);box(im,(x+10,y+3,x+12,y+12),c,c)

def header(im,phase='PLAN',round_no=1,live=None):
    box(im,(0,0,319,10),1,1)
    text(im,4,2,'BOTCHED!',15);text(im,62,2,f'YOU P1  R{round_no}/12',6)
    text(im,166,2,phase,5);text(im,226,2,'~43S' if phase=='PLAN' else 'TICK 1',15)
    text(im,276,2,'TO 20',4)
    for p in range(1,5):
        x=4+(p-1)*79;box(im,(x,12,x+75,23),1,PC[p] if p==1 else 2)
        text(im,x+3,14,f'P{p} 0000',PC[p]);
        for k in range(3):box(im,(x+50+k*6,15,x+53+k*6,19),PC[p],PC[p])
    for tick in range(1,4):
        x=4+(tick-1)*105;box(im,(x,26,x+101,37),1,3)
        text(im,x+3,28,f'T{tick}',4)
        start=((round_no-1)*3+tick-1)%4
        for j in range(4):
            p=(start+j)%4+1;xx=x+23+j*15
            if live==(tick,p):box(im,(xx-2,27,xx+8,36),3,5)
            text(im,xx,28,str(p),PC[p])
        text(im,x+86,28,'M',15)

def palette(im,selected=1,locked=False):
    box(im,(200,40,315,199),1,3);text(im,205,43,'CHOOSE ORDER',15)
    for i,name in enumerate(ORDERS):
        y=54+i*16;hot=i==selected
        box(im,(202,y,313,y+14),2 if hot else 1,5 if hot else 2)
        text(im,205,y+4,str(i+1),15 if hot else 4);order_icon(im,213,y,i,4 if locked else 5)
        text(im,231,y+4,name,4 if locked else 5)

def programme(im,playing=False,selected=0):
    for i,(name,cmd) in enumerate([('FWD 2',1),('LEFT',3),('WAIT',8)]):
        x=4+i*73;box(im,(x,204,x+68,227),2 if i==selected else 1,5 if i==selected else 3)
        text(im,x+3,207,str(i+1),15);order_icon(im,x+9,208,cmd);text(im,x+27,213,name)
    box(im,(224,204,315,227),13 if not playing else 1,5 if not playing else 3)
    text(im,231,207,'ENTER: LOCK' if not playing else 'REPLAY',5)
    text(im,231,217,'3 ORDERS' if not playing else 'R1 / T1',15)

def footer(im,lines):
    for i,s in enumerate(lines):text(im,4,231+i*8,s,5 if i==0 else 4)

def planning(name='training-01',overview=False):
    im=new();header(im,round_no=2 if overview else 1);board(im,name,overview);palette(im);programme(im)
    footer(im,['FORWARD 2: UP TO TWO CELLS; STOPS IF BLOCKED.',
        'UP/DN CHOOSE  SPACE SET  TAB SLOT  ENTER LOCK',
        'V MAP  I INSPECT  F1 GUIDE  ESC QUIT'])
    if overview:
        text(im,10,43,'MAP OVERVIEW',15);text(im,10,181,'WHITE FRAME = DETAIL VIEW',4);text(im,10,190,'V RETURN / I INSPECT',4)
    return im

def playback(step=0):
    im=new();header(im,'REPLAY',live=(1,1));board(im,'foundry-01',scenario=True,step=step)
    box(im,(200,40,315,199),1,3);text(im,206,45,'NOW: P1',6)
    bot=new(16,16);robot(bot,0,0,1,'E',active=True);im.paste(bot.resize((32,32),Image.Resampling.NEAREST),(241,57))
    text(im,206,97,'FORWARD 1',5);text(im,206,110,'PLAYER ORDER',15)
    line(im,[(205,124),(310,124)],3);text(im,206,130,'NEXT: P2',7)
    text(im,206,141,'THEN P3, P4',4);text(im,206,158,'M = MACHINERY',15)
    text(im,206,171,'BELTS, HAZARDS',4);text(im,206,185,'P3/P4 BELOW',15)
    programme(im,True)
    # Programme for this illustrative replay is F1, left, wait.
    box(im,(4,204,72,227),2,5);text(im,7,207,'1',15);order_icon(im,13,208,0);text(im,31,213,'FWD 1')
    captions=[['P1 FORWARD 1: P2 IS IN THE NEXT CELL.','WATCH THE WHITE FRAME. ARROW = FACING.'],
              ['P1 PUSHES P2 ONE CELL EAST.','P2 KEEPS ITS NORTH FACING.'],
              ['P1 MOVES INTO THE VACATED CELL.','P1 ORDER COMPLETE. NEXT: P2.']][step]
    footer(im,captions+['REPLAY R1 / T1   H LOG   V MAP   ESC QUIT'])
    return im

def machinery():
    im=new();header(im,'REPLAY');board(im,'foundry-01',scenario=True,step=2)
    # P2 illustrates a server-reported trap hit at (5,4), within X1's ring.
    rows=load_map('foundry-01');ox=12;oy=40
    im.paste(tile('.'),(ox+5*16,oy+5*16))
    robot(im,ox+5*16,oy+4*16,2,'N',active=True)
    for x,y in ((3,2),(4,2),(5,2),(3,3),(5,3),(3,4),(4,4),(5,4)):
        box(im,(ox+x*16,oy+y*16,ox+x*16+15,oy+y*16+15),None,12)
    box(im,(85,27,99,36),3,5);text(im,90,28,'M',15)
    # Two remaining durability pips after one damage.
    box(im,(4+79+62,15,4+79+65,19),0,3)
    box(im,(200,40,315,199),1,3);text(im,206,45,'MACHINERY',15)
    text(im,206,62,'TRAP X1 FIRES',12);im.paste(tile('X1').resize((32,32),Image.Resampling.NEAREST),(240,77))
    text(im,206,117,'P2: 1 DAMAGE',7);text(im,206,130,'2 OF 3 LEFT',5)
    text(im,206,147,'X1 NOW OFF',4);text(im,206,162,'OWNER: NONE',4)
    text(im,206,185,'P3/P4 BELOW',15);programme(im,True)
    footer(im,['NEUTRAL TRAP X1 HITS P2. NO POINTS AWARDED.',
               'TRAP DISARMS. P2 REMAINS IN PLAY.',
               'REPLAY R1 / T1   H LOG   V MAP   ESC QUIT'])
    return im

def guide():
    im=new();text(im,8,6,'FIELD GUIDE / F1 RETURNS TO GAME',15)
    text(im,8,18,'SHAPES FIRST. COLOUR IDENTIFIES PLAYERS.',4)
    entries=[('S1','SPAWN','RETURN POINT'),('D1','DOCK','DELIVER +5'),('G1','DISPENSER','CRATE SOURCE'),
      ('C>A','CONVEYOR','MACHINE MOVE'),('TA','BELT CONSOLE','TOGGLE GROUP'),('O','PIT','DISABLES'),
      ('L>','FIXED LASER','DIRECTIONAL'),('X1','BURST TRAP','8 NEIGHBOURS'),('K1','TRAP CONTROL','TOGGLE/OWN'),
      ('Y1','TURRET','CHARGE 0-2'),('P1','TURRET PAD','STEP TO OWN'),('A','TAR DRUM','SLOWS ORDERS')]
    for n,(tok,name,hint) in enumerate(entries):
        x=8+(n%2)*156;y=34+(n//2)*29
        im.paste(tile(tok),(x,y));text(im,x+22,y,name);text(im,x+22,y+10,hint,4)
    robot(im,8,211,1,'E',True,True);text(im,30,211,'P1: NUMBER + NOSE + CARGO',6)
    text(im,30,221,'3 PIPS = DURABILITY LEFT',4)
    crate(im,8,232);text(im,30,232,'CRATE: GOLD BOX. AUTO PICKUP.',15)
    return im

def save(im,name):
    assert im.size==(320,256)
    assert all(v%17==0 for rgb in P for v in rgb)
    assert len(im.getcolors())<=16
    im.save(HERE/f'{name}.png')
    im.resize((960,768),Image.Resampling.NEAREST).save(HERE/f'{name}-3x.png')

if __name__=='__main__':
    images={'01-planning':planning(),'02-foundry':planning('foundry-01'),
        '03-overview':planning('foundry-01',True),'04-push-before':playback(0),
        '05-push-displacement':playback(1),'06-push-result':playback(2),'07-field-guide':guide(),'08-machinery':machinery()}
    for name,im in images.items():save(im,name)
    maps=sorted((ROOT/'resources/maps').glob('*.map'))
    sheet=new(960,804)
    sizes={}
    for i,p in enumerate(maps):
        im=planning(p.stem,True);sheet.paste(im,((i%3)*320,(i//3)*268));text(sheet,(i%3)*320+4,(i//3)*268+257,p.stem,15)
        rows=load_map(p.stem);sizes[p.stem]=[len(rows[0]),len(rows)]
    sheet.save(HERE/'all-nine-overviews.png')
    # Review contact sheet: two complementary native-resolution screens, 2x.
    contact=new(640,268)
    for i,name in enumerate(('01-planning','05-push-displacement')):
        contact.paste(images[name],(i*320,12));text(contact,i*320+4,2,'PROPOSAL / '+('PLANNING' if i==0 else 'EVENT REPLAY'),15)
    contact.resize((1280,536),Image.Resampling.NEAREST).save(HERE/'review.png')
    (HERE/'constraints.json').write_text(json.dumps({'size':[320,256],'colours':PAL,'bitplanes':4,
        'screen_bitmap_bytes':40960,'two_bitmaps_bytes':81920,'detail_tile':[16,16],
        'viewport_cells':[12,10],'overview_tile':[8,8],'maps':sizes,
        'status':'Design illustrations; not emulator screenshots or measured runtime performance.'},indent=2)+'\n')
    print(f'Validated {len(images)} native mockups, OCS palette, nine real map geometries.')

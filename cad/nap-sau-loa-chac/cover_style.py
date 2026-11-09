# Nap sau cho vo robot TFT. Toa do giong khung truoc (frame.py): z=0 la mat truoc.
import cadquery as cq, math, sys
STYLE=sys.argv[1]; CORNER_R=float(sys.argv[2]); PRE=sys.argv[3]
W,H=104.0,101.0; WALL=3.0
RIM_Z=18.0                 # dinh thanh khung truoc
PCB_DX,PCB_DY=88.6,86.0    # 4 lo PCB (giong frame.py)
PCB_TOP=16.0+4.0           # mat tren cot PCB (PLATE 4 + cot 16)
PCB_BACK=PCB_TOP+1.6       # mat sau PCB
PLATE_T=3.0
PLATE_IN=44.5              # mat trong nap: pin 18650 (de cao ~21 mm) + ho ~2 mm
Z_BACK=PLATE_IN+PLATE_T    # mat ngoai nap
# vit giau: M3 x 10 (xuyen day lo 2.5 + PCB 1.6 + an vao cot ~6)
BOSS_D=8.4; CB_D=6.2; SCREW_D=3.4; FLOOR=2.5
# o pin (theo anh PCB: de pin ben phai, nhin tu sau)
WX,WY=37.05,0.5; WIN_W,WIN_L=21.0,70.0; LEDGE=1.5; DOOR_T=1.5; GAP=0.2
# rang cong sac (giu nhu hop cu)
USB_W=14.0; USB_D=4.0     # bang khung truoc: cong sac nam giua mep ghep
LIP_T=1.2; LIP_H=2.0; LIP_CLR=0.25

innerW,innerH=W-2*WALL,H-2*WALL
R_IN=max(CORNER_R-WALL,0)
def rr(w,h,z,t,r):
    b=cq.Workplane("XY").workplane(offset=z).rect(w,h).extrude(t)
    return b.edges("|Z").fillet(r) if r>0 else b
cov=rr(W,H,RIM_Z,Z_BACK-RIM_Z,CORNER_R).faces(">Z").edges().chamfer(1.0)
cov=cov.cut(rr(innerW,innerH,RIM_Z,PLATE_IN-RIM_Z,R_IN))
# go dinh vi lot vao trong khung truoc
lipO=(innerW-2*LIP_CLR,innerH-2*LIP_CLR)
lip=rr(lipO[0],lipO[1],RIM_Z-LIP_H,LIP_H,max(R_IN-LIP_CLR,0)).cut(rr(lipO[0]-2*LIP_T,lipO[1]-2*LIP_T,RIM_Z-LIP_H,LIP_H,max(R_IN-LIP_CLR-LIP_T,0)))
lip=lip.cut(cq.Workplane("XY").workplane(offset=RIM_Z-LIP_H).center(0,-H/2).rect(USB_W+2,20).extrude(LIP_H))
cov=cov.union(lip)
# rang cong sac o mep -y
cov=cov.cut(cq.Workplane("XY").workplane(offset=RIM_Z).center(0,-(H/2-WALL/2)).rect(USB_W,WALL+1).extrude(USB_D))
# lo tron choc nut PWR: duoi de pin, phia cong sac, sat lo vit goc (theo anh PCB, sai so ~1 mm)
PWR_X,PWR_Y,PWR_D=36.39,-41.81,3.5   # cach tam vit goc 8 mm (nguoi dung do)
cov=cov.cut(cq.Workplane("XY").workplane(offset=PLATE_IN-0.1).center(PWR_X,PWR_Y).circle(PWR_D/2).extrude(PLATE_T+0.2))
# 4 tru vit giau
pts=[(sx*PCB_DX/2,sy*PCB_DY/2) for sx in(-1,1) for sy in(-1,1)]
boss=cq.Workplane("XY").workplane(offset=PCB_BACK).pushPoints(pts).circle(BOSS_D/2).extrude(PLATE_IN-PCB_BACK+0.01)
cov=cov.union(boss)
cov=cov.cut(cq.Workplane("XY").workplane(offset=PCB_BACK+FLOOR).pushPoints(pts).circle(CB_D/2).extrude(Z_BACK))
cov=cov.cut(cq.Workplane("XY").workplane(offset=PCB_BACK-0.1).pushPoints(pts).circle(SCREW_D/2).extrude(FLOOR+0.2))
# o pin: cua so + hoc nap
cov=cov.cut(cq.Workplane("XY").workplane(offset=PLATE_IN-0.1).center(WX,WY).rect(WIN_W,WIN_L).extrude(PLATE_T+0.2))
PK_W,PK_L=WIN_W+2*LEDGE+2*GAP,WIN_L+2*LEDGE+2*GAP
cov=cov.cut(cq.Workplane("XY").workplane(offset=Z_BACK-DOOR_T).center(WX,WY).rect(PK_W,PK_L).extrude(DOOR_T+0.1))
# ranh khoa (2 ranh nho tren thanh hoc phia -x) + khe mong tay
def ybar(x,y0,z,r,L):   # thanh tron nam doc truc y, tu y0 den y0+L
    return cq.Workplane("XY").add(cq.Solid.makeCylinder(r,L,cq.Vector(x,y0,z),cq.Vector(0,1,0)))
for y in (-20,20):
    cov=cov.cut(ybar(WX-PK_W/2,WY+y-3.5,Z_BACK-DOOR_T/2,0.55,7))
cov=cov.cut(cq.Workplane("XY").workplane(offset=Z_BACK-DOOR_T).center(WX-PK_W/2-0.6,WY).rect(1.6,10).extrude(DOOR_T+0.1))

# ===== loa 24 mm (vanh O23.7, vanh day ~2.4, sau 10.4) : luoi loa + ngam kep =====
import math
SPK_X,SPK_Y=-2.0,-23.0          # vung trong: duoi ESP32, giua, tranh mach MAX98357 va de pin
SPK_D=24.2; SPK_RIM=2.0; RT=1.4; TAB_H=4.6; TAB_ANG=40; LIP=0.8; SLIT=0.8   # v2: vanh loa that 2 mm
Ri=SPK_D/2; Ro=Ri+RT
clip=cq.Workplane("XY").circle(Ro).circle(Ri).extrude(SPK_RIM+0.1)
# v2: 4 gan ep giua cac ngam -> vanh loa khong lac ngang
for a_ in (45,135,225,315):
    rb=cq.Workplane("XZ").polyline([(Ri+0.05,0.0),(Ri-0.45,0.0),(Ri-0.45,SPK_RIM-0.6),(Ri+0.05,SPK_RIM+0.1)]).close().extrude(0.5,both=True)
    clip=clip.union(rb.rotate((0,0,0),(0,0,1),a_))
clip=clip.cut(cq.Workplane("XY").workplane(offset=0.8).rect(2*Ro+1,6.0).extrude(TAB_H))   # 2 khe day loa (+-x)
for ang in (90,270):
    a0=math.radians(TAB_ANG/2)
    tab=(cq.Workplane("XY").circle(Ro).circle(Ri).extrude(TAB_H)
         .intersect(cq.Workplane("XY").polyline([(0,0),(30*math.cos(a0),30*math.sin(a0)),(30*math.cos(a0),-30*math.sin(a0))]).close().extrude(TAB_H)))
    z0=SPK_RIM
    lip=(cq.Workplane("XZ").polyline([(Ri-LIP,z0),(Ri+0.2,z0),(Ri+0.2,z0+LIP+0.9),(Ri-LIP,z0+0.3)]).close()
         .revolve(TAB_ANG,(0,0,0),(0,1,0)).rotate((0,0,0),(0,0,1),-TAB_ANG/2))
    clip=clip.union(tab.union(lip).rotate((0,0,0),(0,0,1),ang))
    for sgn in (-1,1):
        aa=math.radians(ang+sgn*(TAB_ANG/2+2))
        clip=clip.cut(cq.Workplane("XY").workplane(offset=0.8).rect(RT+1.0,SLIT).extrude(TAB_H)
             .rotate((0,0,0),(0,0,1),math.degrees(aa)).translate(((Ri+RT/2)*math.cos(aa),(Ri+RT/2)*math.sin(aa),0)))
# kep huong xuong duoi (vao trong hop), gan vao mat trong nap
clip=clip.mirror("XY").translate((SPK_X,SPK_Y,PLATE_IN))
cov=cov.union(clip)
# luoi loa: lo O1.8 xep luc giac trong vong O19
holes=[]
P=3.0
for j in range(-4,5):
    for i in range(-4,5):
        x=i*P+(j%2)*P/2; y=j*P*math.sqrt(3)/2
        if math.hypot(x,y)<=9.5-0.9: holes.append((SPK_X+x,SPK_Y+y))
cov=cov.cut(cq.Workplane("XY").workplane(offset=PLATE_IN-0.1).pushPoints(holes).circle(0.9).extrude(PLATE_T+0.2))
print('grille holes',len(holes))

# ===== nap pin =====
DW,DL=PK_W-2*GAP,PK_L-2*GAP
door=cq.Workplane("XY").workplane(offset=Z_BACK-DOOR_T).center(WX,WY).rect(DW,DL).extrude(DOOR_T)
# 3 moc ben +x luon duoi mep cua so
xe=WX+WIN_W/2
for y in (-25,0,25):
    v=cq.Workplane("XY").workplane(offset=PLATE_IN-1.3).center(xe-GAP-0.6,WY+y).rect(1.2,8).extrude(Z_BACK-DOOR_T-(PLATE_IN-1.3))
    h=cq.Workplane("XY").workplane(offset=PLATE_IN-1.3).center(xe-GAP+0.7,WY+y).rect(1.4,8).extrude(1.0)
    door=door.union(v).union(h)
# 2 gu bam ben -x (an vao ranh khoa)
for y in (-20,20):
    door=door.union(ybar(WX-DW/2,WY+y-3,Z_BACK-DOOR_T/2,0.5,6))

# ===== trang tri mat sau: khac chim G mm (in up mat ngoai xuong ban) =====
G=0.6; ZD=Z_BACK-G
FONT="/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf"
FONT_T="/usr/share/fonts/opentype/inter/Inter-ExtraBold.otf"
def rect(w,h,u,v,ang=0):
    b=cq.Workplane("XY").workplane(offset=ZD).center(u,v).rect(w,h).extrude(G+0.1)
    return b.rotate((u,v,0),(u,v,1),ang) if ang else b
def ring(u,v,r0,r1): return cq.Workplane("XY").workplane(offset=ZD).center(u,v).circle(r1).circle(r0).extrude(G+0.1)
def dot(u,v,r): return cq.Workplane("XY").workplane(offset=ZD).center(u,v).circle(r).extrude(G+0.1)
def text(t,u,v,s,font,ang=0):
    x=cq.Workplane("XY").workplane(offset=ZD).center(u,v).text(t,s,G+0.1,combine=False,fontPath=font,halign="center",valign="center")
    return x.rotate((u,v,0),(u,v,1),ang) if ang else x
def poly(pts): return cq.Workplane("XY").workplane(offset=ZD).polyline(pts).close().extrude(G+0.1)
def hexo(u,v,d,w): return cq.Workplane("XY").workplane(offset=ZD).center(u,v).polygon(6,d).polygon(6,d-2*w).extrude(G+0.1)
def line(pts,w=0.9,pads=True):
    out=None
    for (u0,v0),(u1,v1) in zip(pts[:-1],pts[1:]):
        L=math.hypot(u1-u0,v1-v0); a=math.degrees(math.atan2(v1-v0,u1-u0))
        sg=rect(L,w,(u0+u1)/2,(v0+v1)/2,a); out=sg if out is None else out.union(sg)
    for u,v in pts: out=out.union(dot(u,v,w/2))
    if pads:
        for u,v in (pts[0],pts[-1]): out=out.union(ring(u,v,0.5,1.3))
    return out
def arc(u,v,r0,r1,a0,a1):
    n=12; pp=[(u,v)]+[(u+40*math.cos(math.radians(a0+(a1-a0)*k/n)),v+40*math.sin(math.radians(a0+(a1-a0)*k/n))) for k in range(n+1)]
    return ring(u,v,r0,r1).intersect(poly(pp))
SX,SY=-2.0,-23.0       # luoi loa
D=[]; DD=[]            # D: nap sau, DD: nap pin
if STYLE=="ai":
    CU,CV,CS=-14.0,24.0,16.0
    chip=cq.Workplane("XY").workplane(offset=ZD).center(CU,CV).rect(CS,CS).extrude(G+0.1).edges("|Z").fillet(1.4)
    chip=chip.cut(text("AI",CU,CV,9.5,FONT_T)).cut(dot(CU-CS/2+2,CV+CS/2-2,0.8))
    D.append(chip)
    for k in range(6):
        o=-5+k*2.0
        for (u,v,w,h) in ((CU+o,CV+CS/2+1.3,0.9,1.6),(CU+o,CV-CS/2-1.3,0.9,1.6),(CU-CS/2-1.3,CV+o,1.6,0.9),(CU+CS/2+1.3,CV+o,1.6,0.9)):
            D.append(rect(w,h,u,v))
    # chip -> canh tren
    for i,o in enumerate((-5,-2,1,4)):
        D.append(line([(CU+o,CV+CS/2+2.3),(CU+o,CV+CS/2+5+i*0),(CU+o-(3 if o<0 else -3),CV+CS/2+8),(CU+o-(3 if o<0 else -3),44)] if False else [(CU+o,CV+CS/2+2.3),(CU+o,44)]))
    # chip -> trai
    for i,o in enumerate((4,1,-2,-5)):
        D.append(line([(CU-CS/2-2.3,CV+o),(-30-i*0,CV+o),(-33-1.6*i,CV+o-3),(-33-1.6*i,-8-4*i)]))
    # chip -> phai -> o pin
    for i,o in enumerate((5,1,-3)):
        D.append(line([(CU+CS/2+2.3,CV+o),(14+2*i,CV+o),(18+2*i,CV+o-4),(18+2*i,-6+6*i)]))
    # chip -> xuong loa
    for o in (-3,1,5):
        D.append(line([(CU+o,CV-CS/2-2.3),(CU+o,6),(SX-2+o*0.6,-9.5)],pads=False))
    D.append(ring(SX,SY,11.0,11.9)); D.append(ring(SX,SY,13.2,13.8))
    # vong pad quanh 4 lo vit
    for x,y in pts: D.append(ring(x,y,CB_D/2+0.9,CB_D/2+1.7))
    # via
    for i in range(6):
        for j in range(3): D.append(dot(-36+i*2.6,-30-j*2.6,0.6))
    # chu
    D.append(text("Bon DIY",-12,-42.5,5.6,FONT_T))
    D.append(text("XIAOZHI AI VOICE ROBOT",-12,-47.2,1.9,FONT))
    D.append(line([(12,-41),(19,-41),(22,-44),(31.5,-44)]))
    # nap pin
    DD.append(cq.Workplane("XY").workplane(offset=ZD).center(WX,WY+6).rect(9.0,22.0).rect(7.0,20.0).extrude(G+0.1))
    DD.append(rect(4.0,1.6,WX,WY+18.2))
    for k in range(3): DD.append(rect(5.4,4.6,WX,WY-0.6+k*6.2))
    DD.append(text("18650",WX,WY-14,3.0,FONT,90))
    DD.append(rect(0.9,14,WX-7,WY-14)); DD.append(rect(0.9,14,WX+7,WY-14))
    DD.append(ring(WX-7,WY-21.6,0.5,1.3)); DD.append(ring(WX+7,WY-21.6,0.5,1.3)); DD.append(ring(WX-7,WY-6.4,0.5,1.3)); DD.append(ring(WX+7,WY-6.4,0.5,1.3))
else:
    CU,CV=-14.0,24.0
    D.append(hexo(CU,CV,20,1.2)); D.append(hexo(CU,CV,13,1.2)); D.append(cq.Workplane("XY").workplane(offset=ZD).center(CU,CV).polygon(6,5.0).extrude(G+0.1))
    for k in range(6):
        a=k*60+30; D.append(rect(2.4,0.9,CU+8.0*math.cos(math.radians(a)),CV+8.0*math.sin(math.radians(a)),a))
    for k in range(6):
        D.append(poly([(-1+3.4*k,CV+4),(0.8+3.4*k,CV+4),(-0.8+3.4*k,CV-4),(-2.6+3.4*k,CV-4)]))
        D.append(poly([(-27-3.4*k,CV+4),(-28.8-3.4*k,CV+4),(-27.2-3.4*k,CV-4),(-25.4-3.4*k,CV-4)]))
    # ngam loa
    for a0 in (15,105,195,285): D.append(arc(SX,SY,11.0,11.9,a0,a0+60))
    for a in (0,90,180,270): D.append(rect(3.2,0.9,SX+13.6*math.cos(math.radians(a)),SY+13.6*math.sin(math.radians(a)),a))
    # ngam 4 lo vit
    for x,y in pts:
        for a0 in (10,100,190,280): D.append(arc(x,y,CB_D/2+1.0,CB_D/2+1.8,a0,a0+70))
    # khung HUD lon
    for su in (-1,1):
        for sv in (-1,1):
            cu=-14+su*26.0; cv=24+sv*15.5
            D.append(rect(8.0,1.2,cu-su*3.4,cv)); D.append(rect(1.2,8.0,cu,cv-sv*3.4))
    # to ong mo dan (giua trai)
    HR=2.6
    for i in range(8):
        for j in range(10):
            u=-47+i*HR*1.5; v=-34+j*HR*math.sqrt(3)+(i%2)*HR*math.sqrt(3)/2
            if not(-48<=u<=-30 and -35<=v<=-6) or math.hypot(u+44.3,v+43)<8.5: continue
            if (u+48)>18-(v+35)*0.5: continue
            D.append(hexo(u,v,2*HR-0.3,0.6))
    # chu
    D.append(text("Bon DIY",-12,-42.5,5.6,FONT_T))
    D.append(text("// AI VOICE CORE",-12,-47.2,2.0,FONT))
    D.append(rect(40,0.9,-12,-39.0))
    # nap pin: bieu tuong pin + mui ten
    DD.append(cq.Workplane("XY").workplane(offset=ZD).center(WX,WY+16).rect(9.0,20.0).rect(7.0,18.0).extrude(G+0.1))
    DD.append(rect(4.0,1.6,WX,WY+27.2))
    for k in range(3): DD.append(rect(5.4,4.0,WX,WY+10.4+k*5.6))
    for k in range(5):
        vv=WY+0-k*6.5
        DD.append(poly([(WX-4,vv),(WX,vv-3.4),(WX+4,vv),(WX+4,vv-1.4),(WX,vv-4.8),(WX-4,vv-1.4)]))
def merge(L):
    m=None
    for d in L:
        if d.solids().size()==0: continue
        m=d if m is None else m.union(d)
    return m
dec=merge(D)
# khong khac vao o nap pin, lo loa, lo PWR
dec=dec.cut(cq.Workplane("XY").workplane(offset=ZD-1).center(WX,WY).rect(PK_W+2,PK_L+2).extrude(3))
cov=cov.cut(dec)
door=door.cut(merge(DD))
# ===== nut che vit =====
plug=(cq.Workplane("XY").circle(CB_D/2-0.1).extrude(4.0).faces(">Z").chamfer(0.4))

for o,n in((cov,PRE+'nap-sau'),(door,PRE+'nap-pin')):
    cq.exporters.export(o,n+'.step')
# huong in: nap sau & nap pin up mat ngoai xuong ban
cq.exporters.export(cov.rotate((0,0,0),(1,0,0),180).translate((0,0,Z_BACK)),PRE+'nap-sau.stl',tolerance=0.02,angularTolerance=0.1)
cq.exporters.export(door.rotate((0,0,0),(1,0,0),180).translate((0,0,Z_BACK)),PRE+'nap-pin.stl',tolerance=0.02,angularTolerance=0.1)
for o,n in((cov,'cov'),(door,'door')):
    b=o.val().BoundingBox(); print(n,o.val().isValid(),round(b.xmin,2),round(b.xmax,2),round(b.ymin,2),round(b.ymax,2),round(b.zmin,2),round(b.zmax,2))
print('interference door/cover vol', round(door.val().intersect(cov.val()).Volume(),3))

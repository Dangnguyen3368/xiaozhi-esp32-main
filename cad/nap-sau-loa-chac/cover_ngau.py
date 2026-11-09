# Nap sau cho vo robot TFT. Toa do giong khung truoc (frame.py): z=0 la mat truoc.
import cadquery as cq
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
R_OUT,R_IN=7.0,4.0
cov=(cq.Workplane("XY").workplane(offset=RIM_Z).rect(W,H).extrude(Z_BACK-RIM_Z).edges("|Z").fillet(R_OUT)
     .faces(">Z").edges().chamfer(1.5))
cov=cov.cut(cq.Workplane("XY").workplane(offset=RIM_Z).rect(innerW,innerH).extrude(PLATE_IN-RIM_Z).edges("|Z").fillet(R_IN))
lipO=(innerW-2*LIP_CLR,innerH-2*LIP_CLR)
lip=(cq.Workplane("XY").workplane(offset=RIM_Z-LIP_H).rect(*lipO).extrude(LIP_H).edges("|Z").fillet(R_IN-LIP_CLR)
     .cut(cq.Workplane("XY").workplane(offset=RIM_Z-LIP_H).rect(lipO[0]-2*LIP_T,lipO[1]-2*LIP_T).extrude(LIP_H).edges("|Z").fillet(R_IN-LIP_CLR-LIP_T)))
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
# ===== trang tri (ban ngau) =====
G=0.6
# ranh chay vong quanh than, tiep noi ranh cua khung truoc
outer=cq.Workplane("XY").workplane(offset=RIM_Z+8.0).rect(W,H).extrude(1.0).edges("|Z").fillet(R_OUT)
inner=cq.Workplane("XY").workplane(offset=RIM_Z+8.0).rect(W-2*G,H-2*G).extrude(1.0).edges("|Z").fillet(R_OUT-G)
cov=cov.cut(outer.cut(inner))
# chu khac mat sau
txt=(cq.Workplane("XY").workplane(offset=Z_BACK-G).center(-12,28)
     .text("XIAOZHI",9,G+0.1,combine=False,fontPath="/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",halign="center",valign="center"))
cov=cov.cut(txt)
cov=cov.cut(cq.Workplane("XY").workplane(offset=Z_BACK-G).center(-12,19.5)
     .text("DIY ROBOT",4.5,G+0.1,combine=False,fontPath="/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",halign="center",valign="center"))
# vong ranh quanh luoi loa
cov=cov.cut(cq.Workplane("XY").workplane(offset=Z_BACK-G).center(SPK_X,SPK_Y).circle(11.6).circle(10.8).extrude(G+0.1))
# vien bat giac quanh nap pin
def octo(w,h,c):
    return [(-w/2+c,-h/2),(w/2-c,-h/2),(w/2,-h/2+c),(w/2,h/2-c),(w/2-c,h/2),(-w/2+c,h/2),(-w/2,h/2-c),(-w/2,-h/2+c)]
cov=cov.cut(cq.Workplane("XY").workplane(offset=Z_BACK-G).center(WX-1.5,WY).polyline(octo(PK_W+5,PK_L+5,4)).close().extrude(G+0.1)
            .cut(cq.Workplane("XY").workplane(offset=Z_BACK-G).center(WX-1.5,WY).polyline(octo(PK_W+3,PK_L+3,3.6)).close().extrude(G+0.1)))
# nap pin: 3 vach nham cho ngon tay
for i in (-1,0,1):
    door=door.cut(cq.Workplane("XY").workplane(offset=Z_BACK-0.5).center(WX-4,WY+i*3).rect(10,1.0).extrude(0.6))
# ===== nut che vit =====
plug=(cq.Workplane("XY").circle(CB_D/2-0.1).extrude(4.0).faces(">Z").chamfer(0.4))

for o,n in((cov,'nap-sau-ngau'),(door,'nap-pin-ngau'),(plug,'nut-che-vit-ngau')):
    cq.exporters.export(o,n+'.step')
# huong in: nap sau & nap pin up mat ngoai xuong ban
cq.exporters.export(cov.rotate((0,0,0),(1,0,0),180).translate((0,0,Z_BACK)),'nap-sau-ngau.stl',tolerance=0.02,angularTolerance=0.1)
cq.exporters.export(door.rotate((0,0,0),(1,0,0),180).translate((0,0,Z_BACK)),'nap-pin-ngau.stl',tolerance=0.02,angularTolerance=0.1)
cq.exporters.export(plug,'nut-che-vit-ngau.stl',tolerance=0.02,angularTolerance=0.1)
for o,n in((cov,'cov'),(door,'door')):
    b=o.val().BoundingBox(); print(n,o.val().isValid(),round(b.xmin,2),round(b.xmax,2),round(b.ymin,2),round(b.ymax,2),round(b.zmin,2),round(b.zmax,2))
print('interference door/cover vol', round(door.val().intersect(cov.val()).Volume(),3))

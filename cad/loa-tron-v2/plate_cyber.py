# Tam sau "DIY" (dung lai bo cuc thiet ke cua ban), toa do giong khung truoc. z=0 la mat duoi tam.
import cadquery as cq, math, sys
CORNER_R=float(sys.argv[1]) if len(sys.argv)>1 else 0.0   # 0 = goc vuong (khung thuong), 7 = bo goc (khung ngau)
NAME=sys.argv[2] if len(sys.argv)>2 else 'tam-cyber'
W,H,T=104.0,101.0,2.0
PCB_DX,PCB_DY=88.6,86.0
SPACER=6.5                  # tu mat sau PCB den mat duoi tam
RIM_W,RIM_H=2.0,1.0         # gờ vien
LBL=0.6                     # chu noi (doi mau o lop nay)
FONT="/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf"
FONT_T="/usr/share/fonts/opentype/inter/Inter-ExtraBold.otf"

def box(w,h,x=0,y=0,z=0,t=T,r=0):
    s=cq.Workplane("XY").workplane(offset=z).center(x,y).rect(w,h).extrude(t)
    return s.edges("|Z").fillet(r) if r>0 else s
def rx(x0,x1,y0,y1,z=-0.1,t=T+0.2): return box(x1-x0,y1-y0,(x0+x1)/2,(y0+y1)/2,z,t)

p=box(W,H,r=CORNER_R)
p=p.faces(">Z").edges().chamfer(0.6) if CORNER_R==0 else p.faces(">Z").edges().chamfer(0.8)
# vien noi quanh mep
p=p.union(box(W,H,z=T,t=RIM_H,r=CORNER_R).cut(box(W-2*RIM_W,H-2*RIM_W,z=T,t=RIM_H,r=max(CORNER_R-RIM_W,0))))
# --- cac lo theo thiet ke cua ban ---
cuts=[]
cuts.append(cq.Workplane("XY").workplane(offset=-0.1).center(-32.53,34.65).circle(7.0).extrude(T+0.2))       # mic O14
cuts.append(rx(26,48,-38.5,40.0))                                       # o pin (dinh ha 0.5 de tranh tru vit)
cuts.append(rx(32,39,-46,-38.4))                                        # khe nut PWR
cuts.append(rx(-42,16,21.5,25.5)); cuts.append(rx(-42,16,-4.5,-0.5))    # 2 khe chan ESP32
cuts.append(rx(-39.11,-26.11,2.54,18.54))                               # lo duoi ESP32
cuts.append(rx(-30.07,-23.07,-16.44,-8.5))                              # nut BOOT
cuts.append(rx(-8,5,-11.5,-7))                                          # chan OLED
# loa tron 24 mm: mat loa huong ra ngoai, lung ngoi tren tam; lo giua O10 cho day loa + thoat hoi
SPK_X,SPK_Y=-35.2,-30.7
cuts.append(cq.Workplane("XY").workplane(offset=-0.1).center(SPK_X,SPK_Y).circle(5.0).extrude(T+0.2))
# luoi "via" thoat nhiet (lo tron nho xep luoi, nhu via tren PCB)
via=[(-19+i*2.6,29.2+j*2.6) for i in range(9) for j in range(3)]
# khe thoat nhiet xe cheo (kieu hut gio)
for i in range(7):
    cx=-17+3.0*i
    cuts.append(cq.Workplane("XY").workplane(offset=-0.1).center(cx,31.5).slot2D(8.0,1.6,60).extrude(T+0.2))
for c in cuts: p=p.cut(c)
# 4 lo vit goc: M3 dau bang (loe 90 do), dung khoang cach PCB that
pts=[(sx*PCB_DX/2,sy*PCB_DY/2) for sx in(-1,1) for sy in(-1,1)]
p=p.cut(cq.Workplane("XY").workplane(offset=-0.1).pushPoints(pts).circle(1.7).extrude(T+0.2))
p=p.cut(cq.Workplane("XY").add([cq.Solid.makeCone(1.7,3.3,1.6,cq.Vector(x,y,T-1.6+0.001)) for x,y in pts]))
# --- phan noi tren mat (giu nhu ban cua ban) ---
p=p.union(cq.Workplane("XY").workplane(offset=T).center(-32.53,34.65).circle(8).circle(7).extrude(2.0))     # vong mic
# --- coc kep loa tron 24 mm (vanh O23.7, sau 10.4) ---
SPK_D=24.2; RT=1.4; CUP_H=10.4; TAB_Z=4.0; TAB_ANG=40; LIP=0.8; SLIT=0.8; SPK_DEPTH=10.4
Ri=SPK_D/2; Ro=Ri+RT
cup=cq.Workplane("XY").circle(Ro).circle(Ri).extrude(CUP_H)
# v2: 6 gan ep doc thanh trong (an vao vanh loa O23.7, ep 0.2 mm moi ben) -> loa khong lac ngang
RIB=0.45
for a in (30,90,150,210,270,330):
    rb=(cq.Workplane("XY").workplane(offset=0.6).center(Ri-RIB/2+0.05,0).rect(RIB+0.1,1.0).extrude(CUP_H-0.6-1.2)
        .faces(">Z").edges("|Y").chamfer(0.4) if False else
        cq.Workplane("XZ").polyline([(Ri+0.05,0.6),(Ri-RIB,0.6),(Ri-RIB,CUP_H-1.6),(Ri+0.05,CUP_H-0.4)]).close().extrude(0.5,both=True))
    cup=cup.union(rb.rotate((0,0,0),(0,0,1),a))
for ang in (0,180):
    a0=math.radians(TAB_ANG/2)
    lip=(cq.Workplane("XZ").polyline([(Ri-LIP,CUP_H),(Ri+0.2,CUP_H),(Ri+0.2,CUP_H+1.2),(Ri-0.1,CUP_H+1.2)]).close()
         .revolve(TAB_ANG,(0,0,0),(0,1,0)).rotate((0,0,0),(0,0,1),-TAB_ANG/2))
    cup=cup.union(lip.rotate((0,0,0),(0,0,1),ang))
    for sgn in (-1,1):
        aa=math.radians(ang+sgn*(TAB_ANG/2+2))
        cup=cup.cut(cq.Workplane("XY").workplane(offset=TAB_Z).rect(RT+1.0,SLIT).extrude(CUP_H)
             .rotate((0,0,0),(0,0,1),math.degrees(aa)).translate(((Ri+RT/2)*math.cos(aa),(Ri+RT/2)*math.sin(aa),0)))
cup=cup.translate((SPK_X,SPK_Y,T))
# khoet khe tren thanh coc cho dau vit goc duoi trai (van van vit duoc khi da gan coc)
SCR=(-PCB_DX/2,-PCB_DY/2)
cup=cup.cut(cq.Workplane("XY").center(*SCR).circle(3.6).extrude(30))
p=p.union(cup)
OLED_PAD=rx(-21,17,-42.5,-7.5,z=T,t=1.0).edges("|Z").fillet(2.0).faces(">Z").edges().chamfer(0.4).cut(rx(-8,5,-11.5,-7,z=T,t=1.2))
p=p.union(OLED_PAD)                                  # de OLED
oled=[(-16.73,-38.99),(12.87,-38.99),(-16.73,-10.98),(12.87,-10.98)]
p=p.union(cq.Workplane("XY").workplane(offset=T).pushPoints(oled).circle(2.0).extrude(4.0))
p=p.cut(cq.Workplane("XY").workplane(offset=T).pushPoints(oled).circle(1.0).extrude(4.1))
OLED_ZONE=OLED_PAD.union(cq.Workplane("XY").workplane(offset=T).pushPoints(oled).circle(2.0).extrude(4.0))                   # cot M2 cho OLED
# --- chu noi: logo + nhan ---
def label(txt,x,y,size,rot=0,font=FONT):
    t=cq.Workplane("XY").workplane(offset=T).center(x,y).text(txt,size,LBL,combine=False,fontPath=font,halign="center",valign="center")
    return t.rotate((x,y,0),(x,y,1),rot) if rot else t
for txt,x,y,sz,r in [("PWR",28.6,-44.0,2.4,0),("BOOT",-34.6,-12.5,2.4,0),
                     ("OLED",-12.0,-44.4,2.2,0),("USB-C",6.0,-46.4,2.2,0),("SPK",-31.6,-47.0,2.4,0)]:
    p=p.union(label(txt,x,y,sz,r))



# ================= PHONG CACH CYBER HUD =================
def zbox(w,h,x,y,ang=0,z=T,t=LBL):
    b=cq.Workplane("XY").workplane(offset=z).center(x,y).rect(w,h).extrude(t)
    return b.rotate((x,y,0),(x,y,1),ang) if ang else b
def arc(cx,cy,r0,r1,a0,a1,z=T,t=LBL):
    ring=cq.Workplane("XY").workplane(offset=z).center(cx,cy).circle(r1).circle(r0).extrude(t)
    n=12; pts=[(cx,cy)]+[(cx+40*math.cos(math.radians(a0+(a1-a0)*k/n)),cy+40*math.sin(math.radians(a0+(a1-a0)*k/n))) for k in range(n+1)]
    return ring.intersect(cq.Workplane("XY").workplane(offset=z).polyline(pts).close().extrude(t))
deco=[]
# tieu de
deco.append(cq.Workplane("XY").workplane(offset=T).center(-8.5,42.6).text("Bon DIY",5.2,LBL,combine=False,fontPath=FONT_T,halign="center",valign="center"))
deco.append(cq.Workplane("XY").workplane(offset=T).center(-8.5,38.0).text("// AI VOICE CORE",1.9,LBL,combine=False,fontPath=FONT,halign="center",valign="center"))
deco.append(cq.Workplane("XY").workplane(offset=T).polyline([(-22.5,36.2),(5.0,36.2),(6.2,35.4),(-22.5,35.4)]).close().extrude(LBL))   # gach chan vat
# loi nang luong luc giac (thay chip AI)
CX,CY=14.0,37.0
deco.append(cq.Workplane("XY").workplane(offset=T).center(CX,CY).polygon(6,13.0).polygon(6,11.4).extrude(LBL))
deco.append(cq.Workplane("XY").workplane(offset=T).center(CX,CY).polygon(6,7.0).polygon(6,5.4).extrude(LBL))
deco.append(cq.Workplane("XY").workplane(offset=T).center(CX,CY).polygon(6,2.6).extrude(LBL))
for k in range(6):
    a=k*60+30; r=4.6
    deco.append(zbox(1.6,0.7,CX+r*math.cos(math.radians(a)),CY+r*math.sin(math.radians(a)),a))
# vong ngam quanh mic: 4 vach chu thap
MX,MY=-32.53,34.65
for a in (0,90,180,270):
    deco.append(zbox(2.6,0.8,MX+10.3*math.cos(math.radians(a)),MY+10.3*math.sin(math.radians(a)),a))
deco.append(arc(MX,MY,9.0,9.6,20,70)); deco.append(arc(MX,MY,9.0,9.6,110,160)); deco.append(arc(MX,MY,9.0,9.6,200,250)); deco.append(arc(MX,MY,9.0,9.6,290,340))
# vong ngam quanh 4 vit goc
for (x,y) in pts:
    for a0 in (10,100,190,280):
        g=arc(x,y,4.4,5.1,a0,a0+70)
        deco.append(g.cut(cq.Workplane("XY").center(SPK_X,SPK_Y).circle(Ro+0.4).extrude(20)))
# khung HUD tren de OLED (noi tren mat de)
ZP=T+1.0
for sx,sy in ((-1,-1),(1,-1),(-1,1),(1,1)):
    xc=-2.0+sx*17.8; yc=-25.0+sy*16.6
    deco.append(zbox(5.0,0.8,xc-sx*2.1,yc,0,z=ZP)); deco.append(zbox(0.8,5.0,xc,yc-sy*2.1,0,z=ZP))
# to ong mo dan giua 2 khe ESP32
HR=2.5
for j in range(-1,9):
    for i in range(-1,16):
        x=-23+i*HR*1.5; y=2.4+j*HR*math.sqrt(3)+(i%2)*HR*math.sqrt(3)/2
        if not(-23<=x<=14 and 2.2<=y<=19.8): continue
        if (x+23) > 26-(y-2)*0.5: continue
        deco.append(cq.Workplane("XY").workplane(offset=T).center(x,y).polygon(6,2*HR-0.3).polygon(6,2*HR-1.3).extrude(LBL))
# bieu tuong pin + mui ten chi xuong PWR
BX,BY=22.6,14.0
deco.append(cq.Workplane("XY").workplane(offset=T).center(BX,BY).rect(5.0,11.0).rect(3.8,9.8).extrude(LBL))
deco.append(zbox(2.2,1.0,BX,BY+6.0))
for k in range(3): deco.append(zbox(2.8,1.8,BX,BY-3.2+k*2.7))
for k in range(6):
    yy=2.0-k*6.5
    deco.append(cq.Workplane("XY").workplane(offset=T).polyline([(BX-2.2,yy),(BX,yy-2.0),(BX+2.2,yy),(BX+2.2,yy-1.0),(BX,yy-3.0),(BX-2.2,yy-1.0)]).close().extrude(LBL))
for d in deco:
    if d.solids().size()>0: p=p.union(d)
# cat lai tat ca lo xuyen de khong chi tiet nao che lo
for c in cuts: p=p.cut(c)
p=p.cut(cq.Workplane("XY").workplane(offset=-0.1).pushPoints(pts).circle(1.7).extrude(T+3))
b=p.val().BoundingBox(); print(NAME,p.val().isValid(),round(b.xlen,2),round(b.ylen,2),round(b.zmin,2),round(b.zmax,2))
# nap luoi loa (in rieng, up len coc)
CAP_IN=Ro+0.12; CW=1.2; CAP_H=4.0; TOP=1.2
cap=cq.Workplane("XY").circle(CAP_IN+CW).extrude(CAP_H+TOP).cut(cq.Workplane("XY").circle(CAP_IN).extrude(CAP_H))
# v2: vong de xuong vanh loa (nap tua tren 2 mau ngam, vong ep vanh loa 0.15 mm) -> loa khong nhap nho
PRESS=(CUP_H+1.2)-SPK_DEPTH+0.15
cap=cap.union(cq.Workplane("XY").workplane(offset=CAP_H-PRESS).circle(11.1).circle(10.4).extrude(PRESS+0.01))
# 3 gu nho bam vao thanh coc
for a in (60,180,300):
    cap=cap.union(cq.Workplane("XY").workplane(offset=0.8).center((CAP_IN-0.1)*math.cos(math.radians(a)),(CAP_IN-0.1)*math.sin(math.radians(a))).circle(0.35).extrude(2.0))
cap=cap.faces(">Z").edges().chamfer(0.6)
hp=[]
P=3.0
for j in range(-5,6):
    for i in range(-5,6):
        x=i*P+(j%2)*P/2; y=j*P*math.sqrt(3)/2
        if math.hypot(x,y)<=10.0: hp.append((x,y))
for k in range(12):
    a=k*30
    cap=cap.cut(cq.Workplane("XY").center(6.6*math.cos(math.radians(a)),6.6*math.sin(math.radians(a))).rect(7.0,1.5).extrude(CAP_H+TOP+1)
                .rotate((6.6*math.cos(math.radians(a)),6.6*math.sin(math.radians(a)),0),(6.6*math.cos(math.radians(a)),6.6*math.sin(math.radians(a)),1),a+25))
cap=cap.union(cq.Workplane("XY").workplane(offset=CAP_H).circle(2.6).extrude(TOP+0.6))
cq.exporters.export(cap,'nap-loa-tuabin-24.stl',tolerance=0.02,angularTolerance=0.1)
print('cap holes',len(hp), cap.val().isValid())
cq.exporters.export(p,NAME+'.step'); cq.exporters.export(p,NAME+'.stl',tolerance=0.02,angularTolerance=0.1)
# 2 phan cho in 2 mau (AMS): de OLED + cot rieng, phan con lai rieng
acc=p.intersect(OLED_ZONE); base=p.cut(OLED_ZONE)
print(' parts',acc.val().isValid(),base.val().isValid())
cq.exporters.export(base,NAME+'-phan-chinh.stl',tolerance=0.02,angularTolerance=0.1)
cq.exporters.export(acc,NAME+'-de-oled.stl',tolerance=0.02,angularTolerance=0.1)
# ong dem (in 4 cai)
sp=cq.Workplane("XY").circle(3.2).circle(1.75).extrude(SPACER)


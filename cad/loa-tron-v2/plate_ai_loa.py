# Tam sau "DIY" (dung lai bo cuc thiet ke cua ban), toa do giong khung truoc. z=0 la mat duoi tam.
import cadquery as cq, math, sys
CORNER_R=float(sys.argv[1]) if len(sys.argv)>1 else 0.0   # 0 = goc vuong (khung thuong), 7 = bo goc (khung ngau)
NAME=sys.argv[2] if len(sys.argv)>2 else 'tam-ai-loa-tron'
W,H,T=104.0,101.0,2.0
PCB_DX,PCB_DY=88.6,86.0
SPACER=6.5                  # tu mat sau PCB den mat duoi tam
RIM_W,RIM_H=2.0,1.0         # gờ vien
LBL=0.6                     # chu noi (doi mau o lop nay)
FONT="/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"

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
cuts.append(cq.Workplane("XY").workplane(offset=-0.1).pushPoints(via).circle(0.75).extrude(T+0.2))
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
for txt,x,y,sz,r in [("BAT",23.2,0,3.0,90),("PWR",28.6,-44.0,2.4,0),("BOOT",-34.6,-12.5,2.4,0),
                     ("OLED",-12.0,-44.4,2.2,0),("USB-C",6.0,-46.4,2.2,0),("SPK",-31.6,-47.0,2.4,0)]:
    p=p.union(label(txt,x,y,sz,r))


# ================= PHONG CACH MACH DIEN + CHIP AI =================
TW=0.9   # be rong duong mach
def trace(pts,pad_start=True,pad_end=True):
    w=cq.Workplane("XY").workplane(offset=T)
    segs=None
    for (x0,y0),(x1,y1) in zip(pts[:-1],pts[1:]):
        L=math.hypot(x1-x0,y1-y0); ang=math.degrees(math.atan2(y1-y0,x1-x0))
        sg=(cq.Workplane("XY").workplane(offset=T).center((x0+x1)/2,(y0+y1)/2).rect(L,TW).extrude(LBL)
            .rotate(((x0+x1)/2,(y0+y1)/2,0),((x0+x1)/2,(y0+y1)/2,1),ang))
        segs=sg if segs is None else segs.union(sg)
    for x,y in pts[1:-1]:
        segs=segs.union(cq.Workplane("XY").workplane(offset=T).center(x,y).circle(TW/2).extrude(LBL))
    for (x,y),on in ((pts[0],pad_start),(pts[-1],pad_end)):
        if on: segs=segs.union(cq.Workplane("XY").workplane(offset=T).center(x,y).circle(1.2).circle(0.5).extrude(LBL))
    return segs
# --- chip AI ---
CX,CY,CS=14.0,37.0,12.0
chip=cq.Workplane("XY").workplane(offset=T).center(CX,CY).rect(CS,CS).extrude(LBL).edges("|Z").fillet(1.0)
chip=chip.cut(cq.Workplane("XY").workplane(offset=T).center(CX,CY).text("AI",7.0,LBL+0.1,combine=False,fontPath=FONT,halign="center",valign="center"))
chip=chip.cut(cq.Workplane("XY").workplane(offset=T).center(CX-CS/2+1.6,CY+CS/2-1.6).circle(0.6).extrude(LBL+0.1))   # cham chan 1
pins=[]
for k in range(5):
    o=-4+k*2.0
    pins+= [(CX+o,CY+CS/2+1.1,0.9,1.4),(CX+o,CY-CS/2-1.1,0.9,1.4),(CX-CS/2-1.1,CY+o,1.4,0.9),(CX+CS/2+1.1,CY+o,1.4,0.9)]
for x,y,w,h in pins:
    chip=chip.union(cq.Workplane("XY").workplane(offset=T).center(x,y).rect(w,h).extrude(LBL))
p=p.union(chip)
p=p.union(label("Bon DIY",-6.5,41.6,4.6))
p=p.union(label("AI VOICE",-6.5,36.6,2.2))
# --- duong mach ---
tr=[]
# chip -> bus ESP32 (xuong khe tren)
for k,x in enumerate((CX-4,CX-2,CX,CX+2,CX+4)):
    tr.append([(x,CY-CS/2-1.8),(x,27.2)])
# chip -> o pin (sang phai)
tr.append([(CX+CS/2+1.8,CY+2),(24.4,CY+2)]); tr.append([(CX+CS/2+1.8,CY-2),(22.4,CY-2),(24.4,CY-4)])
# chip -> len tren, re trai theo mep
tr.append([(CX-2,CY+CS/2+1.8),(CX-2,45.6),(CX-6,45.6)],)
# day dai ben phai xuong PWR / USB-C
tr.append([(18.8,2.0),(18.8,-40.0),(21.8,-43.0),(24.4,-43.0)])
tr.append([(20.6,6.0),(20.6,-38.0)])
tr.append([(22.4,10.0),(22.4,-36.0),(24.4,-38.0)])
tr.append([(17.6,-44.2),(15.4,-46.2),(11.2,-46.2)])
# ben trai: bus doc tu loa len khe
tr.append([(-46.2,-14.0),(-46.2,16.0),(-44.2,18.0)])
tr.append([(-44.4,-12.0),(-44.4,-8.0),(-42.6,-6.2)])
# quanh lo BOOT -> de OLED
tr.append([(-22.0,-19.0),(-22.0,-13.0)])
# mic -> chip (vong qua)
tr.append([(-24.0,34.6),(-21.6,34.6)],)
for t in tr: p=p.union(trace(t))
# vong dem PCB quanh 4 lo vit goc
p=p.union(cq.Workplane("XY").workplane(offset=T).pushPoints(pts).circle(4.4).circle(3.5).extrude(LBL).cut(cq.Workplane("XY").center(SPK_X,SPK_Y).circle(Ri).extrude(20)))
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
cap=cap.cut(cq.Workplane("XY").pushPoints(hp).polygon(6,2.2).extrude(CAP_H+TOP+1))
cq.exporters.export(cap,'nap-luoi-loa-24.stl',tolerance=0.02,angularTolerance=0.1)
print('cap holes',len(hp), cap.val().isValid())
cq.exporters.export(p,NAME+'.step'); cq.exporters.export(p,NAME+'.stl',tolerance=0.02,angularTolerance=0.1)
# 2 phan cho in 2 mau (AMS): de OLED + cot rieng, phan con lai rieng
acc=p.intersect(OLED_ZONE); base=p.cut(OLED_ZONE)
print(' parts',acc.val().isValid(),base.val().isValid())
cq.exporters.export(base,NAME+'-phan-chinh.stl',tolerance=0.02,angularTolerance=0.1)
cq.exporters.export(acc,NAME+'-de-oled.stl',tolerance=0.02,angularTolerance=0.1)
# ong dem (in 4 cai)
sp=cq.Workplane("XY").circle(3.2).circle(1.75).extrude(SPACER)


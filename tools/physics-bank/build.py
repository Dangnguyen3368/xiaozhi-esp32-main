import json, sys, importlib
import common
topics = [("co","Cơ học"),("nhiet","Nhiệt học"),("dien","Điện học"),("quang","Quang học"),("thuchanh","Thực hành")]
# Real exam problems span topics; each module maps its ids to a topic in TOPICS.
modules = topics + [("dethat1","Đề thật 1"),("dethat2","Đề thật 2"),("dethat3","Đề thật 3"),("dethat4","Đề thật 4")]
out=[]; bad=0; figures={}; givens={}
ids=set()
for mod,name in modules:
    common.BANK.clear(); m=importlib.import_module(mod)
    tmap=getattr(m,"TOPICS",None)
    figures.update(getattr(m,"FIGURES",{}))
    givens.update(getattr(m,"GIVENS",{}))
    # Data summaries for problems without their own GIVENS live in givens_<module>.py.
    try: givens.update(importlib.import_module("givens_"+mod).GIVENS)
    except ModuleNotFoundError: pass
    for p in common.BANK:
        assert p["id"] not in ids, p["id"]; ids.add(p["id"])
        p["topic"]=tmap[p["id"]] if tmap else mod
        if p["check"]:
            try: ok = eval(p["check"], vars(__import__("math")))
            except Exception as e: ok=False; print("ERR",p["id"],e)
            if not ok: bad+=1; print("FAIL",p["id"],p["check"])
        out.append({k:v for k,v in p.items() if k!="check"})
    print(mod,len(common.BANK),"levels",[sum(1 for p in common.BANK if p["level"]==l) for l in (1,2,3)])
for p in out:
    p["figure"]=figures.get(p["id"])
    if p["id"] in givens: p["givens"]=givens.pop(p["id"])
assert not givens, "givens for unknown ids: %s" % sorted(givens)
print("givens",sum(1 for p in out if p.get("givens")),"missing",[p["id"] for p in out if not p.get("givens")])
print("figures",sum(1 for p in out if p["figure"]))
print("total",len(out),"checked",sum(1 for p in out),"bad",bad)
json.dump(out,open("physics_bank.json","w",encoding="utf-8"),ensure_ascii=False,indent=1)

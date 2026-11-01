from math import *
BANK = []
def P(id, lv, title, q, ans, hints, sol, chk=None):
    BANK.append(dict(id=id, level=lv, title=title, question=q, answer=ans,
                     hints=hints, solution=sol, check=chk))

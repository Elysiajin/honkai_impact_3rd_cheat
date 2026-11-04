import re
src=open("overlay/ui/_ranges.inc",encoding="utf-8").read()
nums=[int(x,16) for x in re.findall(r"0x([0-9A-Fa-f]{4})",src)]
ranges=[]
for i in range(0,len(nums),2):
    if i+1<len(nums): ranges.append([nums[i],nums[i+1]])
ranges.append([0x0020,0x00FF])
ranges.append([0x2000,0x206F])
ranges.sort()
merged=[]
for a,b in ranges:
    if merged and a<=merged[-1][1]+12:
        if b>merged[-1][1]: merged[-1][1]=b
    else:
        merged.append([a,b])
out=[]
line="   "
for a,b in merged:
    for tok in ("0x%04X"%a,"0x%04X"%b):
        piece=tok+", "
        if len(line)+len(piece)>100:
            out.append(line.rstrip()); line="   "
        line+=piece
out.append(line.rstrip().rstrip(","))
open("overlay/ui/_ranges.inc","w",encoding="utf-8",newline="\n").write("\n".join(out)+"\n")
print("ranges:",len(merged),"values:",len(merged)*2,"lines:",len(out))

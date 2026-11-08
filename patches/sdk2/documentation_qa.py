"""Contact sheets and text-bound checks for the generated PDF, not game tests."""
from pathlib import Path
import argparse
from PIL import Image,ImageDraw
import pdfplumber

p=argparse.ArgumentParser(description=__doc__);p.add_argument('directory',type=Path);a=p.parse_args()
pages=sorted(a.directory.glob('page-*.png'))
for offset in range(0,len(pages),12):
    selected=pages[offset:offset+12];sheet=Image.new('RGB',(4*310,3*455),'#b9c4cc');draw=ImageDraw.Draw(sheet)
    for at,path in enumerate(selected):
        im=Image.open(path).convert('RGB');im.thumbnail((300,425));x=(at%4)*310+5;y=(at//4)*455+23
        sheet.paste(im,(x,y));draw.text((x,y-18),path.stem,fill='black')
    sheet.save(a.directory/('contact-'+str(offset//12+1)+'.png'))
with pdfplumber.open(a.directory/'DKR-R-Modding-Bible-SDK-2.0.pdf') as document:
    issues=[];characters=0
    for number,page in enumerate(document.pages,1):
        characters+=len(page.chars)
        for item in page.chars:
            if item['x0']<40 or item['x1']>557 or item['top']<42 or item['bottom']>820:
                issues.append((number,item['text'],round(item['x0'],1),round(item['x1'],1),round(item['top'],1)))
    print('Pages:',len(document.pages),'characters:',characters,'out-of-safe-page-bound characters:',len(issues))
    if issues:print(issues[:30])

"""Build the SDK 2 Modding Bible from authored prose and exact staged headers.

This is document creation and static reference extraction, not mod execution.
The PDF is subsequently rendered and reviewed before it can be delivered.
"""
from pathlib import Path
import argparse
import html
import re
import textwrap

AUTH=Path(__file__).resolve().parent


def sections(source):
    result=[]
    for part in source.split('\n@@ '):
        heading,_,body=part.lstrip('@ ').partition('\n')
        result.append((heading.strip(),body.strip()))
    return result


def reference(sdk):
    chunks=[]
    for name in ('magic_codes.h','game.h','development.h','development.hpp','json.h','json_read.h','presentation.h'):
        path=sdk/'include/dkr'/name
        if path.is_file():chunks.append(('SDK REFERENCE - '+name,path.read_text()))
    return chunks


def create(sdk,output):
    from reportlab.lib import colors
    from reportlab.lib.enums import TA_LEFT
    from reportlab.lib.styles import getSampleStyleSheet,ParagraphStyle
    from reportlab.lib.utils import simpleSplit
    from reportlab.platypus import SimpleDocTemplate,Paragraph,Spacer,PageBreak,Preformatted,KeepTogether
    output.mkdir(parents=True,exist_ok=False)
    source=(AUTH/'BIBLE.txt').read_text(encoding='utf-8')
    chapters=sections(source)
    appendix=reference(sdk)
    title='DKR-R Modding Bible - SDK 2.0'
    full=title+'\n'+'='*len(title)+'\nv1.0.7 release\n\n'
    full+='\n\n'.join(heading+'\n'+'-'*min(78,len(heading))+'\n\n'+body for heading,body in chapters+appendix)
    (output/'DKR-R-Modding-Bible-SDK-2.0.txt').write_text(full+'\n',encoding='utf-8')
    nav=''.join(f'<a href="#chapter-{i}">{html.escape(heading)}</a>' for i,(heading,_) in enumerate(chapters+appendix))
    body=''
    for i,(heading,content) in enumerate(chapters+appendix):
        body+=f'<section id="chapter-{i}"><h2>{html.escape(heading)}</h2>'
        if i>=len(chapters):body+='<pre><code>'+html.escape(content)+'</code></pre>'
        else:
            for paragraph in content.split('\n\n'):
                if paragraph.startswith('    '):body+='<pre><code>'+html.escape(textwrap.dedent(paragraph))+'</code></pre>'
                else:body+='<p>'+html.escape(' '.join(paragraph.splitlines()))+'</p>'
        body+='</section>'
    document='<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">'
    document+='<title>'+title+'</title><style>body{margin:0;background:#edf4f7;color:#102b3a;font:17px/1.65 system-ui,sans-serif}header{background:#082d48;color:white;padding:3rem 6vw;border-bottom:6px solid #f6b843}h1{margin:0;color:#ffc859}h2{color:#075d7b;line-height:1.25}nav{padding:2rem;background:#dcebf1}nav a{display:block;color:#075675;margin:.3rem 0}main{max-width:1050px;margin:auto;padding:2rem}section{margin-bottom:4rem;scroll-margin-top:1rem}p{white-space:normal}pre{background:#082d48;color:#e8f7ff;padding:1.1rem;border-radius:8px;overflow:auto;font:14px/1.55 ui-monospace,monospace}code{tab-size:4}a{color:#076681}@media(min-width:1450px){nav{position:fixed;width:260px;top:0;bottom:0;overflow:auto;font-size:13px}header,main{margin-left:330px}}@media print{nav{display:none}header{padding:1rem}body{background:white}pre{white-space:pre-wrap;color:black;background:#eee}section{break-before:page}}</style>'
    document+='<header><h1>'+title+'</h1><p>v1.0.7 release - the complete authoring reference and Dev Menu guide</p></header><nav aria-label="Contents">'+nav+'</nav><main>'+body+'</main></html>'
    (output/'DKR-R-Modding-Bible-SDK-2.0.html').write_text(document,encoding='utf-8')
    styles=getSampleStyleSheet()
    styles.add(ParagraphStyle(name='BibleBody',fontName='Helvetica',fontSize=10.5,leading=15,spaceAfter=10,textColor=colors.HexColor('#102b3a')))
    styles.add(ParagraphStyle(name='BibleHeading',fontName='Helvetica-Bold',fontSize=20,leading=24,spaceAfter=18,textColor=colors.HexColor('#075d7b')))
    styles.add(ParagraphStyle(name='BibleCode',fontName='Courier',fontSize=7.2,leading=10,spaceAfter=10,leftIndent=8,rightIndent=8))
    story=[Paragraph('DKR-R',ParagraphStyle('Cover',fontName='Helvetica-Bold',fontSize=42,leading=48,textColor=colors.HexColor('#075d7b'))),Spacer(1,22),Paragraph('MODDING BIBLE',styles['Title']),Paragraph('SDK 2.0 / v1.0.7 release',styles['Heading2']),Spacer(1,28),Paragraph('Build powerful Custom Magic Codes. Keep existing mods working. Own your tools, not the engine scheduler.',styles['BibleBody']),Spacer(1,24),Paragraph('Portable C and freestanding C++, the API 1 compatibility path, all new API 2 services, the Dev Menu, online development, save isolation and the exact public headers.',styles['BibleBody']),PageBreak(),Paragraph('CONTENTS',styles['BibleHeading'])]
    for i,(heading,_) in enumerate(chapters+appendix,1):story.append(Paragraph('<link href="#chapter-'+str(i-1)+'" color="#075d7b">'+html.escape(f'{i}. {heading}')+'</link>',styles['BibleBody']))
    for i,(heading,content) in enumerate(chapters+appendix):
        story.extend([PageBreak(),Paragraph('<a name="chapter-'+str(i)+'"/>'+html.escape(heading),styles['BibleHeading'])])
        if i>=len(chapters):
            wrapped=[]
            for line in content.splitlines():wrapped.extend(textwrap.wrap(line,width=105,replace_whitespace=False,drop_whitespace=False,subsequent_indent='    ') or [''])
            story.append(Preformatted('\n'.join(wrapped),styles['BibleCode']))
        else:
            for paragraph in content.split('\n\n'):
                if paragraph.startswith('    '):
                    lines=[]
                    for line in textwrap.dedent(paragraph).splitlines():lines.extend(textwrap.wrap(line,width=105,replace_whitespace=False,drop_whitespace=False,subsequent_indent='    ') or [''])
                    story.append(Preformatted('\n'.join(lines),styles['BibleCode']))
                else:story.append(Paragraph(html.escape(' '.join(paragraph.splitlines())),styles['BibleBody']))
    def page(canvas,doc):
        canvas.saveState();canvas.setStrokeColor(colors.HexColor('#e0ab3c'));canvas.line(42,36,553,36);canvas.setFillColor(colors.HexColor('#315568'));canvas.setFont('Helvetica',8);canvas.drawString(42,24,'DKR-R SDK 2.0 | Modding Bible | v1.0.7');canvas.drawRightString(553,24,str(doc.page));canvas.restoreState()
    path=output/'DKR-R-Modding-Bible-SDK-2.0.pdf'
    SimpleDocTemplate(str(path),pagesize=(595.28,841.89),leftMargin=42,rightMargin=42,topMargin=46,bottomMargin=48,title=title,author='DKR-R',pageCompression=1).build(story,onFirstPage=page,onLaterPages=page)
    print('Created TXT, HTML and PDF. PDF rendering/layout inspection is still required: '+str(path))


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--sdk',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args();create(a.sdk,a.output)

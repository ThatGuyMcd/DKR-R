"""Portal frontend splices: shared catalogue permissions and upload checksums."""

from pathlib import Path


def once(text, old, new):
    if text.count(old) != 1:
        raise ValueError("Portal frontend anchor changed: " + old[:100])
    return text.replace(old, new)


def transform(name, text):
    if name == "uploads-client.js":
        text = once(
            text,
            "  async function checkImage(file, extension) {",
            (Path(__file__).parent / "upload-hash.inl").read_text(encoding="utf-8")
            + "\n  async function checkImage(file, extension) {",
        )
        text = once(
            text,
            "      mode='completed';message='Upload complete. Review the mod details before publishing.';selectedFile=null;lastError='';notify();return snapshot();",
            "      if(kind==='download'){mode='completing';message='Preparing the download checksum…';notify();await preparePackageDownload(selectedFile,epoch,expected,selection);}\n      mode='completed';message='Upload complete. Ready to publish and download.';selectedFile=null;lastError='';notify();return snapshot();",
        )
        text = once(
            text,
            "      const hash=await fingerprint(file);assertSelection(epoch,expected,run);\n      if(job&&['open','completing','cancel_pending'].includes(job.state)){",
            "      const hash=await fingerprint(file);assertSelection(epoch,expected,run);\n      if(job&&['open','completing','completed','cancel_pending'].includes(job.state)){",
        )
        return text
    if name == "api-store.js":
        text = once(text, "(D.isAdmin() ? D.managedItems.find", "(D.isStaff() ? D.managedItems.find")
        text = once(
            text,
            "if (personal.user && personal.user.role === 'admin') admin = await paged('/v1/admin', ['accounts', 'drafts', 'audit', 'items'], token, epoch);",
            "if (personal.user && ['moderator','admin'].includes(personal.user.role)) admin = await paged(personal.user.role === 'admin' ? '/v1/admin' : '/v1/catalogue-tools', ['accounts', 'drafts', 'audit', 'items'], token, epoch);",
        )
        return text
    if name != "app.js":
        raise ValueError("Unexpected frontend source")
    text = once(
        text,
        "function managedItems() { return isAdmin()&&Array.isArray(D.managedItems)?D.managedItems:list(D&&D.items); }",
        "function managedItems() { return isStaff()&&Array.isArray(D.managedItems)&&D.managedItems.length?D.managedItems:list(D&&D.items); }",
    )
    text = text.replace("isAdmin()?button('add-listing'", "isStaff()?button('add-listing'")
    text = text.replace("(isAdmin()?button('admin','Library manager'", "(isStaff()?button('admin','Library manager'")
    text = once(
        text,
        "function metadataPage() {\n    if(!isAdmin())return deniedPage(true);",
        "function metadataPage() {\n    if(!isStaff())return deniedPage();",
    )
    text = text.replace("s.editorMode==='admin'&&!isAdmin()", "s.editorMode==='admin'&&!isStaff()")
    text = text.replace("p.mode==='admin'&&!isAdmin()", "p.mode==='admin'&&!isStaff()")
    text = once(
        text,
        "function adminPage() {\n    if(!isAdmin())return deniedPage(true);",
        "function adminPage() {\n    if(!isStaff())return deniedPage();\n    if(!isAdmin()&&s.adminTab!=='catalogue')s.adminTab='catalogue';",
    )
    text = once(
        text,
        "[['catalogue','Catalogue'],['staff','Staff access'],['audit','Activity log']].map",
        "[['catalogue','Catalogue'],...(isAdmin()?[['staff','Staff access'],['audit','Activity log']]:[])].map",
    )
    for start in (
        "if(action==='edit-details'){",
        "if(action==='add-listing'||action==='edit-listing'){",
        "if(action==='open-draft'){",
        "if(kind==='metadata'){",
        "if(choice==='draft'){",
    ):
        text = once(
            text,
            start + "if(!isAdmin())throw new Error('Administrator access is required.');",
            start + "if(!isStaff())throw new Error('Moderator access is required.');",
        )
    text = once(
        text,
        "if(action==='admin-tab'){if(!isAdmin())throw new Error('Administrator access is required.');",
        "if(action==='admin-tab'){if(!isStaff()||(id!=='catalogue'&&!isAdmin()))throw new Error('Administrator access is required for this section.');",
    )
    text = once(
        text,
        "        else if(!isAdmin())throw new Error('Administrator access is required.');",
        "        else if(!isStaff())throw new Error('Moderator access is required.');",
    )
    # Archive/restore, staff roles and connection diagnostics deliberately retain
    # their separate administrator guards. Do not globally replace isAdmin().
    return text

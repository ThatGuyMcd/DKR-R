// Fixture: each anchor frontend.py splices at, once.
function managedItems() { return isAdmin()&&Array.isArray(D.managedItems)?D.managedItems:list(D&&D.items); }
function metadataPage() {
    if(!isAdmin())return deniedPage(true);
function adminPage() {
    if(!isAdmin())return deniedPage(true);
[['catalogue','Catalogue'],['staff','Staff access'],['audit','Activity log']].map
if(action==='edit-details'){if(!isAdmin())throw new Error('Administrator access is required.');
if(action==='add-listing'||action==='edit-listing'){if(!isAdmin())throw new Error('Administrator access is required.');
if(action==='open-draft'){if(!isAdmin())throw new Error('Administrator access is required.');
if(kind==='metadata'){if(!isAdmin())throw new Error('Administrator access is required.');
if(choice==='draft'){if(!isAdmin())throw new Error('Administrator access is required.');
if(action==='admin-tab'){if(!isAdmin())throw new Error('Administrator access is required.');
        else if(!isAdmin())throw new Error('Administrator access is required.');
isAdmin()?button('add-listing'
(isAdmin()?button('admin','Library manager'
s.editorMode==='admin'&&!isAdmin()
p.mode==='admin'&&!isAdmin()

const fs=require('node:fs'),vm=require('node:vm'),path=require('node:path');
const {DatabaseSync}=require('node:sqlite');
const {webcrypto,createHash}=require('node:crypto');
const schema=`
PRAGMA foreign_keys=ON;
CREATE TABLE accounts(id TEXT PRIMARY KEY,discord_id TEXT,display_name TEXT,role TEXT,status TEXT,created_at INTEGER,updated_at INTEGER);
CREATE TABLE sessions(token_hash TEXT PRIMARY KEY,user_id TEXT REFERENCES accounts(id),created_at INTEGER,last_seen_at INTEGER,idle_expires_at INTEGER,absolute_expires_at INTEGER,revoked_at INTEGER);
CREATE TABLE library_items(id TEXT PRIMARY KEY,title TEXT,author TEXT,category TEXT,summary TEXT,image TEXT,tags_json TEXT,related_json TEXT,featured INTEGER,current_release_id TEXT,archived INTEGER DEFAULT 0,revision INTEGER DEFAULT 1,created_at INTEGER,updated_at INTEGER,last_mutation TEXT);
CREATE TABLE library_releases(id TEXT PRIMARY KEY,item_id TEXT REFERENCES library_items(id),version TEXT,file_name TEXT,file_url TEXT,format TEXT,requirements TEXT,install TEXT,created_at INTEGER,UNIQUE(item_id,version));
CREATE TABLE library_catalogue_drafts(id TEXT PRIMARY KEY,owner_id TEXT REFERENCES accounts(id),content_json TEXT,status TEXT DEFAULT 'draft',revision INTEGER DEFAULT 1,listing_id TEXT,release_id TEXT,created_at INTEGER,updated_at INTEGER,last_mutation TEXT);
CREATE TABLE library_publications(id TEXT PRIMARY KEY,source_type TEXT,source_id TEXT,item_id TEXT,release_id TEXT,created_at INTEGER,UNIQUE(source_type,source_id));
CREATE TABLE library_audit(id TEXT PRIMARY KEY,actor_id TEXT,actor_name TEXT,action TEXT,target_id TEXT,message TEXT,created_at INTEGER);
CREATE TABLE library_action_limits(account_id TEXT,bucket TEXT,window_start INTEGER,count INTEGER,PRIMARY KEY(account_id,bucket));
CREATE TABLE library_reviews(id TEXT PRIMARY KEY,item_id TEXT,account_id TEXT,score INTEGER,body TEXT,tested_version TEXT,hidden INTEGER DEFAULT 0,hidden_reason TEXT,revision INTEGER DEFAULT 1,created_at INTEGER,updated_at INTEGER);
CREATE TABLE library_upload_jobs(id TEXT PRIMARY KEY,owner_id TEXT,kind TEXT,total_size INTEGER,original_name TEXT,state TEXT,completed_at INTEGER,relative_path TEXT);
CREATE TABLE library_requests(id TEXT PRIMARY KEY,owner_id TEXT,updated_at INTEGER,status TEXT);
CREATE TABLE library_review_reports(id TEXT,review_id TEXT,reporter_id TEXT,created_at INTEGER);
CREATE TABLE launcher_release_integrity(release_id TEXT PRIMARY KEY,sha256 TEXT,bytes INTEGER,verified_at INTEGER);
CREATE TABLE launcher_asset_integrity(file_url TEXT PRIMARY KEY,sha256 TEXT,bytes INTEGER,verified_at INTEGER);
`;
function fixture(stage){
  const sqlite=new DatabaseSync(':memory:');sqlite.exec(schema);
  class Query{
    constructor(sql,args=[]){this.sql=sql;this.args=args;}
    bind(...args){return new Query(this.sql,args);}
    async first(){return sqlite.prepare(this.sql).get(...this.args)||null;}
    async all(){return {results:sqlite.prepare(this.sql).all(...this.args)};}
    async run(){const result=sqlite.prepare(this.sql).run(...this.args);return {meta:{changes:Number(result.changes)}};}
    execute(){const stmt=sqlite.prepare(this.sql);if(stmt.columns().length)return {results:stmt.all(...this.args),meta:{changes:0}};const r=stmt.run(...this.args);return {results:[],meta:{changes:Number(r.changes)}};}
  }
  const db={prepare:sql=>new Query(sql),withSession(){return this;},async batch(queries){sqlite.exec('BEGIN');try{const r=queries.map(q=>q.execute());sqlite.exec('COMMIT');return r;}catch(e){sqlite.exec('ROLLBACK');throw e;}}};
  const context={URL,Request,Response,Headers,crypto:webcrypto,TextEncoder,TextDecoder,ReadableStream,AbortController,setTimeout,clearTimeout,Uint8Array,Uint32Array,DataView,console,atob,btoa,structuredClone,
    fetch:async()=>{throw new Error('External requests are disabled in workflow tests.');}};
  let source=fs.readFileSync(path.join(stage,'PRIVATE-API/worker-single-file.mjs'),'utf8');
  source=source.replace('export default {','globalThis.worker = {');
  vm.runInNewContext(source+'\nglobalThis.modules={library:__dkrLibrary,hosted:__dkrHostedFiles,uploads:__dkrUploads};',context);
  const env={DB:db,FRONTEND_URL:'https://positron-portal.com/dkr-r/index.html',DOWNLOAD_ORIGINS:JSON.stringify(['https://positron-portal.com/dkr-r/downloads/']),IMAGE_ORIGINS:JSON.stringify(['https://positron-portal.com/dkr-r/images/'])};
  const tokens={};
  for(const role of ['admin','moderator','player']){
    const id='test_'+role,token=(role[0]+'test').padEnd(43,'a'),now=Math.floor(Date.now()/1000);
    sqlite.prepare('INSERT INTO accounts VALUES(?,?,?,?,?,?,?)').run(id,id,'Test '+role,role,'active',now,now);
    sqlite.prepare('INSERT INTO sessions VALUES(?,?,?,?,?,?,NULL)').run(createHash('sha256').update(token).digest('base64url'),id,now,now,now+3600,now+86400);
    tokens[role]=token;
  }
  async function call(route,body,role='moderator'){
    const headers={Origin:'https://positron-portal.com',...(role?{Authorization:'Bearer '+tokens[role]}:{}),...(body!==undefined?{'Content-Type':'application/json'}:{})};
    const request=new Request('https://test.invalid'+route,{method:body===undefined?'GET':'POST',headers,body:body===undefined?undefined:JSON.stringify(body)});
    try{
      const result=await context.modules.hosted.handleHostedFiles(request,env)||await context.modules.library.handleLibrary(request,env,{});
      return {status:result?result.status:404,data:result?await result.json():null};
    }catch(e){return {status:e.status||500,data:{error:{message:e.message,code:e.code||''}}};}
  }
  return {sqlite,env,db,context,tokens,call};
}
function draft(patch={}){return {target:'new',category:'tracks',title:'Test track',author:'Test creator',summary:'A useful description.',version:'1.0',fileName:'new.zip',fileUrl:'https://positron-portal.com/dkr-r/downloads/Tracks/new.zip',format:'.zip',image:'pattern',requirements:'',install:'Import this package.',tags:[],related:[],featured:false,...patch};}
module.exports={fixture,draft,schema};

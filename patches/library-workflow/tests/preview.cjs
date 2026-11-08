// Isolated local QA: synthetic sign-in/database, real staged website scripts.
// This helper is never included in the deployment package and cannot use FTP.
const http=require('node:http'),fs=require('node:fs'),path=require('node:path');
const {fixture,draft}=require('./support.cjs');
const stage=path.resolve(process.argv[2]),ftp=path.join(stage,'FTP-PATCH'),f=fixture(stage),cache=new Map();
const types={'.js':'application/javascript','.css':'text/css','.html':'text/html','.png':'image/png'};
async function main(){
  await f.call('/v1/workflow/entry',{operationId:'7'.repeat(32),draft:draft({title:'Toad’s Turnpike',fileName:'Toads_Turnpike.zip',fileUrl:'https://positron-portal.com/dkr-r/downloads/Tracks/Toads_Turnpike.zip',image:'https://positron-portal.com/dkr-r/images/Tracks/raceway.png'}),publish:true},'admin');
  await f.call('/v1/workflow/entry',{operationId:'8'.repeat(32),draft:draft({title:'Moo Moo Farm draft',fileName:'Moo_Moo_Farm.zip',fileUrl:'https://positron-portal.com/dkr-r/downloads/Tracks/Moo_Moo_Farm.zip'})},'admin');
  const server=http.createServer(async(req,res)=>{
    try{
      const url=new URL(req.url,'http://127.0.0.1:8765');
      if(url.pathname.startsWith('/mock-api/')){
        let body='';for await(const bytes of req){body+=bytes;if(body.length>2*1024*1024)throw new Error('Preview body too large');}
        const result=await f.call(url.pathname.slice('/mock-api'.length)+url.search,body?JSON.parse(body):undefined,'admin');
        res.writeHead(result.status,{'Content-Type':'application/json'});res.end(JSON.stringify(result.data));return;
      }
      if(url.pathname==='/preview-auth.js'){
        const user={id:'test_admin',displayName:'Local QA admin',role:'admin',status:'active'};
        res.writeHead(200,{'Content-Type':'application/javascript'});
        res.end('const originalFetch=window.fetch.bind(window);window.fetch=(url,options)=>originalFetch(typeof url==="string"&&url.startsWith("https://test.invalid/")?"/mock-api/"+url.slice("https://test.invalid/".length):url,options);window.DKRAuth={ready:Promise.resolve(),getUser:()=>('+JSON.stringify(user)+'),getToken:()=>'+JSON.stringify(f.tokens.admin)+',ensureSession:async()=>{},onChange:()=>()=>{},logout:async()=>{}};');return;
      }
      if(url.pathname==='/config.js'){res.writeHead(200,{'Content-Type':'application/javascript'});res.end('window.DKR_CONFIG={apiBase:"https://test.invalid",discordInvite:"https://discord.com/invite/AMWfXdBjNP"};');return;}
      const relative=url.pathname==='/'?'index.html':url.pathname.replace(/^\//,'');
      const file=path.resolve(ftp,relative);if(!file.startsWith(ftp+path.sep)&&file!==path.join(ftp,'index.html'))throw new Error('Invalid preview path');
      let raw;
      if(fs.existsSync(file))raw=fs.readFileSync(file);
      else if(/^assets\/(app\.css|auth-client\.js|lucide\.min\.js|(?:logo|icon|sky|pattern|pattern-two)\.png)$/.test(relative)){
        if(!cache.has(relative)){const response=await fetch('https://positron-portal.com/dkr-r/'+relative);if(!response.ok)throw new Error('Missing public preview asset');cache.set(relative,Buffer.from(await response.arrayBuffer()));}raw=cache.get(relative);
      }else{res.writeHead(404);res.end();return;}
      if(relative==='index.html')raw=Buffer.from(raw.toString().replace("connect-src https:","connect-src 'self' https:").replace(/assets\/auth-client\.js\?v=[^"]+/, '/preview-auth.js'));
      res.writeHead(200,{'Content-Type':types[path.extname(relative)]||'application/octet-stream','Cache-Control':'no-store'});res.end(raw);
    }catch(e){console.error(e.message);res.writeHead(500,{'Content-Type':'application/json'});res.end(JSON.stringify({error:{message:e.message}}));}
  });
  server.listen(8765,'127.0.0.1',()=>console.log('Isolated website preview ready: http://127.0.0.1:8765/ (no real sign-in, publication or FTP writes)'));
}
main().catch(e=>{console.error(e);process.exitCode=1;});

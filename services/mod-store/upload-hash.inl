  // Bounded-memory SHA-256 for full packages, not the resume sample. File bytes
  // are checked against the server's confirmed chunk digests in the same pass.
  class PackageSha256 {
    constructor(){this.h=new Uint32Array([0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19]);this.buffer=new Uint8Array(64);this.used=0;this.length=0;this.w=new Uint32Array(64);}
    block(bytes,at){
      const k=PackageSha256.k,w=this.w,ror=(x,n)=>(x>>>n)|(x<<(32-n));
      for(let i=0;i<16;i++){const p=at+i*4;w[i]=(bytes[p]<<24)|(bytes[p+1]<<16)|(bytes[p+2]<<8)|bytes[p+3];}
      for(let i=16;i<64;i++){const a=w[i-15],b=w[i-2];w[i]=(w[i-16]+(ror(a,7)^ror(a,18)^(a>>>3))+w[i-7]+(ror(b,17)^ror(b,19)^(b>>>10)))>>>0;}
      let [a,b,c,d,e,f,g,h]=this.h;
      for(let i=0;i<64;i++){const t1=(h+(ror(e,6)^ror(e,11)^ror(e,25))+((e&f)^(~e&g))+k[i]+w[i])>>>0,t2=((ror(a,2)^ror(a,13)^ror(a,22))+((a&b)^(a&c)^(b&c)))>>>0;h=g;g=f;f=e;e=(d+t1)>>>0;d=c;c=b;b=a;a=(t1+t2)>>>0;}
      [a,b,c,d,e,f,g,h].forEach((x,i)=>{this.h[i]=(this.h[i]+x)>>>0;});
    }
    update(bytes){
      this.length+=bytes.length;let at=0;
      if(this.used){const n=Math.min(64-this.used,bytes.length);this.buffer.set(bytes.subarray(0,n),this.used);this.used+=n;at=n;if(this.used===64){this.block(this.buffer,0);this.used=0;}}
      while(at+64<=bytes.length){this.block(bytes,at);at+=64;}
      if(at<bytes.length){this.buffer.set(bytes.subarray(at),0);this.used=bytes.length-at;}
    }
    hex(){const bits=this.length*8,tail=new Uint8Array(this.used<56?64-this.used:128-this.used);tail[0]=128;const view=new DataView(tail.buffer);view.setUint32(tail.length-8,Math.floor(bits/4294967296));view.setUint32(tail.length-4,bits>>>0);this.update(tail);return Array.from(this.h,n=>n.toString(16).padStart(8,'0')).join('');}
  }
  PackageSha256.k=new Uint32Array([0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2]);
  async function preparePackageDownload(file,epoch,expected,run){
    const saved=await request('/v1/uploads/'+job.id,undefined,epoch,expected);
    assertSelection(epoch,expected,run);
    const current=validJob(saved.upload),parts=saved.resumeChunks;
    if(current.id!==job.id||current.state!=='completed'||current.size!==file.size||current.offset!==file.size||!Array.isArray(parts)||parts.length>512)
      throw failure('The completed package could not be checked. Retry this upload.','invalid_response');
    const full=new PackageSha256();let end=0;
    for(const part of parts){
      assertSelection(epoch,expected,run);
      if(!part||part.offset!==end||!Number.isSafeInteger(part.size)||part.size!==Math.min(current.chunkBytes,file.size-end)||part.size<1||!/^[a-f0-9]{64}$/.test(part.sha256))throw failure('The completed upload has invalid chunk records.','invalid_response');
      const bytes=new Uint8Array(await file.slice(end,end+part.size).arrayBuffer());
      assertSelection(epoch,expected,run);
      const digest=Array.from(new Uint8Array(await root.crypto.subtle.digest('SHA-256',bytes)),n=>n.toString(16).padStart(2,'0')).join('');
      if(bytes.length!==part.size||digest!==part.sha256)throw failure('The selected file no longer matches its uploaded bytes. Do not publish this file.','upload_file_changed');
      // Yield between 256 KiB blocks so controls remain responsive while hashing.
      for(let at=0;at<bytes.length;at+=262144){full.update(bytes.subarray(at,at+262144));await new Promise(resolve=>root.setTimeout(resolve,0));assertSelection(epoch,expected,run);}
      end+=bytes.length;message='Preparing the download: '+Math.round(end/file.size*100)+'%';notify();
    }
    if(end!==file.size)throw failure('The package checksum is incomplete.','invalid_response');
    await request('/v1/launcher/uploads/'+job.id+'/integrity',{sha256:full.hex(),bytes:end},epoch,expected);
    assertSelection(epoch,expected,run);
  }

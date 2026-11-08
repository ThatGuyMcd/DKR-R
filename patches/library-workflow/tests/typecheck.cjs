// Check the reported batch-validation function using inference from the entire
// real Worker. Cloudflare-specific diagnostics elsewhere are outside this test.
const fs=require('node:fs'),path=require('node:path');
const ts=require(path.resolve(process.argv[3]));
const worker=path.resolve(process.argv[2]);
const source=fs.readFileSync(worker,'utf8');
const start=source.indexOf('function workflowBatchContent(');
const end=source.indexOf('function workflowBatchDTO(',start);
if(start<0||end<start)throw new Error('Batch validation function not found.');
const program=ts.createProgram([worker],{allowJs:true,checkJs:true,noEmit:true,
  target:ts.ScriptTarget.ES2022,module:ts.ModuleKind.ESNext,skipLibCheck:true});
const diagnostics=ts.getPreEmitDiagnostics(program).filter(d=>d.file&&path.resolve(d.file.fileName)===worker&&d.start>=start&&d.start<end);
for(const d of diagnostics){
  const pos=d.file.getLineAndCharacterOfPosition(d.start);
  console.log(`${pos.line+1}:${pos.character+1} TS${d.code}: ${ts.flattenDiagnosticMessageText(d.messageText,' ')}`);
}
console.log(`Batch validation: ${diagnostics.length} type-check errors (TypeScript ${ts.version}).`);
process.exitCode=diagnostics.length?1:0;

const assert = require('node:assert/strict');

async function main() {
  const { replayStorage } = await import('../api/speedrun_storage.mjs');
  const files = new Map();
  const client = {
    size: async file => files.get(file)?.length ?? null,
    append: async (file, offset, bytes) => {
      assert.equal(offset, 0);
      assert.equal(files.has(file), false);
      files.set(file, Buffer.from(bytes));
    },
    rename: async (temp, final) => {
      assert.equal(temp, final + '.part');
      assert.ok(files.has(temp));
      files.set(final, files.get(temp));
      files.delete(temp);
    },
    readRange: async (file,offset,length) => files.get(file).subarray(offset,offset+length),
    remove: async file => files.delete(file),
  };
  const store = replayStorage({
    SPEEDRUN_FTP:'true', SPEEDRUN_FTP_KEY:'a1'.repeat(32), FTP_USERNAME:'fixture', FTP_PASSWORD:'fixture',
  }, { createClient: () => ({ withSession: work => work(client) }) });
  const a = 'runs/' + 'A'.repeat(24) + '.dkrreplay';
  const b = 'runs/' + 'B'.repeat(24) + '.dkrreplay';
  const ghost = 'runs/' + 'A'.repeat(24) + '.dkrghost';
  const source = Buffer.from('DKRREPL2 private evidence');
  assert.equal(await store.get(a), null);
  await store.put(a,source);
  await store.put(a,source);
  await store.put(b,source);
  const [archiveA, archiveB] = [...files.keys()];
  const encrypted = files.get(archiveA);
  assert.equal(encrypted.length, source.length + 28);
  assert.notDeepEqual(encrypted, files.get(archiveB));
  assert.equal(encrypted.includes(source), false);
  assert.deepEqual(Buffer.from(await (await store.get(a)).arrayBuffer()), source);
  await assert.rejects(store.put(a,Buffer.from('different')));
  files.set(archiveB,encrypted);
  await assert.rejects(store.get(b), 'a file cannot be reused under another run id');
  await assert.rejects(store.get('../private.txt'));
  const trajectory = Buffer.from('DKRPGH01 prepared ghost');
  await store.put(ghost, trajectory);
  const archiveGhost = [...files.keys()].find(file => file.endsWith('dkr-ghost-' + 'A'.repeat(24) + '.bin'));
  const encryptedGhost = files.get(archiveGhost);
  assert.equal(encryptedGhost.includes(trajectory), false);
  assert.deepEqual(Buffer.from(await (await store.get(ghost)).arrayBuffer()), trajectory);
  files.set(archiveGhost, encrypted);
  await assert.rejects(store.get(ghost), 'a replay archive cannot be substituted for a ghost');
  await store.delete([a,b,ghost]);
  await store.delete([a,b,ghost]);
  assert.equal(files.size,0);
  console.log('FTP evidence passed: encryption, authenticated reads, retry, path restriction and cleanup.');
}
main().catch(error => { console.error(error); process.exitCode = 1; });


// Durable journal is independent of the policy panels' current-boot log window.
export class LocalLogArchive {
  constructor(storage, changed = () => {}) {
    this.storage = storage; this.changed = changed; this.pending = []; this.tail = Promise.resolve();
    this.devices = {}; this.latest = null; this.count = 0; this.error = ''; this.recent = [];
    this.ready = this.tail = this.restore();
  }
  async restore() {
    try {
      const saved = await this.storage.restore();
      this.devices = saved.devices || {}; this.latest = saved.latest || null;
      this.count = saved.count || 0; this.recent = saved.recent || [];
    } catch (e) { this.error = '本地存储不可用：' + e.message; }
    this.changed(this);
  }
  ingest(snapshot, now = new Date().toISOString()) {
    // Copy before mergePageLogs changes the snapshot's logs.
    const d = JSON.parse(JSON.stringify(snapshot));
    this.tail = this.tail.then(async () => {
      const device = d.deviceId || 'legacy-device';
      const prior = this.devices[device];
      const boot = d.bootId || (prior && d.ms >= prior.ms && d.logSeq >= prior.seq ? prior.boot : 'legacy-' + now);
      const previous = prior?.boot === boot ? prior.seq : 0;
      const rows = [], context = { deviceId: device, bootId: boot, receivedAt: now };
      if (prior?.boot !== boot) rows.push({ ...context, key: device + '/' + boot + '/boot', source: 'LOCAL_BOOT', ms: d.ms,
        line: '首次收到本次启动的状态', firmware: d.firmware, network: d.network, config: d.config, cameraSettings: d.cameraSettings });
      const incoming = (d.logs || []).filter(r => Number.isInteger(r.seq) && r.seq > previous).sort((a,b) => a.seq-b.seq);
      let cursor = previous;
      for (const row of incoming) {
        if (row.seq > cursor + 1) rows.push({ ...context, key: device + '/' + boot + '/gap/' + (cursor+1) + '-' + (row.seq-1),
          source: 'LOCAL_GAP', ms: d.ms, line: '网页未收到日志 seq ' + (cursor+1) + '–' + (row.seq-1) });
        rows.push({ ...row, ...context, key: device + '/' + boot + '/' + row.seq }); cursor = row.seq;
      }
      if (d.cameraSettings?.revision !== prior?.cameraRevision) rows.push({ ...context,
        key: device + '/' + boot + '/camera/' + d.cameraSettings?.revision, source: 'LOCAL_CAMERA_SETTINGS', ms: d.ms,
        line: '摄像头参数回读', cameraSettings: d.cameraSettings });
      for (let i=0;i<rows.length;i++) rows[i].order=i;
      this.devices[device] = { boot, ms: d.ms, seq: cursor, cameraRevision: d.cameraSettings?.revision };
      this.latest = d; this.pending.push(...rows);
      this.recent.push(...rows); this.recent = this.recent.slice(-500);
      await this.flush(); this.changed(this);
    }).catch(e => { this.error = e.message; this.changed(this); });
    return this.tail;
  }
  connectionLost(message, now = new Date().toISOString()) {
    this.tail = this.tail.then(async () => {
      const row = { key: 'local/disconnect/' + now, receivedAt: now, order:0, source: 'LOCAL_DISCONNECT', line: message };
      this.pending.push(row); this.recent.push(row); this.recent = this.recent.slice(-500);
      await this.flush(); this.changed(this);
    });
    return this.tail;
  }
  async flush() {
    try {
      this.count = await this.storage.commit(this.pending, { devices: this.devices, latest: this.latest });
      this.pending = []; this.error = '';
    } catch(e) { this.error = '本地保存失败（待保存记录仍可导出）：' + e.message; }
  }
  exportChunks() {
    // Export and ingest share the queue: a concurrent poll must not have its
    // pending rows cleared by the export's flush, or enter halfway through it.
    const exported = this.tail.then(() => this.buildExportChunks());
    this.tail = exported.then(() => {}, () => {});
    return exported;
  }
  async buildExportChunks() {
    await this.flush(); this.changed(this);
    const chunks = ['{"format":"esp32-local-history-v1","exportedAt":' + JSON.stringify(new Date().toISOString()) +
      ',"storageError":' + JSON.stringify(this.error) + ',"lastState":' + JSON.stringify(this.latest) + ',"logs":['];
    const pendingKeys = new Set(this.pending.map(r => r.key)); let first = true;
    const append = row => { chunks.push((first ? '' : ',') + JSON.stringify(row)); first = false; };
    // Read failure must fail export, never silently produce an incomplete archive.
    await this.storage.visit(row => { if (!pendingKeys.has(row.key)) append(row); });
    for (const row of this.pending) append(row);
    chunks.push(']}'); return chunks;
  }
}

export function indexedLogStorage(indexedDB) {
  let opening;
  const open = () => opening ||= new Promise((resolve,reject) => {
    if (!indexedDB) { reject(Error('浏览器不支持 IndexedDB')); return; }
    const r = indexedDB.open('esp32-cat-local-history', 1);
    r.onupgradeneeded = () => { r.result.createObjectStore('logs', {keyPath:'key'}).createIndex('receivedAt',['receivedAt','order']); r.result.createObjectStore('meta'); };
    r.onerror = () => { opening = null; reject(r.error); };
    r.onblocked = () => reject(Error('其他页面阻止打开本地数据库，请关闭旧页面后重试'));
    r.onsuccess = () => { r.result.onversionchange = () => r.result.close(); resolve(r.result); };
  });
  return {
    async restore() {
      const db = await open();
      return new Promise((resolve,reject) => {
        const tx = db.transaction(['logs','meta'],'readonly'); let state = {}, count = 0; const recent = [];
        const m = tx.objectStore('meta').get('state'); m.onsuccess = () => state = m.result || {};
        const c = tx.objectStore('logs').count(); c.onsuccess = () => count = c.result;
        const r = tx.objectStore('logs').index('receivedAt').openCursor(null,'prev');
        r.onsuccess = () => { const cursor = r.result; if(cursor && recent.length < 500) { recent.push(cursor.value); cursor.continue(); } };
        tx.oncomplete = () => resolve({...state,count,recent:recent.reverse()}); tx.onabort = () => reject(tx.error || Error('本地读取中断'));
      });
    },
    async commit(rows, meta) {
      const db = await open();
      return new Promise((resolve,reject) => {
        const tx = db.transaction(['logs','meta'],'readwrite'), store = tx.objectStore('logs'); let count;
        for(const row of rows) { const r = store.get(row.key); r.onsuccess = () => { if(!r.result)store.put(row); }; }
        tx.objectStore('meta').put(meta,'state');
        // Count after all get/put requests have finished, in the same transaction.
        tx.oncomplete = () => {
          const read = db.transaction('logs','readonly'); const r = read.objectStore('logs').count();
          r.onsuccess = () => count = r.result; read.oncomplete = () => resolve(count); read.onabort = () => reject(read.error);
        };
        tx.onabort = () => reject(tx.error || Error('本地写入中断'));
      });
    },
    async visit(append) {
      const db = await open();
      return new Promise((resolve,reject) => {
        const tx = db.transaction('logs','readonly'); const r = tx.objectStore('logs').index('receivedAt').openCursor();
        r.onsuccess = () => { const cursor = r.result; if(cursor){try{append(cursor.value);cursor.continue();}catch(e){reject(e);tx.abort();}} };
        tx.oncomplete = resolve; tx.onabort = () => reject(tx.error || Error('导出读取中断'));
      });
    }
  };
}

let archiveIndexedDB;
try { archiveIndexedDB=globalThis.indexedDB; } catch { /* Report unavailable storage without breaking controller UI. */ }
const localLogArchive = new LocalLogArchive(indexedLogStorage(archiveIndexedDB), archive => {
 $('localLogStatus').textContent = `浏览器本地已保存 ${archive.count} 条，待保存 ${archive.pending.length} 条。` +
   (archive.error || '断线后仍可导出全部记录。') + ' 页面显示最近 500 条；本地历史不自动裁剪。';
 $('localLogs').textContent = archive.recent.map(r => `${r.receivedAt} ${r.bootId || ''} ${r.seq || ''} ${r.ms ?? ''}ms [${r.source}] ${r.line}`).join('\n');
});
async function exportLocalLogs(){
 const button=$('exportLocalLogs');button.disabled=true;
 try {
   const chunks=await localLogArchive.exportChunks(),blob=new Blob(chunks,{type:'application/json'});
   const url=URL.createObjectURL(blob),link=document.createElement('a');link.href=url;
   link.download='esp32-local-history-'+new Date().toISOString().replace(/[:.]/g,'-')+'.json';link.click();
   setTimeout(()=>URL.revokeObjectURL(url),60000);
 }catch(e){$('localLogStatus').textContent='导出失败，未生成不完整文件：'+e.message;}
 finally{button.disabled=false;}
}

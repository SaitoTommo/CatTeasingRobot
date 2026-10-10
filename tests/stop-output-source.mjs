import {readFileSync,writeFileSync} from 'node:fs';
const source=readFileSync(new URL('../esp32-host/ESP32CatHost/ESP32CatHost.ino',import.meta.url),'utf8');
const start=source.indexOf('void flushTx(){'),end=source.indexOf('void policyInput(const String &line);',start);
if(start<0||end<0)throw Error('Missing actual flushTx source');
writeFileSync(process.argv[2],source.slice(start,end));

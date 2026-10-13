import {readFileSync,writeFileSync} from 'node:fs';
const source=readFileSync(new URL('../firmware/UnoCatController/UnoCatController.ino',import.meta.url),'utf8');
const start=source.indexOf('void processCommand(char *line) {');
const end=source.indexOf('  const uint8_t n = count - 3;',start);
if(start<0||end<0)throw Error('Missing actual Uno CRC/window/envelope gate');
writeFileSync(process.argv[2],source.slice(start,end)+'\n  accepted=id; acceptedVerb=verb;\n}\n');

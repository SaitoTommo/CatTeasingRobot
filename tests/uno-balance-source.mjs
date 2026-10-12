import fs from 'node:fs';
const source=fs.readFileSync(new URL('../firmware/UnoCatController/UnoCatController.ino',import.meta.url),'utf8');
function extract(start){const offset=source.indexOf(start);if(offset<0)throw Error(start);const open=source.indexOf('{',offset);let depth=1,end=open+1;while(depth&&end<source.length){if(source[end]==='{')depth++;if(source[end]==='}')depth--;end++;}if(depth)throw Error('unbalanced source');return source.slice(offset,end);}
fs.writeFileSync(process.argv[2],[extract('enum MotionKind'),';',extract('struct Motion'),';','Motion motion = {};',...['stopOutputs','abortMotion','finishMotion','startMotion','renewRemote','tickMotion'].map(name=>extract('void '+name+'('))].join('\n'));

import { readFile, writeFile } from 'node:fs/promises';
import { createHash } from 'node:crypto';
const root=new URL('./ESP32CatHost/',import.meta.url);
const b=await readFile(new URL('cat_model.espdl',root));
if(createHash('sha256').update(b).digest('hex')!=='604501220b26b8a59f44a835b7d37c31444444a8f5661c5ad9469c61fbcd71e1')throw new Error('Unexpected cat model; verify source before updating checksum');
const rows=[];for(let i=0;i<b.length;i+=24)rows.push(Array.from(b.subarray(i,i+24)).join(','));
await writeFile(new URL('cat_model.h',root),'#pragma once\n#include <stdint.h>\nalignas(16) const uint8_t CAT_MODEL[]={\n'+rows.join(',\n')+'\n};\n');

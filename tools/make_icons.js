#!/usr/bin/env node
// Copyright (c) 2026 Dalsin Limited. SPDX-License-Identifier: MIT
// OpenMail's icons, OS 3.2 style: an OS 3.5 colour icon with a classic
// fallback picture, written by ACBuild's amiga-icon.js.
//   node tools/make_icons.js AMIGA_ICON_JS ENVELOPE.rgba OUT_DIR
const fs = require('fs'), path = require('path');
const [, , moduleFile, rgbaFile, outDir] = process.argv;
const A = require(path.resolve(moduleFile));
(async () => {
  const w = 48, h = 40, rgba = new Uint8Array(fs.readFileSync(rgbaFile));
  let doc = A.documentFromRGBA([rgba], w, h);
  doc = A.glowSelected(doc) || doc;
  doc.meta = { type: 3, stack: 4096 };                       // a tool
  fs.mkdirSync(outDir, { recursive: true });
  fs.writeFileSync(path.join(outDir, 'OpenMail.info'), Buffer.from(await A.write(doc, 'os35')));
  console.log(path.join(outDir, 'OpenMail.info'));
})().catch(e => { console.error(e); process.exit(1); });

#!/usr/bin/env node
/**
 * check-i18n.js
 *
 * Verifies that all `description` and `name` fields in api-definition.yaml
 * have matching translations for every language detected in the file.
 *
 * Detection works by scanning for any `description_<lang>` or `name_<lang>`
 * suffix used anywhere in the file, then ensuring every base entry has
 * the same set of translations.
 *
 * Exits 1 if any translations are missing, so it can be used in CI.
 *
 * Usage:
 *   node docs/scripts/check-i18n.js
 */

const fs = require('fs');
const path = require('path');

const API_YAML = path.join(__dirname, '../api-definition.yaml');

const content = fs.readFileSync(API_YAML, 'utf8');
const lines = content.split('\n');

// 1. Detect all language suffixes used in the file
const langs = new Set();
for (const line of lines) {
    const m = line.match(/^\s+(?:description|name)_([a-z]{2,5}):/);
    if (m) langs.add(m[1]);
}
const langList = [...langs].sort();

if (langList.length === 0) {
    console.log('No translation suffixes found. Nothing to check.');
    process.exit(0);
}

console.log(`Detected languages: ${langList.join(', ')}`);

// 2. Walk the file and find every `description:` / `name:` entry,
//    then check that the immediately-following sibling lines (within the
//    same indentation block) include all language variants.
const missing = {
    description: Object.fromEntries(langList.map(l => [l, []])),
    name: Object.fromEntries(langList.map(l => [l, []])),
};

let totalDescription = 0;
let totalName = 0;

function isBaseField(line, field) {
    const re = new RegExp(`^(\\s+)${field}:\\s*(.*)$`);
    return line.match(re);
}

function siblingHas(startIdx, indent, suffixField) {
    // Look ahead a small window for fields at the same indent that match
    // suffixField. Stop when we hit a line with less indent, a list marker
    // at <= indent, or end of file.
    for (let j = startIdx + 1; j < Math.min(startIdx + 20, lines.length); j++) {
        const next = lines[j];
        if (next.trim() === '') continue;
        const leading = next.match(/^(\s*)/)[1];
        if (leading.length < indent.length) return false;
        // List marker at parent level → end of this entry
        if (/^\s*-\s/.test(next) && leading.length <= indent.length) return false;
        if (next.startsWith(indent + suffixField + ':')) return true;
    }
    return false;
}

for (let i = 0; i < lines.length; i++) {
    const line = lines[i];

    // description:
    const mDesc = isBaseField(line, 'description');
    if (mDesc) {
        const indent = mDesc[1];
        const value = mDesc[2].trim().replace(/^"|"$/g, '');
        totalDescription++;
        for (const lang of langList) {
            if (!siblingHas(i, indent, `description_${lang}`)) {
                missing.description[lang].push({ line: i + 1, value });
            }
        }
    }

    // name: (only at indent levels where it represents a category/type, not
    // a list-item function name. We treat any `name:` followed by sibling
    // `name_<lang>` somewhere in the file as worth checking — but skip
    // entries that have no `name_ja`/`name_ko` siblings *and* no other lang
    // siblings either, since those are typically per-function names which
    // don't get translated.)
    const mName = isBaseField(line, 'name');
    if (mName) {
        const indent = mName[1];
        // Only flag if at least one language sibling exists for this entry,
        // OR if any language sibling exists for ANY name in the file (i.e.
        // we've decided this file translates names at all).
        // Simpler rule: only check name entries that already have at least
        // one localized sibling. Function-level `name:` (which is never
        // translated) is silently skipped.
        let hasAnyLangSibling = false;
        for (const lang of langList) {
            if (siblingHas(i, indent, `name_${lang}`)) {
                hasAnyLangSibling = true;
                break;
            }
        }
        if (!hasAnyLangSibling) continue;

        const value = mName[2].trim().replace(/^"|"$/g, '');
        totalName++;
        for (const lang of langList) {
            if (!siblingHas(i, indent, `name_${lang}`)) {
                missing.name[lang].push({ line: i + 1, value });
            }
        }
    }
}

// 3. Report
console.log(`\nChecked: ${totalDescription} description entries, ${totalName} name entries\n`);

let hasMissing = false;

for (const field of ['description', 'name']) {
    for (const lang of langList) {
        const list = missing[field][lang];
        if (list.length === 0) {
            console.log(`[OK] ${field}_${lang}: complete`);
        } else {
            hasMissing = true;
            console.log(`[MISSING] ${field}_${lang}: ${list.length} entries`);
            for (const entry of list.slice(0, 20)) {
                console.log(`  L${entry.line}: ${entry.value}`);
            }
            if (list.length > 20) {
                console.log(`  ... and ${list.length - 20} more`);
            }
        }
    }
}

console.log('\n' + '='.repeat(60));
if (hasMissing) {
    console.log('Translation check FAILED');
    console.log('='.repeat(60));
    process.exit(1);
} else {
    console.log('All translations in sync');
    console.log('='.repeat(60));
}

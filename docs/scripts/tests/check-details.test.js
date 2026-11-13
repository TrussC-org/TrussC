const assert = require('node:assert/strict');
const { test, after } = require('node:test');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { spawnSync } = require('node:child_process');

const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'tc-check-details-'));
after(() => fs.rmSync(tmp, { recursive: true, force: true }));
const structure = path.join(tmp, 'structure.json');
fs.writeFileSync(structure, JSON.stringify({ Plain: {}, Localized: {}, Empty: {} }));
const fixture = path.join(tmp, 'prose.toml');
function check(extra = '', strict = true) {
    // Emoji occupy two UTF-16 code units each; Japanese/Korean characters occupy
    // one. Each field is exactly at the limit before adding the optional suffix.
    fs.writeFileSync(fixture, `
[Plain]
description.en = "Plain fixture"
details = ${JSON.stringify('😀'.repeat(5000) + extra)}
[Localized]
description.en = "Localized fixture"
details.en = ${JSON.stringify('a'.repeat(10000) + extra)}
details.ja = ${JSON.stringify('あ'.repeat(10000) + extra)}
details.ko = ${JSON.stringify('한'.repeat(10000) + extra)}
[Empty]
description.en = "No details"
`);
    const result = spawnSync(process.execPath, [
        path.resolve(__dirname, '../check.js'), '--toml', fixture,
        '--structure', structure, ...(strict ? ['--strict'] : []),
    ], { encoding: 'utf8' });
    assert.ifError(result.error);
    assert.equal(result.stderr, '');
    return result;
}

test('strict accepts plain and en/ja/ko details at exactly 10,000 characters each', () => {
    const result = check();
    assert.equal(result.status, 0, result.stdout);
    assert.match(result.stdout, /details over cap: 0\s+✓/);
});

test('strict rejects over-limit plain and en/ja/ko details with actionable diagnostics', () => {
    const result = check('x');
    assert.equal(result.status, 1, result.stdout);
    assert.match(result.stdout, /details over cap: 4\s+✗ \(strict\)/);
    for (const field of ['Plain details.en', 'Localized details.en', 'Localized details.ja', 'Localized details.ko']) {
        assert.ok(result.stdout.includes(`✗ error: ${field}: 10001 characters (UTF-16 code units) exceeds 10000; split or shorten it.`), result.stdout);
    }
});

test('non-strict reports over-limit details as warnings without failing', () => {
    const result = check('x', false);
    assert.equal(result.status, 0, result.stdout);
    assert.match(result.stdout, /details over cap: 4\s+\(soft\)/);
    assert.match(result.stdout, /warning: Plain details.en/);
    assert.doesNotMatch(result.stdout, /✗ error:/);
});

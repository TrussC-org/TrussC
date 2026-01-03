#!/usr/bin/env node
/**
 * Generate documentation from api-definition.yaml
 *
 * Usage:
 *   node generate-docs.js                  # Generate all outputs
 *   node generate-docs.js --tcscript       # Generate tcScript-related files only
 *   node generate-docs.js --reference      # Generate REFERENCE.md only
 *
 * Outputs:
 *   --tcscript:
 *     - ../trussc.org/tcscript/tcscript-api.js
 *     - ../TrussSketch/REFERENCE.md
 */

const fs = require('fs');
const path = require('path');
const yaml = require('js-yaml');

// Paths
const API_YAML = path.join(__dirname, '../api-definition.yaml');
const TCSCRIPT_API_JS = path.join(__dirname, '../../../trussc.org/sketch/tcscript-api.js');
const REFERENCE_MD = path.join(__dirname, '../../../TrussSketch/REFERENCE.md');

// Parse command line args
const args = process.argv.slice(2);
const generateAll = args.length === 0;
const generateTcScript = generateAll || args.includes('--tcscript');
const generateReference = generateAll || args.includes('--reference');

// Load YAML
function loadAPI() {
    const content = fs.readFileSync(API_YAML, 'utf8');
    return yaml.load(content);
}

// Generate tcscript-api.js
function generateTcScriptAPI(api) {
    const categories = [];

    for (const cat of api.categories) {
        const functions = [];

        for (const fn of cat.functions) {
            // Create entry for each signature
            for (const sig of fn.signatures) {
                functions.push({
                    name: fn.name,
                    params: sig.params_simple,
                    desc: fn.description,
                    snippet: fn.snippet
                });
            }
        }

        categories.push({
            name: cat.name,
            functions: functions
        });
    }

    const constants = api.constants
        .filter(c => c.tcscript)
        .map(c => ({
            name: c.name,
            value: c.value,
            desc: c.description
        }));

    const output = {
        categories: categories,
        constants: constants,
        keywords: api.keywords
    };

    // Generate JavaScript source
    let js = `// tcScript API Definition
// This is the single source of truth for all tcScript functions.
// Used by: autocomplete, reference page, REFERENCE.md generation
//
// AUTO-GENERATED from api-definition.yaml
// Do not edit directly - edit api-definition.yaml instead

const tcScriptAPI = ${JSON.stringify(output, null, 4)};

// Export for different environments
if (typeof module !== 'undefined' && module.exports) {
    module.exports = tcScriptAPI;
}
`;

    return js;
}

// Generate REFERENCE.md
function generateReferenceMd(api) {
    let md = `# tcScript API Reference

Complete API reference for tcScript. All functions are directly mapped from TrussC.

`;

    // Generate each category
    for (const cat of api.categories) {
        // Only include tcScript-enabled functions
        const tcFunctions = cat.functions.filter(fn => fn.tcscript);
        if (tcFunctions.length === 0) continue;

        md += `## ${cat.name}\n\n`;
        md += '```javascript\n';

        // Group overloads
        const seen = new Set();
        for (const fn of tcFunctions) {
            for (const sig of fn.signatures) {
                const sigStr = `${fn.name}(${sig.params_simple})`;
                if (seen.has(sigStr)) continue;
                seen.add(sigStr);

                const padding = Math.max(0, 32 - sigStr.length);
                md += `${sigStr}${' '.repeat(padding)} // ${fn.description}\n`;
            }
        }

        md += '```\n\n';
    }

    // Constants
    md += `## Constants

\`\`\`javascript
`;
    for (const c of api.constants.filter(c => c.tcscript)) {
        const padding = Math.max(0, 28 - c.name.length);
        md += `${c.name}${' '.repeat(padding)} // ${c.value} (${c.description})\n`;
    }
    md += '```\n\n';

    // Variables section
    md += `## Variables

\`\`\`javascript
global myVar = 0         // Global variable (persists across frames)
var localVar = 0         // Local variable (scope-limited)
\`\`\`

## Example

\`\`\`javascript
global angle = 0.0

def setup() {
    logNotice("Starting!")
}

def update() {
    angle = angle + getDeltaTime()
}

def draw() {
    clear(0.1)

    pushMatrix()
    translate(getWindowWidth() / 2.0, getWindowHeight() / 2.0)
    rotate(angle)

    setColor(1.0, 0.5, 0.2)
    drawRect(-50.0, -50.0, 100.0, 100.0)

    popMatrix()
}

def keyPressed(key) {
    logNotice("Key: " + to_string(key))
}
\`\`\`
`;

    return md;
}

// Main
function main() {
    console.log('Loading api-definition.yaml...');
    const api = loadAPI();
    console.log(`  Found ${api.categories.length} categories`);

    if (generateTcScript || generateReference) {
        if (generateTcScript) {
            console.log('\nGenerating tcscript-api.js...');
            const js = generateTcScriptAPI(api);
            fs.writeFileSync(TCSCRIPT_API_JS, js);
            console.log(`  Written: ${TCSCRIPT_API_JS}`);
        }

        if (generateReference) {
            console.log('\nGenerating REFERENCE.md...');
            const md = generateReferenceMd(api);
            fs.writeFileSync(REFERENCE_MD, md);
            console.log(`  Written: ${REFERENCE_MD}`);
        }
    }

    console.log('\nDone!');
}

main();

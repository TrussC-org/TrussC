#!/usr/bin/env node
/**
 * Generate documentation from api-definition.yaml
 *
 * Usage:
 *   node generate-docs.js                  # Generate all outputs
 *   node generate-docs.js --sketch         # Generate TrussSketch-related files only
 *   node generate-docs.js --reference      # Generate REFERENCE.md only
 *
 * Outputs:
 *   --sketch:
 *     - ../trussc.org/sketch/trusssketch-api.js
 *     - ../TrussSketch/REFERENCE.md
 */

const fs = require('fs');
const path = require('path');
const yaml = require('js-yaml');

// Paths
const API_YAML = path.join(__dirname, '../api-definition.yaml');
const SKETCH_API_JS = path.join(__dirname, '../../../trussc.org/sketch/trusssketch-api.js');
const REFERENCE_MD = path.join(__dirname, '../../../TrussSketch/REFERENCE.md');
const REFERENCE_MD_DOCS = path.join(__dirname, '../REFERENCE.md');

// Parse command line args
const args = process.argv.slice(2);
const generateAll = args.length === 0;
const generateSketch = generateAll || args.includes('--sketch');
const generateReference = generateAll || args.includes('--reference');

// Load YAML
function loadAPI() {
    const content = fs.readFileSync(API_YAML, 'utf8');
    return yaml.load(content);
}

// Generate trusssketch-api.js
function generateSketchAPI(api) {
    const categories = [];

    for (const cat of api.categories) {
        const sketchFunctions = cat.functions.filter(fn => fn.sketch);
        if (sketchFunctions.length === 0) continue;

        const functions = [];

        for (const fn of sketchFunctions) {
                                    // Create entry for each signature
                                    for (const sig of fn.signatures) {
                                        functions.push({
                                            name: fn.name,
                                            params: sig.params_simple,
                                            params_typed: sig.params,
                                            return_type: fn.return !== undefined ? fn.return : null,
                                            desc: fn.description,
                                            snippet: fn.snippet
                                        });
                                    }                    }
            
                    categories.push({
                        name: cat.name,
                        functions: functions
                    });
                }
            
                const constants = api.constants
                    .filter(c => c.sketch)
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
                let js = `// TrussSketch API Definition
            // This is the single source of truth for all TrussSketch functions.
            // Used by: autocomplete, reference page, REFERENCE.md generation
            //
            // AUTO-GENERATED from api-definition.yaml
            // Do not edit directly - edit api-definition.yaml instead
            
            const TrussSketchAPI = ${JSON.stringify(output, null, 4)};
            
            // Export for different environments
            if (typeof module !== 'undefined' && module.exports) {
                module.exports = TrussSketchAPI;
            }
            `;
            
                return js;
            }
            
            // Generate REFERENCE.md
            function generateReferenceMd(api) {
                let md = `# TrussC API Reference
            
            Complete API reference. This document is auto-generated from \`api-definition.yaml\`.
            
            For the latest interactive reference, visit [trussc.org/reference](https://trussc.org/reference/).
            
            `;
                // Generate each category
                for (const cat of api.categories) {
                    // Only include TrussSketch-enabled functions
                    const sketchFunctions = cat.functions.filter(fn => fn.sketch);
                    if (sketchFunctions.length === 0) continue;
            
                    md += `## ${cat.name}\n\n`;
                    md += '```cpp\n';
            
                    // Group overloads
                    const seen = new Set();
                    for (const fn of sketchFunctions) {
                        for (const sig of fn.signatures) {
                            let sigStr = `${fn.name}(${sig.params || ''})`;
                            
                            // Prepend return type if available
                            if (fn.return !== undefined) {
                                // Empty string return type means constructor (no return type displayed)
                                if (fn.return === '') {
                                    sigStr = `${fn.name}(${sig.params || ''})`;
                                } else {
                                    sigStr = `${fn.return} ${fn.name}(${sig.params || ''})`;
                                }
                            }
            
                            if (seen.has(sigStr)) continue;
                            seen.add(sigStr);
            
                            const padding = Math.max(0, 40 - sigStr.length);
                            md += `${sigStr}${' '.repeat(padding)} // ${fn.description}\n`;
                        }
                    }
            
                    md += '```\n\n';
                }

    // Constants
    md += `## Constants

\`\`\`cpp
`;
    for (const c of api.constants.filter(c => c.sketch)) {
        const padding = Math.max(0, 28 - c.name.length);
        md += `${c.name}${' '.repeat(padding)} // ${c.value} (${c.description})\n`;
    }
    md += '```\n\n';

    // Variables section
    md += `## Variables

\`\`\`cpp
global myVar = 0         // Global variable (persists across frames)
var localVar = 0         // Local variable (scope-limited)
\`\`\`

## Example

\`\`\`cpp
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

    if (generateSketch || generateReference) {
        if (generateSketch) {
            console.log('\nGenerating trusssketch-api.js...');
            const js = generateSketchAPI(api);
            fs.writeFileSync(SKETCH_API_JS, js);
            console.log(`  Written: ${SKETCH_API_JS}`);
        }

        if (generateReference) {
            console.log('\nGenerating REFERENCE.md...');
            const md = generateReferenceMd(api);
            
            // Write to TrussSketch
            fs.writeFileSync(REFERENCE_MD, md);
            console.log(`  Written: ${REFERENCE_MD}`);

            // Write to TrussC/docs
            fs.writeFileSync(REFERENCE_MD_DOCS, md);
            console.log(`  Written: ${REFERENCE_MD_DOCS}`);
        }
    }

    console.log('\nDone!');
}

main();

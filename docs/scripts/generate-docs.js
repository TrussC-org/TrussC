#!/usr/bin/env node
/**
 * Generate documentation from api-definition.yaml
 *
 * Usage:
 *   node generate-docs.js                  # Generate all outputs
 *   node generate-docs.js --sketch         # Generate TrussSketch-related files only
 *   node generate-docs.js --reference      # Generate REFERENCE.md only
 *   node generate-docs.js --of-mapping     # Generate oF mapping JSON for website
 *   node generate-docs.js --of-markdown    # Generate oF comparison markdown
 *
 * Outputs:
 *   --sketch:
 *     - ../trussc.org/sketch/trusssketch-api.js
 *     - ../TrussSketch/REFERENCE.md
 *   --of-mapping:
 *     - ../trussc.org/of-mapping.json
 *   --of-markdown:
 *     - ../TrussC_vs_openFrameworks.md (Section 5)
 */

const fs = require('fs');
const path = require('path');
const yaml = require('js-yaml');
const { categoryMapping, ofOnlyEntries } = require('./of-category-mapping.js');

// Paths
const API_YAML = path.join(__dirname, '../api-definition.yaml');
const SKETCH_API_JS = path.join(__dirname, '../../../trussc.org/sketch/trusssketch-api.js');
const REFERENCE_MD = path.join(__dirname, '../../../TrussSketch/REFERENCE.md');
const REFERENCE_MD_DOCS = path.join(__dirname, '../REFERENCE.md');
const OF_MAPPING_JSON = path.join(__dirname, '../../../trussc.org/of-mapping.json');
const OF_COMPARISON_MD = path.join(__dirname, '../TrussC_vs_openFrameworks.md');

// Parse command line args
const args = process.argv.slice(2);
const generateAll = args.length === 0;
const generateSketch = generateAll || args.includes('--sketch');
const generateReference = generateAll || args.includes('--reference');
const generateOfMapping = generateAll || args.includes('--of-mapping');
const generateOfMarkdown = generateAll || args.includes('--of-markdown');

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

// Generate oF mapping JSON for website
function generateOfMappingJson(api) {
    // Group functions by display category
    const categoryGroups = {};

    for (const cat of api.categories) {
        const mapping = categoryMapping[cat.id];
        if (!mapping) continue;

        const displayId = mapping.id;
        if (!categoryGroups[displayId]) {
            categoryGroups[displayId] = {
                id: displayId,
                name: mapping.name,
                name_ja: mapping.name_ja,
                order: mapping.order,
                mappings: []
            };
        }

        // Add functions that have of_equivalent
        for (const fn of cat.functions) {
            if (fn.of_equivalent) {
                // Use simple params for cleaner display
                const params = fn.signatures[0]?.params_simple || '';
                categoryGroups[displayId].mappings.push({
                    of: fn.of_equivalent,
                    tc: fn.name + (params ? `(${params})` : '()'),
                    notes: fn.of_notes || '',
                    notes_ja: fn.of_notes_ja || fn.of_notes || ''
                });
            }
        }
    }

    // Add ofOnlyEntries (categories/functions not yet in YAML)
    for (const entry of ofOnlyEntries) {
        if (!categoryGroups[entry.category]) {
            categoryGroups[entry.category] = {
                id: entry.category,
                name: entry.name,
                name_ja: entry.name_ja,
                order: entry.order,
                mappings: []
            };
        }
        for (const e of entry.entries) {
            categoryGroups[entry.category].mappings.push({
                of: e.of,
                tc: e.tc,
                notes: e.notes || '',
                notes_ja: e.notes_ja || e.notes || ''
            });
        }
    }

    // Convert to sorted array
    const categories = Object.values(categoryGroups)
        .filter(cat => cat.mappings.length > 0)
        .sort((a, b) => a.order - b.order);

    return JSON.stringify({ categories }, null, 2);
}

// Generate oF comparison markdown (Section 5)
function generateOfMarkdownSection(api) {
    // Same grouping logic as JSON
    const categoryGroups = {};

    for (const cat of api.categories) {
        const mapping = categoryMapping[cat.id];
        if (!mapping) continue;

        const displayId = mapping.id;
        if (!categoryGroups[displayId]) {
            categoryGroups[displayId] = {
                id: displayId,
                name: mapping.name,
                order: mapping.order,
                mappings: []
            };
        }

        for (const fn of cat.functions) {
            if (fn.of_equivalent) {
                const params = fn.signatures[0]?.params_simple || '';
                categoryGroups[displayId].mappings.push({
                    of: fn.of_equivalent,
                    tc: fn.name + (params ? `(${params})` : '()'),
                    notes: fn.of_notes || ''
                });
            }
        }
    }

    // Add ofOnlyEntries
    for (const entry of ofOnlyEntries) {
        if (!categoryGroups[entry.category]) {
            categoryGroups[entry.category] = {
                id: entry.category,
                name: entry.name,
                order: entry.order,
                mappings: []
            };
        }
        for (const e of entry.entries) {
            categoryGroups[entry.category].mappings.push({
                of: e.of,
                tc: e.tc,
                notes: e.notes || ''
            });
        }
    }

    // Convert to sorted array
    const categories = Object.values(categoryGroups)
        .filter(cat => cat.mappings.length > 0)
        .sort((a, b) => a.order - b.order);

    // Generate markdown
    let md = '';
    for (const cat of categories) {
        md += `### **${cat.name}**\n\n`;
        md += '| openFrameworks | TrussC | Notes |\n';
        md += '|:---|:---|:---|\n';
        for (const m of cat.mappings) {
            md += `| \`${m.of}\` | \`${m.tc}\` | ${m.notes} |\n`;
        }
        md += '\n';
    }

    return md;
}

// Update TrussC_vs_openFrameworks.md with auto-generated section
function updateOfComparisonMarkdown(api) {
    const START_MARKER = '<!-- AUTO-GENERATED-START -->';
    const END_MARKER = '<!-- AUTO-GENERATED-END -->';

    let content = fs.readFileSync(OF_COMPARISON_MD, 'utf8');
    const generatedSection = generateOfMarkdownSection(api);

    const startIdx = content.indexOf(START_MARKER);
    const endIdx = content.indexOf(END_MARKER);

    if (startIdx === -1 || endIdx === -1) {
        console.log('  Warning: Markers not found in TrussC_vs_openFrameworks.md');
        console.log('  Add <!-- AUTO-GENERATED-START --> and <!-- AUTO-GENERATED-END --> markers');
        return null;
    }

    const newContent = content.substring(0, startIdx + START_MARKER.length) +
        '\n\n' + generatedSection +
        content.substring(endIdx);

    return newContent;
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

    // Generate oF mapping JSON
    if (generateOfMapping) {
        console.log('\nGenerating of-mapping.json...');
        const json = generateOfMappingJson(api);
        fs.writeFileSync(OF_MAPPING_JSON, json);
        console.log(`  Written: ${OF_MAPPING_JSON}`);
    }

    // Update oF comparison markdown
    if (generateOfMarkdown) {
        console.log('\nUpdating TrussC_vs_openFrameworks.md...');
        const updatedMd = updateOfComparisonMarkdown(api);
        if (updatedMd) {
            fs.writeFileSync(OF_COMPARISON_MD, updatedMd);
            console.log(`  Updated: ${OF_COMPARISON_MD}`);
        }
    }

    console.log('\nDone!');
}

main();

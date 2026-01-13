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
 *   node generate-docs.js --gemini         # Generate Gemini RAG knowledge base
 *
 * Outputs:
 *   --sketch:
 *     - ../trussc.org/sketch/trusssketch-api.js
 *     - ../TrussSketch/REFERENCE.md
 *   --of-mapping:
 *     - ../trussc.org/of-mapping.json
 *   --of-markdown:
 *     - ../TrussC_vs_openFrameworks.md (Section 5)
 *   --gemini:
 *     - ./trusssketch-knowledge.md
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
const GEMINI_KNOWLEDGE_MD = path.join(__dirname, './trusssketch-knowledge.md');

// Parse command line args
const args = process.argv.slice(2);
const generateAll = args.length === 0;
const generateSketch = generateAll || args.includes('--sketch');
const generateReference = generateAll || args.includes('--reference');
const generateOfMapping = generateAll || args.includes('--of-mapping');
const generateOfMarkdown = generateAll || args.includes('--of-markdown');
const generateGemini = generateAll || args.includes('--gemini');

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

                // Process types (class definitions with properties and methods)
                const types = [];
                if (api.types) {
                    for (const type of api.types) {
                        if (!type.sketch) continue;

                        const typeData = {
                            name: type.name,
                            desc: type.description
                        };

                        // Constructor
                        if (type.constructor && type.constructor.signatures) {
                            typeData.constructor = {
                                signatures: type.constructor.signatures.map(s => s.params || ''),
                                snippet: type.constructor.snippet
                            };
                        }

                        // Properties
                        if (type.properties) {
                            typeData.properties = type.properties.map(p => ({
                                name: p.name,
                                type: p.type,
                                desc: p.description
                            }));
                        }

                        // Methods (instance)
                        if (type.methods) {
                            typeData.methods = type.methods.map(m => ({
                                name: m.name,
                                return: m.return,
                                signatures: m.signatures.map(s => s.params || ''),
                                desc: m.description,
                                snippet: m.snippet
                            }));
                        }

                        // Static methods
                        if (type.static_methods) {
                            typeData.static_methods = type.static_methods.map(m => ({
                                name: m.name,
                                return: m.return,
                                signatures: m.signatures.map(s => s.params || ''),
                                desc: m.description,
                                snippet: m.snippet
                            }));
                        }

                        types.push(typeData);
                    }
                }

                const output = {
                    categories: categories,
                    constants: constants,
                    keywords: api.keywords,
                    types: types
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
float myVar = 0.0;       // Global variable (persists across frames)
\`\`\`

## Example

\`\`\`cpp
float angle = 0.0;

void setup() {
    logNotice("Starting!");
}

void update() {
    angle = angle + getDeltaTime();
}

void draw() {
    clear(0.1);

    pushMatrix();
    translate(getWindowWidth() / 2.0, getWindowHeight() / 2.0);
    rotate(angle);

    setColor(1.0, 0.5, 0.2);
    drawRect(-50.0, -50.0, 100.0, 100.0);

    popMatrix();
}

void keyPressed(int key) {
    logNotice("Key: " + toString(key));
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

// Generate Gemini RAG knowledge base
function generateGeminiKnowledge(api) {
    let md = `# TrussSketch Complete Knowledge Base

This document contains everything about TrussSketch, the web-based creative coding environment for TrussC.

---

## 1. What is TrussSketch?

TrussSketch is a browser-based creative coding playground that runs TrussC code via WebAssembly. It provides:
- Real-time code editing with syntax highlighting and autocomplete
- Instant preview of your sketches
- Share functionality to share your creations via URL
- Video recording to export your animations
- Multiple file support for organizing larger projects

**Live Editor:** https://trussc.org/sketch/
**API Reference:** https://trussc.org/reference/

---

## 2. How to Embed TrussSketch

You can add interactive creative coding backgrounds to any website.

### Quick Start (Fullscreen Background)

\`\`\`html
<script src="https://cdn.trussc.org/sketch.js"></script>
<script type="text/tcs">
void draw() {
    clear(0.1);
    setColor(1, 0.5, 0.2);
    drawCircle(getMouseX(), getMouseY(), 30);
}
</script>
\`\`\`

### Full HTML Example

\`\`\`html
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <title>My Site</title>
    <style>
        body { margin: 0; color: white; font-family: sans-serif; }
        .content { position: relative; z-index: 1; padding: 2rem; }
    </style>
</head>
<body>
    <div class="content">
        <h1>Hello World</h1>
        <p>Your content goes here.</p>
    </div>

    <script src="https://cdn.trussc.org/sketch.js"></script>
    <script type="text/tcs">
float t = 0;

void draw() {
    t += getDeltaTime();
    clear(0.05);

    float cx = getWindowWidth() / 2;
    float cy = getWindowHeight() / 2;

    for (int i = 0; i < 5; i++) {
        float r = 50 + i * 40 + sin(t * 2) * 20;
        setColor(0.3, 0.5, 0.9, 0.2);
        drawCircle(cx, cy, r);
    }
}
    </script>
</body>
</html>
\`\`\`

### Custom Canvas (Non-fullscreen)

\`\`\`html
<canvas id="myCanvas" width="400" height="300"></canvas>

<script src="https://cdn.trussc.org/sketch.js"></script>
<script type="text/tcs" data-canvas="myCanvas">
void draw() {
    clear(0.1);
    drawCircle(200, 150, 50);
}
</script>
\`\`\`

### External File

\`\`\`html
<script src="https://cdn.trussc.org/sketch.js"></script>
<script type="text/tcs" src="sketch.tcs"></script>
\`\`\`

### Self-Hosting

For version stability, download and host these files yourself:
- sketch.js - Loader script
- TrussSketch.js - Runtime
- TrussSketch.wasm - WebAssembly (~2.8MB)

Place them in the same directory and reference your local sketch.js.

---

## 3. Editor Features

### Share
Click the "Share" button to generate a short URL that encodes your entire sketch. The URL can be shared with anyone and will load your exact code.

### Recording
Click the record button (circle icon) to start recording your sketch as a video. Click again to stop and download the video file (WebM format, MP4 on Safari).

### Save/Load
- **Save:** Downloads your sketch as a .tcs file
- **Load:** Opens a file picker to load a .tcs file

### Multiple Files
You can create multiple files (tabs) in the editor. Use the + button to add new files. Files can be:
- .tcs - TrussSketch code
- .glsl - GLSL shaders
- .frag / .vert - Fragment/vertex shaders

### Keyboard Shortcuts
- Ctrl/Cmd + Enter: Run sketch
- Ctrl/Cmd + S: Save sketch
- Ctrl/Cmd + O: Load sketch

---

## 4. Programming Basics

### Lifecycle Functions

\`\`\`cpp
void setup() {
    // Called once at start
}

void update() {
    // Called every frame before draw
}

void draw() {
    // Called every frame for rendering
}
\`\`\`

### Event Callbacks

\`\`\`cpp
void keyPressed(int key) { }    // Key pressed
void keyReleased(int key) { }   // Key released
void mousePressed() { }         // Mouse button pressed
void mouseReleased() { }        // Mouse button released
void mouseMoved() { }           // Mouse moved
void mouseDragged() { }         // Mouse dragged
\`\`\`

### Variables

Global variables persist across frames:

\`\`\`cpp
float angle = 0.0;
int count = 0;
Vec2 pos;

void draw() {
    angle += getDeltaTime();
    // ...
}
\`\`\`

---

## 5. API Reference

`;

    // Generate API by category
    for (const cat of api.categories) {
        const sketchFunctions = cat.functions.filter(fn => fn.sketch);
        if (sketchFunctions.length === 0) continue;

        // Skip "Types - X" categories (handled in Types section)
        if (cat.name.startsWith('Types - ')) continue;

        md += `### ${cat.name}\n\n`;

        for (const fn of sketchFunctions) {
            // Function signature
            const sig = fn.signatures[0];
            const params = sig?.params || '';
            const returnType = fn.return !== undefined && fn.return !== '' ? `${fn.return} ` : '';
            md += `**\`${returnType}${fn.name}(${params})\`**\n`;
            md += `${fn.description}\n`;

            // Additional signatures
            if (fn.signatures.length > 1) {
                md += `Overloads:\n`;
                for (let i = 1; i < fn.signatures.length; i++) {
                    md += `- \`${fn.name}(${fn.signatures[i].params || ''})\`\n`;
                }
            }
            md += '\n';
        }
    }

    // Types section
    md += `---

## 6. Types

`;

    if (api.types) {
        for (const type of api.types) {
            if (!type.sketch) continue;

            md += `### ${type.name}\n\n`;
            md += `${type.description}\n\n`;

            // Constructor
            if (type.constructor && type.constructor.signatures) {
                md += `**Constructor:**\n`;
                for (const sig of type.constructor.signatures) {
                    md += `- \`${type.name}(${sig.params || ''})\`\n`;
                }
                md += '\n';
            }

            // Properties
            if (type.properties && type.properties.length > 0) {
                md += `**Properties:**\n`;
                for (const p of type.properties) {
                    md += `- \`${p.type} ${p.name}\` - ${p.description}\n`;
                }
                md += '\n';
            }

            // Methods
            if (type.methods && type.methods.length > 0) {
                md += `**Methods:**\n`;
                for (const m of type.methods) {
                    const ret = m.return ? `${m.return} ` : '';
                    const params = m.signatures[0]?.params || '';
                    md += `- \`${ret}${m.name}(${params})\` - ${m.description}\n`;
                }
                md += '\n';
            }

            // Static methods
            if (type.static_methods && type.static_methods.length > 0) {
                md += `**Static Methods:**\n`;
                for (const m of type.static_methods) {
                    const ret = m.return ? `${m.return} ` : '';
                    const params = m.signatures[0]?.params || '';
                    md += `- \`${ret}${type.name}::${m.name}(${params})\` - ${m.description}\n`;
                }
                md += '\n';
            }
        }
    }

    // Constants
    md += `---

## 7. Constants

`;
    for (const c of api.constants.filter(c => c.sketch)) {
        md += `- \`${c.name}\` = ${c.value} (${c.description})\n`;
    }

    // Common patterns
    md += `

---

## 8. Common Patterns

### Animation Loop

\`\`\`cpp
float t = 0;

void draw() {
    t += getDeltaTime();
    clear(0.1);

    float x = getWindowWidth() / 2 + cos(t) * 100;
    float y = getWindowHeight() / 2 + sin(t) * 100;

    setColor(1, 0.5, 0.2);
    drawCircle(x, y, 30);
}
\`\`\`

### Mouse Interaction

\`\`\`cpp
void draw() {
    clear(0.05);

    setColor(1, 1, 1);
    drawCircle(getMouseX(), getMouseY(), 20);

    if (isMousePressed()) {
        setColor(1, 0, 0);
        drawCircle(getMouseX(), getMouseY(), 40);
    }
}
\`\`\`

### Keyboard Input

\`\`\`cpp
float x, y;

void setup() {
    x = getWindowWidth() / 2;
    y = getWindowHeight() / 2;
}

void draw() {
    clear(0.1);
    setColor(0, 1, 0.5);
    drawCircle(x, y, 30);
}

void keyPressed(int key) {
    if (key == KEY_LEFT) x -= 10;
    if (key == KEY_RIGHT) x += 10;
    if (key == KEY_UP) y -= 10;
    if (key == KEY_DOWN) y += 10;
}
\`\`\`

### Drawing Shapes

\`\`\`cpp
void draw() {
    clear(0.1);

    // Filled shapes
    setColor(1, 0.5, 0.2);
    drawRect(50, 50, 100, 80);
    drawCircle(250, 100, 40);
    drawTriangle(350, 150, 400, 50, 450, 150);

    // Strokes
    setStrokeColor(0, 1, 1);
    setStrokeWeight(3);
    beginStroke();
    strokeVertex(50, 200);
    strokeVertex(150, 250);
    strokeVertex(100, 300);
    endStroke();
}
\`\`\`

### Transforms

\`\`\`cpp
float angle = 0;

void draw() {
    angle += getDeltaTime();
    clear(0.1);

    pushMatrix();
    translate(getWindowWidth() / 2, getWindowHeight() / 2);
    rotate(angle);
    scale(1.5, 1.0);

    setColor(1, 0.3, 0.5);
    drawRect(-50, -50, 100, 100);

    popMatrix();
}
\`\`\`

### Using Vectors

\`\`\`cpp
Vec2 pos;
Vec2 vel;

void setup() {
    pos = Vec2(getWindowWidth() / 2, getWindowHeight() / 2);
    vel = Vec2(2, 3);
}

void draw() {
    clear(0.02);

    pos = pos + vel;

    // Bounce off walls
    if (pos.x < 0 || pos.x > getWindowWidth()) vel.x = -vel.x;
    if (pos.y < 0 || pos.y > getWindowHeight()) vel.y = -vel.y;

    setColor(1, 1, 0);
    drawCircle(pos.x, pos.y, 20);
}
\`\`\`

### Colors

\`\`\`cpp
void draw() {
    clear(0.1);

    // RGB (0-1 range)
    setColor(1.0, 0.5, 0.2);
    drawCircle(100, 100, 40);

    // RGBA with alpha
    setColor(0, 0.8, 1, 0.5);
    drawCircle(150, 100, 40);

    // HSB mode
    for (int i = 0; i < 10; i++) {
        Color c = Color::fromHsb(i / 10.0, 1, 1);
        setColor(c);
        drawRect(50 + i * 40, 200, 35, 50);
    }
}
\`\`\`

### Offscreen Rendering (Fbo)

\`\`\`cpp
Fbo fbo;

void setup() {
    fbo.allocate(400, 300);
}

void draw() {
    // Draw to FBO
    fbo.begin(0, 0, 0, 1);
    setColor(1, 0, 0);
    drawCircle(200, 150, 50);
    fbo.end();

    // Draw FBO to screen
    clear(0.1);
    fbo.draw(100, 100);
}
\`\`\`

---

## 9. Differences from openFrameworks

TrussSketch uses a simplified API compared to openFrameworks:

- **No ofApp class** - Just write functions directly
- **No of prefix** - \`drawCircle()\` instead of \`ofDrawCircle()\`
- **Colors are 0-1** - Not 0-255
- **Angles in radians** - Use \`TAU\` (2π) for full rotation
- **\`clear()\`** - Instead of \`ofBackground()\`
- **\`getDeltaTime()\`** - Instead of \`ofGetLastFrameTime()\`

---

## 10. Tips & Troubleshooting

### Performance
- Use \`clear(0.1)\` with low alpha for trail effects (cheaper than storing history)
- Avoid creating objects every frame
- Use \`getDeltaTime()\` for frame-rate independent animation

### Common Mistakes
- Forgetting to call \`clear()\` - screen won't update visibly
- Using 0-255 for colors - TrussSketch uses 0-1 range
- Using degrees for angles - Use radians (multiply degrees by \`TAU/360\`)

### Debugging
- Use \`logNotice("message")\` to log to the console panel
- Check the browser console (F12) for JavaScript errors

---

*This document is auto-generated from api-definition.yaml*
`;

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

    // Generate Gemini knowledge base
    if (generateGemini) {
        console.log('\nGenerating trusssketch-knowledge.md...');
        const knowledge = generateGeminiKnowledge(api);
        fs.writeFileSync(GEMINI_KNOWLEDGE_MD, knowledge);
        console.log(`  Written: ${GEMINI_KNOWLEDGE_MD}`);
    }

    console.log('\nDone!');
}

main();

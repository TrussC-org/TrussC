#!/usr/bin/env node
/**
 * validate-bindings.js
 *
 * Validates that api-definition.yaml (sketch: true) functions
 * are properly bound in tcScriptHost.cpp
 */

const fs = require('fs');
const path = require('path');
const yaml = require('js-yaml');

// Paths
const SCRIPT_DIR = __dirname;
const API_YAML = path.join(SCRIPT_DIR, '../api-definition.yaml');
const BINDING_CPP = path.join(SCRIPT_DIR, '../../../TrussSketch/src/tcScriptHost.cpp');

// Categories/functions that are user-defined callbacks, not bindings
const CALLBACK_CATEGORIES = ['Lifecycle', 'Events'];
const CALLBACK_FUNCTIONS = ['setup', 'update', 'draw', 'mousePressed', 'mouseReleased',
    'mouseMoved', 'mouseDragged', 'keyPressed', 'keyReleased', 'windowResized'];

// Categories where functions are actually object methods (category name -> type names)
const METHOD_CATEGORIES = {
    'Sound': ['Sound'],
    'ChipSound': ['ChipSoundNote', 'ChipSoundBundle'],
    'Font': ['Font'],
    'Animation': ['Tween'],
    '3D Camera': ['EasyCam'],
    'Graphics - FBO': ['Fbo'],
    'Graphics - Texture & GPU': ['Texture'],
    'Types - Mesh': ['Mesh'],
    'Types - Path': ['Path'],
    'Types - StrokeMesh': ['StrokeMesh'],
    'Types - Pixels': ['Pixels'],
    'Image': ['Image'],
};

// Parse api-definition.yaml and extract sketch functions
function parseYamlApi(yamlPath) {
    const content = fs.readFileSync(yamlPath, 'utf8');
    const api = yaml.load(content);

    const globalFunctions = new Map();  // name -> info
    const objectMethods = new Map();    // "Type.name" -> info
    const callbacks = new Set();        // callback function names

    // Parse categories (global functions)
    if (api.categories) {
        for (const category of api.categories) {
            if (!category.functions) continue;

            // Skip callback categories
            if (CALLBACK_CATEGORIES.includes(category.name)) {
                for (const func of category.functions) {
                    if (func.sketch) callbacks.add(func.name);
                }
                continue;
            }

            for (const func of category.functions) {
                if (!func.sketch) continue;

                const sigs = [];
                if (func.signatures) {
                    for (const sig of func.signatures) {
                        sigs.push({
                            params: sig.params_simple || sig.params || '',
                            return: func.return || 'void'
                        });
                    }
                }

                const info = {
                    name: func.name,
                    signatures: sigs,
                    category: category.name,
                };

                // Check if this category's functions are actually object methods
                const methodTypes = METHOD_CATEGORIES[category.name];
                if (methodTypes !== undefined) {
                    // These are object methods, not global functions
                    // We'll handle them when checking types
                    info.isObjectMethod = true;
                    info.expectedTypes = methodTypes;  // Array of possible types
                }

                const key = func.name;
                if (globalFunctions.has(key)) {
                    // Store as array if multiple entries with same name
                    const existing = globalFunctions.get(key);
                    if (Array.isArray(existing)) {
                        existing.push(info);
                    } else {
                        globalFunctions.set(key, [existing, info]);
                    }
                } else {
                    globalFunctions.set(key, info);
                }
            }
        }
    }

    // Parse types (methods)
    if (api.types) {
        for (const type of api.types) {
            const typeName = type.name;

            // Methods
            if (type.methods) {
                for (const method of type.methods) {
                    if (method.sketch === false) continue;

                    const sigs = [];
                    if (method.signatures) {
                        for (const sig of method.signatures) {
                            sigs.push({
                                params: sig.params_simple || sig.params || '',
                                return: method.return || 'void'
                            });
                        }
                    } else {
                        sigs.push({
                            params: '',
                            return: method.return || 'void'
                        });
                    }

                    const key = `${typeName}.${method.name}`;
                    objectMethods.set(key, {
                        name: method.name,
                        signatures: sigs,
                        typeName: typeName,
                    });
                }
            }

            // Static methods
            if (type.static_methods) {
                for (const method of type.static_methods) {
                    if (method.sketch === false) continue;

                    const sigs = [];
                    if (method.signatures) {
                        for (const sig of method.signatures) {
                            sigs.push({
                                params: sig.params_simple || sig.params || '',
                                return: method.return || 'void'
                            });
                        }
                    }

                    // Static methods are registered as global functions
                    globalFunctions.set(method.name, {
                        name: method.name,
                        signatures: sigs,
                        typeName: typeName,
                        isStatic: true
                    });
                }
            }
        }
    }

    return { globalFunctions, objectMethods, callbacks };
}

// Parse tcScriptHost.cpp and extract registered functions
function parseBindings(cppPath) {
    const content = fs.readFileSync(cppPath, 'utf8');

    const globalFunctions = new Map(); // name -> [signatures]
    const objectMethods = new Map();   // "Type.name" -> [signatures]
    const objectTypes = new Set();     // registered type names

    // Match RegisterGlobalFunction calls
    // Pattern: RegisterGlobalFunction("return_type name(params)", ...)
    const globalRegex = /RegisterGlobalFunction\s*\(\s*"([^"]+)"/g;
    let match;
    while ((match = globalRegex.exec(content)) !== null) {
        const sig = match[1];
        const parsed = parseSignature(sig);
        if (parsed) {
            const key = parsed.name;
            if (!globalFunctions.has(key)) {
                globalFunctions.set(key, []);
            }
            globalFunctions.get(key).push(parsed);
        }
    }

    // Match RegisterObjectMethod calls
    // Pattern: RegisterObjectMethod("Type", "return_type name(params)", ...)
    const methodRegex = /RegisterObjectMethod\s*\(\s*"(\w+)"\s*,\s*"([^"]+)"/g;
    while ((match = methodRegex.exec(content)) !== null) {
        const typeName = match[1];
        const sig = match[2];
        const parsed = parseSignature(sig);
        if (parsed) {
            const key = `${typeName}.${parsed.name}`;
            if (!objectMethods.has(key)) {
                objectMethods.set(key, []);
            }
            objectMethods.get(key).push(parsed);
        }
    }

    // Match RegisterObjectType calls
    const typeRegex = /RegisterObjectType\s*\(\s*"(\w+)"/g;
    while ((match = typeRegex.exec(content)) !== null) {
        objectTypes.add(match[1]);
    }

    return { globalFunctions, objectMethods, objectTypes };
}

// Parse AngelScript signature string
// e.g., "void drawRect(float, float, float, float)" -> { name: "drawRect", return: "void", params: [...] }
function parseSignature(sig) {
    // Handle const methods
    sig = sig.replace(/\s+const$/, '');

    // Match: return_type name(params)
    const match = sig.match(/^(.+?)\s+(\w+)\s*\(([^)]*)\)$/);
    if (!match) return null;

    const returnType = match[1].trim();
    const name = match[2];
    const paramsStr = match[3].trim();

    // Parse params - extract types and names
    const params = [];
    if (paramsStr) {
        // Split by comma, handling "const Type &in" patterns
        const parts = paramsStr.split(/\s*,\s*/);
        for (const part of parts) {
            // Extract just the type (ignore param names)
            const typeMatch = part.match(/^(.+?)(?:\s+\w+)?$/);
            if (typeMatch) {
                params.push(typeMatch[1].trim());
            }
        }
    }

    return { name, return: returnType, params, raw: sig };
}

// Normalize type for comparison
function normalizeType(t) {
    if (!t) return '';
    return t
        .replace(/const\s+/g, '')
        .replace(/\s*&\s*in/g, '')
        .replace(/\s*&\s*/g, '')
        .replace(/\s+/g, ' ')
        .trim();
}

// Compare and report
function validate() {
    console.log('Loading api-definition.yaml...');
    const yaml = parseYamlApi(API_YAML);
    console.log(`  Found ${yaml.globalFunctions.size} global functions`);
    console.log(`  Found ${yaml.objectMethods.size} object methods`);
    console.log(`  Found ${yaml.callbacks.size} callbacks (excluded)\n`);

    console.log('Loading tcScriptHost.cpp...');
    const bindings = parseBindings(BINDING_CPP);
    console.log(`  Found ${bindings.globalFunctions.size} global functions`);
    console.log(`  Found ${bindings.objectMethods.size} object methods`);
    console.log(`  Found ${bindings.objectTypes.size} object types\n`);

    const missingGlobal = [];
    const missingMethod = [];
    const matchedGlobal = [];
    const matchedMethod = [];

    // Helper to get all funcs (handles both single and array)
    function getAllFuncs(entry) {
        return Array.isArray(entry) ? entry : [entry];
    }

    // Check YAML global functions against bindings
    for (const [name, funcEntry] of yaml.globalFunctions) {
        const funcs = getAllFuncs(funcEntry);

        for (const func of funcs) {
            if (func.isObjectMethod) {
                // This is marked as object method in category, check if bound as method
                const expectedTypes = func.expectedTypes || [];
                let found = false;

                // Try expected types first
                for (const expectedType of expectedTypes) {
                    const key = `${expectedType}.${name}`;
                    if (bindings.objectMethods.has(key)) {
                        matchedMethod.push({ key, func });
                        found = true;
                        break;
                    }
                }

                // Try to find in any type's methods
                if (!found) {
                    for (const [key, _] of bindings.objectMethods) {
                        if (key.endsWith(`.${name}`)) {
                            matchedMethod.push({ key, func });
                            found = true;
                            break;
                        }
                    }
                }

                if (!found) {
                    // Also check if it's bound as global function (factory like createSound)
                    if (bindings.globalFunctions.has(name)) {
                        matchedGlobal.push({ name, func });
                    } else {
                        missingMethod.push({ name, func, category: func.category });
                    }
                }
            } else {
                // Regular global function
                if (bindings.globalFunctions.has(name)) {
                    matchedGlobal.push({ name, func });
                } else {
                    missingGlobal.push({ name, func, category: func.category });
                }
            }
        }
    }

    // Check YAML object methods against bindings
    for (const [key, method] of yaml.objectMethods) {
        if (bindings.objectMethods.has(key)) {
            matchedMethod.push({ key, method });
        } else {
            missingMethod.push({ key, method, typeName: method.typeName });
        }
    }

    // Find extra bindings not in YAML
    const extraGlobal = [];
    const extraMethod = [];

    for (const [name, sigs] of bindings.globalFunctions) {
        // Skip operators and internal functions
        if (name.startsWith('op') || name.startsWith('_')) continue;

        if (!yaml.globalFunctions.has(name)) {
            extraGlobal.push({ name, sigs });
        }
    }

    for (const [key, sigs] of bindings.objectMethods) {
        // Skip operators
        const [typeName, methodName] = key.split('.');
        if (methodName.startsWith('op')) continue;

        if (!yaml.objectMethods.has(key)) {
            // Check if it matches a category function marked as object method
            let found = false;
            for (const [name, funcEntry] of yaml.globalFunctions) {
                if (methodName !== name) continue;

                const funcs = getAllFuncs(funcEntry);
                for (const func of funcs) {
                    if (func.isObjectMethod) {
                        // Verify the type matches one of the expected types
                        const expectedTypes = func.expectedTypes || [];
                        if (expectedTypes.length === 0 || expectedTypes.includes(typeName)) {
                            found = true;
                            break;
                        }
                    }
                }
                if (found) break;
            }
            if (!found) {
                extraMethod.push({ key, sigs });
            }
        }
    }

    // Report
    console.log('='.repeat(60));
    console.log('VALIDATION RESULTS');
    console.log('='.repeat(60));

    const totalMissing = missingGlobal.length + missingMethod.length;
    const totalMatched = matchedGlobal.length + matchedMethod.length;
    const totalExtra = extraGlobal.length + extraMethod.length;

    if (totalMissing === 0) {
        console.log('\n[OK] All YAML sketch functions are bound!');
    } else {
        if (missingGlobal.length > 0) {
            console.log(`\n[MISSING GLOBAL] ${missingGlobal.length} functions:\n`);
            for (const m of missingGlobal) {
                const category = m.category ? ` (${m.category})` : '';
                console.log(`  - ${m.name}${category}`);
            }
        }
        if (missingMethod.length > 0) {
            console.log(`\n[MISSING METHOD] ${missingMethod.length} methods:\n`);
            for (const m of missingMethod) {
                const typeName = m.typeName ? `[${m.typeName}] ` : '';
                const category = m.category ? ` (${m.category})` : '';
                const key = m.key || m.name;
                console.log(`  - ${typeName}${key}${category}`);
            }
        }
    }

    if (totalExtra > 0) {
        if (extraGlobal.length > 0) {
            console.log(`\n[EXTRA GLOBAL] ${extraGlobal.length} functions not in YAML:\n`);
            for (const e of extraGlobal) {
                console.log(`  - ${e.name}`);
            }
        }
        if (extraMethod.length > 0) {
            console.log(`\n[EXTRA METHOD] ${extraMethod.length} methods not in YAML:\n`);
            for (const e of extraMethod) {
                console.log(`  - ${e.key}`);
            }
        }
    }

    console.log('\n' + '='.repeat(60));
    console.log(`Summary: ${totalMatched} matched, ${totalMissing} missing, ${totalExtra} extra`);
    console.log('='.repeat(60));

    // Exit with error if missing
    if (totalMissing > 0) {
        process.exit(1);
    }
}

// Run
validate();

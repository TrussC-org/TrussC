import argparse
import json
import os
import platform
import subprocess

from clang.cindex import CursorKind, Index, TokenKind


# libclang only sees standard headers (and therefore can only resolve
# `std::string` and friends) if we pass it the same sysroot / resource-dir
# / include paths that the system compiler would use. Without these, the
# parser silently falls back to `int` for any unresolved type — which
# results in trussc_generated.cpp binding `const int & path` instead of
# `const std::string & path`. See issue: bindgen had been silently broken.
def discoverCompileArgs():
    args = ["-x", "c++", "-std=c++20"]

    system = platform.system()
    if system == "Darwin":
        try:
            sdk = subprocess.check_output(
                ["xcrun", "--show-sdk-path"], text=True
            ).strip()
            clangxx = subprocess.check_output(
                ["xcrun", "-find", "clang++"], text=True
            ).strip()
            res = subprocess.check_output(
                [clangxx, "-print-resource-dir"], text=True
            ).strip()
            args += [
                "-isysroot", sdk,
                "-resource-dir", res,
                "-I", f"{sdk}/usr/include/c++/v1",
                "-I", f"{res}/include",
            ]
        except (subprocess.CalledProcessError, FileNotFoundError) as e:
            print(f"WARN: failed to probe Xcode toolchain: {e}")
    elif system == "Linux":
        # Best-effort. CI / contributors may need to set CPLUS_INCLUDE_PATH.
        try:
            res = subprocess.check_output(
                ["clang++", "-print-resource-dir"], text=True
            ).strip()
            args += ["-resource-dir", res, "-I", f"{res}/include"]
        except (subprocess.CalledProcessError, FileNotFoundError):
            pass

    return args


# libclang 18 occasionally fails to resolve template-instantiated parameter
# types (e.g. `const std::vector<Vec3>&`) when the inline body calls into a
# class whose definition needs newer C++ stdlib features. When that happens
# `param.type.spelling` silently returns `"const int &"`, which would corrupt
# the generated lambda signatures. Fall back to extracting the type from the
# raw source tokens, which libclang always exposes correctly.
def _extractParamTypeFromTokens(param):
    tokens = list(param.get_tokens())
    if not tokens:
        return None
    spellings = [t.spelling for t in tokens]
    name = param.spelling
    if name and spellings and spellings[-1] == name:
        spellings = spellings[:-1]
    # Drop default-value clauses ("= 1.0f", "= nullptr"). Anything from
    # the first '=' onward isn't part of the type.
    if "=" in spellings:
        spellings = spellings[: spellings.index("=")]
    if not spellings:
        return None

    # Reassemble C++ type spelling with minimal spacing. The default
    # `" ".join` produces `const std :: vector < Vec3 > &` which compiles
    # but looks noisy and inflates the diff every regen.
    out = []
    no_space_after = {"::", "<", "&", "*"}
    no_space_before = {"::", ">", ",", "&", "*"}
    for i, tok in enumerate(spellings):
        if i == 0:
            out.append(tok)
            continue
        prev = spellings[i - 1]
        if prev in no_space_after or tok in no_space_before:
            out.append(tok)
        else:
            out.append(" " + tok)
    return "".join(out).strip()


def getParamType(param):
    spelled = param.type.spelling
    token_type = _extractParamTypeFromTokens(param)
    if token_type is None:
        return spelled
    # If libclang gave us `int` (or `const int &`, etc.) but the source
    # tokens contain `::` or angle brackets, the libclang type is wrong.
    looks_resolved = ("::" in token_type) or ("<" in token_type)
    if looks_resolved and "int" in spelled:
        return token_type
    # Prefer the spelled type when it looks sensible (it correctly handles
    # cv-qualifiers, references, decayed arrays, etc).
    return spelled

genfile = "trussc_generated.cpp"

prev = """
// WARNING: This file is auto-generated!

#include "tcxLua.h"
#include "TrussC.h"

#include "sol/sol.hpp"
using namespace tc;

// WORKAROUND: to support deprecated functions in lua
#ifndef _MSC_VER
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif // #ifndef _MSC_VER

void tcxLua::setTrussCGeneratedBindings(const std::shared_ptr<sol::state>& lua){
"""

after = """
}

#ifndef _MSC_VER
#pragma GCC diagnostic pop
#pragma clang diagnostic pop
#endif // #ifndef _MSC_VER
"""

target_filenames = [
    "TrussC.h",
    "tcMath.h",
    "tcPrimitives.h",
    "tc3DGraphics.h",
    "tcLog.h"
]

ignore_functions = {
    "TrussC.h": [
        "trussc#setup",
        "trussc#cleanup",
        "trussc#operator|",
        "trussc#operator&",

        # makes errors on binding (reference problem etc):
        "trussc#intersectRect",
        "trussc#getBitmapStringBounds"
    ],
    "tcMath.h" : [
        "trussc#operator*"
    ],
    "tcPrimitives.h" : [
        "trussc#operator*"
    ],
    "tc3DGraphics.h" : [
        "trussc#operator*"
    ],
    "tcLog.h" : [
        "trussc#operator*",
        "trussc#tcGetLogger"
    ]
}

ignore_namespaces = {
    "TrussC.h": [
        "trussc::internal"
    ],
    "tcMath.h": [
        "trussc::internal"
    ]
}

ignore_classes = {
    "TrussC.h": []
}

additional_overloads = {
    "TrussC.h": {
        "trussc#setColor": [
            "[](float r){  trussc::setColor(r); }",
            "[](float r, float g, float b){  trussc::setColor(r, g, b); }"
        ],
        "trussc#setColorHSB": [
            "[](float h, float s, float b){  trussc::setColorHSB(h, s, b); }"
        ],
        "trussc#clear": [
            "[](float r){  trussc::clear(r); }",
            "[](float r, float g, float b){  trussc::clear(r, g, b); }"
        ],
        "trussc#drawBitmapString": [
            "[](const std::string& s, float x, float y){  trussc::drawBitmapString(s, x, y); }"
        ],
        "trussc#drawBitmapStringHighlight": [
            "[](const std::string& s, float x, float y){  trussc::drawBitmapStringHighlight(s, x, y); }"
        ]
    },
}

functions_map = {}

def visitNode(node, ns="", clazz=""):
    # libclang 18 + clang 21 system headers occasionally yields CursorKind
    # ids it doesn't recognise (e.g. 437 inside C++20 stdlib templates).
    # Skip the offending subtree instead of crashing the whole run.
    try:
        node_kind_name = node.kind.name
    except ValueError:
        return

    if node_kind_name == 'FUNCTION_DECL':
        is_ignore = False

        # The translation unit's own root cursor has no location.file.
        if node.location.file is None:
            return
        filepath = node.location.file.name
        filename = os.path.basename(filepath)
        if not (filename in target_filenames):
            return
        function_name = node.spelling
        return_type = node.result_type.spelling

        line_number = node.location.line

        params = []
        for param in node.get_arguments():
            param_name = param.spelling
            param_type = getParamType(param)

            params.append({
                "name": param_name,
                "type": param_type
            })

        if filename in ignore_namespaces:
            if ns in ignore_namespaces[filename]:
                is_ignore = True

        clazz_id = clazz if ns == "" else ns + "::" + clazz
        if clazz == "":
            clazz_id = ns

        if filename in ignore_classes:
            if clazz_id in ignore_classes[filename]:
                is_ignore = True

        id = clazz_id + "#" + function_name

        if filename in ignore_functions:
            # print(filename + f" / {id}")
            if id in ignore_functions[filename]:
                is_ignore = True

        if not is_ignore:
            obj = {
                "id": id,
                # "type": "function",
                "filename": filename,
                "line_number": line_number,
                "function_name": function_name,
                "params": params,
                "return_type": return_type,
                "namespace": ns,
                "class": clazz
            }

            # print("FUNCTION_DECL", obj)
            # outfile.write(f"// {filename}, LINE {line_number}\n")

            # # write("" + obj)
            # outfile.write(str(obj) + "\n")

            detail_id = id + "@" + str(line_number)

            if not (id in functions_map):
                functions_map[id] = {}
            
            if detail_id in functions_map[id]:
                pass
            else:
                functions_map[id][line_number] = obj

    elif node_kind_name =='NAMESPACE':
        if ns == "":
            ns = node.spelling
        else:
            ns = ns + "::" + node.spelling
    elif node_kind_name =='CALL_EXPR':
        # print(declared_function_name, node.location.line)
        pass
    elif node_kind_name =='CLASS_TEMPLATE':
        pass
    elif node_kind_name =='CLASS_DECL':
        if clazz == "":
            clazz = node.spelling
        else:
            clazz = clazz + "." + node.spelling
    else:
        # print("kind_name:", node.kind.name)
        pass

    try:
        children = list(node.get_children())
    except ValueError:
        return
    for c in children:
        visitNode(c, ns, clazz)


lp = "{"
rp = "}"

def bindFunctions(outfile, fn_map):
    for fns in fn_map.values():
        additional_overloads_count = 0
        additional_overloads_strs = []
        i = 0
        for k in fns.keys():
            if i == 0:
                fn0 = fns[k]
                break
            i += 1
        if fn0 is not None:
            filename = fn0["filename"] 
            if filename in additional_overloads:
                if fn0["id"] in additional_overloads[filename]:
                    additional_overloads_strs = additional_overloads[filename][fn0["id"]]
                    additional_overloads_count += len(additional_overloads_strs)

        overloads_count = len(fns) + additional_overloads_count
        if overloads_count == 1:
            fn = None
            i = 0
            for k in fns.keys():
                if i == 0:
                    fn = fns[k]
                    break

            if fn is not None:
                ns = fn["namespace"]
                name = fn["function_name"]
                arg_names = []
                arg_pairs = []
                for param in fn["params"]:
                    arg_names.append(param["name"])
                    arg_pairs.append(param["type"] + " " + param["name"])

                # print("FUNCTION_DECL", obj)
                outfile.write(f"    // {fn["filename"]}, LINE {fn["line_number"]}\n")

                ret = ""
                if fn["return_type"] != "void":
                    ret = "return"

                id = name if ns == "" else ns + "::" + name
                if len(arg_names) == 0:
                    outfile.write(f"    lua->set_function(\"{name}\", [](){lp} {ret} {id}(); {rp});\n")
                else:
                    # outfile.write(f"// args: {arg_names}\n")
                    # outfile.write(f"// args: {", ".join(arg_pairs)}\n")
                    sarg = ", ".join(arg_pairs) 
                    narg = ", ".join(arg_names) 
                    outfile.write(f"    lua->set_function(\"{name}\", []({sarg}){lp} {ret} {id}({narg}); {rp});\n")
        else:

            # outfile.write(f"    // overloads: {overloads_count}\n")

            overloads = []
            fn_name = ""

            i = 0
            for k in fns.keys():
                fn = fns[k]

                ns = fn["namespace"]
                name = fn["function_name"]
                fn_name = name
                arg_names = []
                arg_pairs = []
                for param in fn["params"]:
                    arg_names.append(param["name"])
                    arg_pairs.append(param["type"] + " " + param["name"])

                # print("FUNCTION_DECL", obj)
                outfile.write(f"    // {fn["filename"]}, LINE {fn["line_number"]}\n")

                ret = ""
                if fn["return_type"] != "void":
                    ret = "return"

                id = name if ns == "" else ns + "::" + name
                if len(arg_names) == 0:
                    overloads.append(f"[](){lp} {ret} {id}(); {rp}")
                else:
                    # outfile.write(f"// args: {arg_names}\n")
                    # outfile.write(f"// args: {", ".join(arg_pairs)}\n")
                    sarg = ", ".join(arg_pairs) 
                    narg = ", ".join(arg_names) 
                    overloads.append(f"[]({sarg}){lp} {ret} {id}({narg}); {rp}")

                i += 1

            if additional_overloads_count > 0:
                for s in additional_overloads_strs:
                    overloads.append(f"// NOTE: additional overloads provided by user")
                    overloads.append(f"{s}")

            if fn_name != "":
                overload_fns = ",\n        ".join(overloads)
                outfile.write(f"    lua->set_function(\"{name}\", sol::overload(\n        {overload_fns}\n    ));\n")

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("filename", help="Input file")
    args = parser.parse_args()

    input_filename = os.path.basename(args.filename)

    # Also add the TrussC core include dirs so files like #include "tc/..."
    # resolve, otherwise types declared in headers next to TrussC.h
    # (Vec2, Color, Sound, etc.) appear unresolved.
    truss_root = os.path.abspath(
        os.path.dirname(args.filename)  # e.g. core/include/
    )
    compile_args = discoverCompileArgs() + [
        "-I", truss_root,
        "-I", os.path.join(truss_root, "sokol"),
    ]

    index = Index.create()
    tree = index.parse(args.filename, args=compile_args)

    # Surface any fatal parse errors so silent type-fallbacks (e.g. unresolved
    # `std::string` becoming `int`) get noticed instead of silently corrupting
    # the generated bindings.
    fatal = [d for d in tree.diagnostics if d.severity >= 3]  # 3 = error
    if fatal:
        print(f"WARN: libclang reported {len(fatal)} error(s) while parsing:")
        for d in fatal[:5]:
            print(f"  {d.spelling} at {d.location}")
        if len(fatal) > 5:
            print(f"  ... and {len(fatal) - 5} more")
        print("Continuing — generation may still be correct if errors are "
              "inside stdlib template internals, but verify the output.")

    visitNode(tree.cursor)

    with open(genfile, mode='w') as f:
        f.write(prev)
        f.write("\n")

        bindFunctions(f, functions_map)

        f.write("\n")
        f.write(after)

    print(f"Generated {genfile}")

if __name__ == "__main__":
    main()
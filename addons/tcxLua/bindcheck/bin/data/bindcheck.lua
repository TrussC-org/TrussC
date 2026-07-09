-- Lua binding verification runner.
-- Checks that every expected binding name is actually reachable from Lua,
-- then exercises a safe subset of pure functions to confirm the call path works.

local expected = dofile(getDataPath("expected.lua"))

local report = { missing = {}, present = 0, ut_missing = {}, ut_present = 0, calls = {} }

-- 1) Existence of free functions
for _, name in ipairs(expected.functions) do
    local v = _G[name]
    if v == nil then
        table.insert(report.missing, name)
    else
        report.present = report.present + 1
    end
end

-- 2) Existence of usertypes (sol exposes them as global tables/constructors)
for _, name in ipairs(expected.usertypes) do
    local v = _G[name]
    if v == nil then
        table.insert(report.ut_missing, name)
    else
        report.ut_present = report.ut_present + 1
    end
end

-- 3) Call a safe subset of pure functions and assert results.
local function approx(a, b) return math.abs(a - b) < 1e-4 end
local function check(label, ok)
    table.insert(report.calls, { label = label, ok = ok })
end

local function try(label, fn)
    local ok, res = pcall(fn)
    check(label, ok and res == true)
    if not ok then check(label .. " (error: " .. tostring(res) .. ")", false) end
end

try("clamp(5,0,3)==3",        function() return clamp(5, 0, 3) == 3 end)
try("clamp(-1,0,3)==0",       function() return clamp(-1, 0, 3) == 0 end)
try("sign(-2)==-1",           function() return sign(-2) == -1 end)
try("sq(4)==16",              function() return sq(4) == 16 end)
try("deg2rad(180)~=PI",       function() return approx(deg2rad(180), math.pi) end)
try("rad2deg(PI)~=180",       function() return approx(rad2deg(math.pi), 180) end)
try("dist(0,0,3,4)==5",       function() return approx(dist(0, 0, 3, 4), 5) end)
try("lerp(0,10,0.5)==5",      function() return approx(lerp(0, 10, 0.5), 5) end)
try("remap(5,0,10,0,100)==50",function() return approx(remap(5, 0, 10, 0, 100), 50) end)
try("min(3,7)==3",            function() return min(3, 7) == 3 end)
try("max(3,7)==7",            function() return max(3, 7) == 7 end)
try("abs(-5)==5",             function() return abs(-5) == 5 end)
try("floor(3.7)==3",          function() return floor(3.7) == 3 end)
try("ceil(3.2)==4",           function() return ceil(3.2) == 4 end)
try("round(3.5)==4",          function() return round(3.5) == 4 end)
try("wrap(7,0,5)==2",         function() return approx(wrap(7, 0, 5), 2) end)
try("fract(3.25)==0.25",      function() return approx(fract(3.25), 0.25) end)

-- Vec2 usertype construction + arithmetic
try("Vec2 ctor + add",        function()
    local a = Vec2.new(1, 2)
    local b = Vec2.new(3, 4)
    local c = a + b
    return approx(c.x, 4) and approx(c.y, 6)
end)
try("Vec2 length",            function()
    local v = Vec2.new(3, 4)
    return approx(v:length(), 5)
end)
try("Color ctor",             function()
    local col = Color.new(1, 0, 0, 1)
    return approx(col.r, 1) and approx(col.g, 0)
end)
try("Color ctor rgb (3-arg)", function()       -- default-arg arity: Color(r,g,b)
    local col = Color.new(1, 0, 0)
    return approx(col.r, 1) and approx(col.g, 0)
end)
try("Vec3 unary minus",       function()       -- operator-() 0-arg overload
    local v = -Vec3.new(1, 2, 3)
    return approx(v.x, -1) and approx(v.z, -3)
end)
try("Node present + ctor", function()
    if _G.Node == nil then return false end
    local ok = pcall(function() local n = Node.new(); return n end)
    return ok
end)
try("Serial present", function() return _G.Serial ~= nil end)

-- Emit machine-parseable summary via print (base lib; goes to stdout).
print("##BINDCHECK_BEGIN##")
print(string.format("functions_expected=%d", #expected.functions))
print(string.format("functions_present=%d", report.present))
print(string.format("functions_missing=%d", #report.missing))
for _, n in ipairs(report.missing) do print("MISSING_FN " .. n) end
print(string.format("usertypes_expected=%d", #expected.usertypes))
print(string.format("usertypes_present=%d", report.ut_present))
print(string.format("usertypes_missing=%d", #report.ut_missing))
for _, n in ipairs(report.ut_missing) do print("MISSING_UT " .. n) end
local call_fail = 0
for _, c in ipairs(report.calls) do
    if not c.ok then
        call_fail = call_fail + 1
        print("CALL_FAIL " .. c.label)
    end
end
print(string.format("call_checks=%d", #report.calls))
print(string.format("call_failures=%d", call_fail))
print("##BINDCHECK_END##")

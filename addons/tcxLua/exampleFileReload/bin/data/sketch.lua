color = colors.white
size = 50

function setup()
    print("hello world")
end

function update()
end

function draw()
    clear(0)

    local x = getMouseX()
    local y = getMouseY()

    setColor(color)
    drawCircle(x, y, size)
end

function keyPressed(key)
    local c = string.char(key)
    print("key: " .. c) 

    if c == "w" or c == "W" then
        print("color white")
        color = colors.white
    elseif c == "y" or c == "Y" then
        print("color yellow")
        color = colors.yellow
    elseif c == "b" or c == "B" then
        print("color blue")
        color = colors.blue
    elseif c == "l" or c == "L" then
        print("large")
        size = 150
    elseif c == "s" or c == "S" then
        print("small")
        size = 50
    elseif c == "]" or c == "}" then
        print("bit larger")
        size = size + 10
    elseif c == "[" or c == "{" then
        print("bit smaller")
        size = size - 10
    end
end
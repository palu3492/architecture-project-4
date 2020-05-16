 lw 1 0 one # load reg1 with 1
 lw 2 0 limit # load reg2 with 100
 lw 3 0 zero # load reg3 with 0
start add 3 1 3 # var ++
 lw 4 0 value
 lw 5 0 value
 sw 4 0 value
 sw 5 0 value
 beq 2 3 end # if equal limit then end
 beq 0 0 start # go back to the beginning of the loop
end halt
limit .fill 10
one .fill 1
zero .fill 0
value .fill 99 # random value to load and store
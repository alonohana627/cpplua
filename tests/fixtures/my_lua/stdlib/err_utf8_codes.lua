-- An uncaught error (stage 9): utf8.codes meets an invalid byte in the
-- middle of the iteration, after yielding the valid characters before it.
for pos, code in utf8.codes("ok\xFFno") do print(pos, code) end

-- warn(msg, ...): emits a warning on stderr; "@on" and "@off" switch warnings on and off.
warn("@on")                                   -- the standalone interpreter starts with them off
warn("disk ", "almost ", "full")              -- stderr: Lua warning: disk almost full
warn("@off")
warn("this one is dropped")
print("done")                                 --> done

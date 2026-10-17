-- An uncaught error (stage 9): table.concat meets a table in the list.
print(table.concat({1, 2}, "+"))
print(table.concat({1, {}, 3}, "+"))

-- package.loaded: the table of modules already loaded; require checks it first.
print(package.loaded.string == string)        --> true
package.loaded["fake"] = { answer = 42 }
print(require("fake").answer)                 --> 42
